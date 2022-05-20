// Copyright 2022 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/socket.h>

#include "sdkconfig.h"

#include "app_manager_export.h"
#include "wasm_export.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "protocol_examples_common.h"

#ifdef CONFIG_WASMACHINE_SHELL
#include "shell.h"
#endif

#define TCP_TX_BUFFER_SIZE      2048
#define WAMR_TASK_STACK_SIZE    4096
#define TCP_SERVER_LISTEN       5
#define MALLOC_ALIGN_SIZE       8
#define SPIFFS_MAX_FILES        32

static const char *TAG = "wm_main";

#ifdef CONFIG_WASMACHINE_APP_MGR
static int listenfd = -1;
static int sockfd = -1;
static pthread_mutex_t sock_lock = PTHREAD_MUTEX_INITIALIZER;

static bool host_init(void)
{
    ESP_LOGI(TAG, "init host");
    return true;
}

static int host_send(void *ctx, const char *buf, int size)
{
    int ret;

    if (pthread_mutex_trylock(&sock_lock) == 0) {
        if (sockfd == -1) {
            ESP_LOGE(TAG, "failed to send");
            pthread_mutex_unlock(&sock_lock);
            return 0;
        }

        ESP_LOGI(TAG, "send %d bytes to host", size);

        ret = write(sockfd, buf, size);

        pthread_mutex_unlock(&sock_lock);
        return ret;
    }

    return -1;
}

static void host_destroy(void)
{
    ESP_LOGI(TAG, "destroy host");

    if (listenfd >= 0) {
        ESP_LOGI(TAG, "close listen socket %d", listenfd);
        close(listenfd);
        listenfd = -1;
    }

    pthread_mutex_lock(&sock_lock);
    if (sockfd) {
        ESP_LOGI(TAG, "close host socket %d", sockfd);
        close(sockfd);
        sockfd = -1;
    }
    pthread_mutex_unlock(&sock_lock);
}

static void *tcp_server_thread(void *arg)
{
    extern int aee_host_msg_callback(void *msg, uint32_t msg_len);

    int ret;
    char *buf;
    socklen_t cli_len;
    struct sockaddr_in sock_addr;

    buf = malloc(TCP_TX_BUFFER_SIZE);
    if (!buf) {
        ESP_LOGE(TAG, "failed to malloc buffer");
        return NULL;
    }

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        ESP_LOGE(TAG, "failed create socket");
        goto errout_create_sock;
    }

    bzero(&sock_addr, sizeof(sock_addr));
    sock_addr.sin_family      = AF_INET;
    sock_addr.sin_addr.s_addr = INADDR_ANY;
    sock_addr.sin_port        = htons(CONFIG_WASMACHINE_TCP_PORT);
    ret = bind(listenfd, (struct sockaddr *)&sock_addr, sizeof(sock_addr));
    if (ret < 0) {
        ESP_LOGE(TAG, "failed bind socket");
        goto errout_bind_sock;
    }

    ret = listen(listenfd, TCP_SERVER_LISTEN);
    if (ret < 0) {
        ESP_LOGE(TAG, "failed bind socket");
        goto errout_bind_sock;
    }

    while (1) {
        cli_len = sizeof(sock_addr);

        pthread_mutex_lock(&sock_lock);
        sockfd = accept(listenfd, (struct sockaddr *)&sock_addr, &cli_len);
        pthread_mutex_unlock(&sock_lock);

        if (sockfd < 0) {
            ESP_LOGE(TAG, "failed to accept");
            break;
        }

        ESP_LOGI(TAG, "host is established!");

        for (;;) {
            int n = read(sockfd, buf, TCP_TX_BUFFER_SIZE);
            if (n <= 0) {
                pthread_mutex_lock(&sock_lock);
                ESP_LOGI(TAG, "close host socket %d", sockfd);
                close(sockfd);
                sockfd = -1;
                pthread_mutex_unlock(&sock_lock);

                sleep(1);
                break;
            } else {
                ESP_LOGI(TAG, "recv %d bytes from host", n);
            }

            aee_host_msg_callback(buf, n);
        }
    }

errout_bind_sock:
    close(listenfd);
    listenfd = -1;
errout_create_sock:
    free(buf);
    return NULL;
}

static void *wamr_thread(void *p)
{
    korp_tid tid;
    host_interface interface = {
        .init = host_init,
        .send = host_send,
        .destroy = host_destroy
    };

    ESP_ERROR_CHECK(os_thread_create(&tid, tcp_server_thread, NULL,
                                     WAMR_TASK_STACK_SIZE));

    app_manager_startup(&interface);

    wasm_runtime_destroy();

    return NULL;
}

static void app_mgr_init(void)
{
    pthread_t tid;
    pthread_attr_t attr;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());

    ESP_ERROR_CHECK(pthread_attr_init(&attr));
    ESP_ERROR_CHECK(pthread_attr_setstacksize(&attr, WAMR_TASK_STACK_SIZE));
    ESP_ERROR_CHECK(pthread_create(&tid, &attr, wamr_thread, NULL));
}
#endif

static void *wamr_malloc(unsigned int size)
{
    void *ptr;
#ifdef CONFIG_SPIRAM
    uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
    uint32_t caps = MALLOC_CAP_8BIT;
#endif

    ptr = heap_caps_aligned_alloc(MALLOC_ALIGN_SIZE, size, caps);
    ESP_LOGV(TAG, "malloc ptr=%p size=%u", ptr, size);

    return ptr;
}

static void wamr_free(void *ptr)
{
    ESP_LOGV(TAG, "free ptr=%p", ptr);

    heap_caps_free(ptr);
}

static void *wamr_realloc(void *ptr, unsigned int size)
{
    void *new_ptr;

    new_ptr = wamr_malloc(size);
    if (new_ptr) {
        if (ptr) {
            size_t n = heap_caps_get_allocated_size(ptr);
            size_t m = MIN(size, n);
            memcpy(new_ptr, ptr, m);
            wamr_free(ptr);
        }
    }

    ESP_LOGV(TAG, "realloc ptr=%p size=%u new_ptr=%p", ptr, size, new_ptr);

    return new_ptr;
}

static void wamr_init(void)
{
    RuntimeInitArgs init_args;

    memset(&init_args, 0, sizeof(RuntimeInitArgs));
    init_args.mem_alloc_type = Alloc_With_Allocator;
    init_args.mem_alloc_option.allocator.malloc_func  = wamr_malloc;
    init_args.mem_alloc_option.allocator.realloc_func = wamr_realloc;
    init_args.mem_alloc_option.allocator.free_func    = wamr_free;
    assert(wasm_runtime_full_init(&init_args));
}

static void fs_init(void)
{
    size_t total = 0, used = 0;
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = SPIFFS_MAX_FILES,
        .format_if_mount_failed = false
    };

    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
    ESP_ERROR_CHECK(esp_spiffs_info(conf.partition_label, &total, &used));

    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
}

void app_main(void)
{
    fs_init();

    wamr_init();

#ifdef CONFIG_WASMACHINE_APP_MGR
    app_mgr_init();
#endif

#ifdef CONFIG_WASMACHINE_SHELL
    shell_init();
#endif
}
