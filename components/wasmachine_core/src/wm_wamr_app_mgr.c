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

#include "wm_wamr.h"

#include "app_manager_export.h"
#include "module_wasm_app.h"
#include "runtime_lib.h"
#include "wasm_export.h"

#define APP_MGR_TASK_STACK_SIZE    8192

#ifdef CONFIG_WASMACHINE_TCP_SERVER
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>

#include "esp_log.h"

#define TCP_TX_BUFFER_SIZE      2048
#define TCP_SERVER_LISTEN       5

static const char *TAG = "wm_wamr_app_mgr";

static int listenfd = -1;
static int sockfd = -1;
static pthread_mutex_t sock_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t app_lock = PTHREAD_MUTEX_INITIALIZER;

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

static void *_tcp_server_thread(void *arg)
{
    extern int aee_host_msg_callback(void *msg, uint32_t msg_len);

    int ret;
    char *buf;
    socklen_t cli_len;
    struct sockaddr_in sock_addr;

    buf = wasm_runtime_malloc(TCP_TX_BUFFER_SIZE);
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

        wm_wamr_app_mgr_lock();

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

        wm_wamr_app_mgr_unlock();
    }

errout_bind_sock:
    close(listenfd);
    listenfd = -1;
errout_create_sock:
    wasm_runtime_free(buf);
    return NULL;
}
#endif

static void *_app_mgr_thread(void *p)
{
#ifdef CONFIG_WASMACHINE_TCP_SERVER
    korp_tid tid;
    host_interface interface = {
        .init = host_init,
        .send = host_send,
        .destroy = host_destroy
    };
#else
    host_interface interface = {
        .init = NULL,
        .send = NULL,
        .destroy = NULL
    };
#endif
    /* timer manager */
    if (!init_wasm_timer()) {
        goto fail1;
    }

#ifdef CONFIG_WAMR_ENABLE_LIBC_WASI
    if ( !wasm_set_wasi_root_dir(WM_FILE_SYSTEM_BASE_PATH)) {
        goto fail1;
    }
#endif

#ifdef CONFIG_WASMACHINE_TCP_SERVER
    ESP_ERROR_CHECK(os_thread_create(&tid, _tcp_server_thread, NULL,
                                     APP_MGR_TASK_STACK_SIZE));
#endif

    app_manager_startup(&interface);

fail1:
    wasm_runtime_destroy();

    return NULL;
}

void wm_wamr_app_mgr_lock(void)
{
    assert(pthread_mutex_lock(&app_lock) == 0);
}

void wm_wamr_app_mgr_unlock(void)
{
    assert(pthread_mutex_unlock(&app_lock) == 0);
}

int wm_wamr_app_send_request(request_t *request, uint16_t msg_type)
{
    char *req_p;
    int req_size, req_size_n;
    char leading[2] = { 0x12, 0x34 };

    extern int aee_host_msg_callback(void *msg, uint32_t msg_len);

    req_p = pack_request(request, &req_size);
    if (!req_p) {
        return -1;
    }

    msg_type = htons(msg_type);
    req_size_n = htonl(req_size);

    aee_host_msg_callback(leading, sizeof(leading));
    aee_host_msg_callback(&msg_type, sizeof(msg_type));
    aee_host_msg_callback(&req_size_n, sizeof(req_size_n));
    aee_host_msg_callback(req_p, req_size);

    free_req_resp_packet(req_p);

    return 0;
}

void wm_wamr_app_mgr_init(void)
{
    pthread_t tid;
    pthread_attr_t attr;

    ESP_ERROR_CHECK(pthread_attr_init(&attr));
    ESP_ERROR_CHECK(pthread_attr_setstacksize(&attr, APP_MGR_TASK_STACK_SIZE));
    ESP_ERROR_CHECK(pthread_create(&tid, &attr, _app_mgr_thread, NULL));
}