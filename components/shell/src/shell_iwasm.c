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

#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_WARN

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/errno.h>

#include "wasm_export.h"

#include "esp_log.h"
#include "esp_heap_caps.h"

#include "shell_cmd.h"
#include "shell_utils.h"

typedef struct iwasm_main_arg {
    uint8_t *buffer;
    uint32_t size;
    uint32_t stack_size;
    uint32_t heap_size;
#if CONFIG_WAMR_ENABLE_LIBC_WASI != 0
    const char *env;
    const char *dir;
    const char *addrs;
#endif
    int argc;
    char **argv;
} iwasm_main_arg_t;

static const char TAG[] = "shell_iwasm";

static struct {
    struct arg_int *stack_size;
    struct arg_int *heap_size;
    struct arg_str *file;
    struct arg_str *args;
#if CONFIG_WAMR_ENABLE_LIBC_WASI != 0
    struct arg_str *env;
    struct arg_str *dir;
    struct arg_str *addrs;
#endif

    struct arg_end *end;
} iwasm_main_arg;

static int str2args(const char *str, int *argc, char ***argv)
{
    int n;
    int ret;
    char **argv_buf;
    char *s;
    char *pbuf;
    int len;

    s = (char *)str;
    n = 2;
    len = 1;
    while (*s) {
        if (*s++ == ' ') {
            n++;
        }

        len++;
    }

    pbuf = wasm_runtime_malloc(n * sizeof(char *) + len);
    if (!pbuf) {
        return -ENOMEM;    
    }

    argv_buf = (char **)pbuf;
    s = pbuf + n * sizeof(char *);
    memcpy(s, str, len);
    ret = esp_console_split_argv(s, argv_buf, n);
    if (ret < 0) {
        wasm_runtime_free(pbuf);
        return -EINVAL;
    }

    *argc = ret;
    *argv = argv_buf;

    return 0;
}

#if CONFIG_WAMR_ENABLE_LIBC_WASI != 0
static bool validate_env_str(char *env)
{
    char *p = env;
    int key_len = 0;

    while (*p != '\0' && *p != '=') {
        key_len++;
        p++;
    }

    if (*p != '=' || key_len == 0)
        return false;

    return true;
}

static bool iwasm_prepare_wasi_dir(const char *wasi_dir, char *wasi_dir_buf, uint32_t buf_size)
{
    const char *wasi_root = CONFIG_WASMACHINE_FILE_SYSTEM_BASE_PATH;
    char *p = wasi_dir_buf;
    uint32_t wasi_dir_len = strlen(wasi_dir);
    uint32_t wasi_root_len = strlen(wasi_root);
    uint32_t total_size;
    struct stat st = { 0 };

    /* wasi_dir: wasi_root/wasi_dir */
    total_size = wasi_root_len + 1 + wasi_dir_len + 1;
    if (total_size > buf_size)
        return false;
    memcpy(p, wasi_root, wasi_root_len);
    p += wasi_root_len;
    *p++ = '/';
    memcpy(p, wasi_dir, wasi_dir_len);
    p += wasi_dir_len;
    *p++ = '\0';

    if (mkdir(wasi_dir_buf, 0777) != 0) {
        if (errno == EEXIST) {
            /* Failed due to dir already exist */
            if ((stat(wasi_dir_buf, &st) == 0) && (st.st_mode & S_IFDIR)) {
                return true;
            }
        }

        return false;
    }

    return true;
}

#endif

static void *iwasm_main_thread(void *p)
{
    iwasm_main_arg_t *arg = (iwasm_main_arg_t *)p;
    uint8_t *buffer = arg->buffer;
    uint32_t size = arg->size;
    package_type_t pkg_type;
    const char *exception;
    wasm_module_t wasm_module;
    wasm_module_inst_t wasm_module_inst;
    char error_buf[128];
#if CONFIG_WAMR_ENABLE_LIBC_WASI != 0
    const char *dir_list[8] = { NULL };
    uint32_t dir_list_size = 0;
    const char *env_list[8] = { NULL };
    uint32_t env_list_size = 0;
    const char *addr_pool[8] = { NULL };
    uint32_t addr_pool_size = 0;
#endif

    /* Process options. */
#if CONFIG_WAMR_ENABLE_LIBC_WASI != 0
    if (arg->dir) {
        char *tmp_dir = strtok((char *)arg->dir, ",");
        while (tmp_dir) {
            if (dir_list_size >= sizeof(dir_list) / sizeof(char *)) {
                ESP_LOGE(TAG, "Only allow max dir number %d\n", (int)(sizeof(dir_list) / sizeof(char *)));
                goto fail0;
            }

            char wasi_dir_buf[64] = { 0 };
            if (iwasm_prepare_wasi_dir(tmp_dir, wasi_dir_buf, sizeof(wasi_dir_buf)))
                dir_list[dir_list_size++] = wasi_dir_buf;
            else {
                ESP_LOGE(TAG, "Wasm parse dir string failed: expect \"key=value\", " "got \"%s\"\n", tmp_dir);
                goto fail0;
            }
            tmp_dir = strtok(NULL, ";");
        }
    } else {
        dir_list[0] = CONFIG_WASMACHINE_FILE_SYSTEM_BASE_PATH;
        dir_list_size = 1;
    }

    if (arg->env) {
        char *tmp_env = strtok((char *)arg->env, ",");
        while (tmp_env) {
            if (env_list_size >= sizeof(env_list) / sizeof(char *)) {
                ESP_LOGE(TAG, "Only allow max env number %d\n", (int)(sizeof(env_list) / sizeof(char *)));
                goto fail0;
            }

            if (validate_env_str(tmp_env))
                env_list[env_list_size++] = tmp_env;
            else {
                ESP_LOGE(TAG, "Wasm parse env string failed: expect \"key=value\", " "got \"%s\"\n", tmp_env);
                goto fail0;
            }
        }
    }
    if (arg->addrs) {
        /* like: --addr-pool=100.200.244.255/30 */
        char *token = strtok((char *)arg->addrs, ",");
        while (token) {
            if (addr_pool_size >= sizeof(addr_pool) / sizeof(char *)) {
                ESP_LOGE(TAG, "Only allow max address number %d\n",
                       (int)(sizeof(addr_pool) / sizeof(char *)));
                goto fail0;
            }

            addr_pool[addr_pool_size++] = token;
            ESP_LOGW(TAG, "addrs %s", token);
            token = strtok(NULL, ";");
        }
    } else {
        addr_pool[0] = "0.0.0.0";
        addr_pool_size = 1;
    }
#endif

    pkg_type = get_package_type(buffer, size);
    if (pkg_type == Wasm_Module_Bytecode) {
        ESP_LOGI(TAG, "Run WASM file");
    }
#ifdef CONFIG_WAMR_ENABLE_AOT
    else if(pkg_type == Wasm_Module_AoT) {
        if (wasm_runtime_is_xip_file(buffer, size)) {
            ESP_LOGI(TAG, "Run XIP file");
        } else {
            ESP_LOGI(TAG, "Run AOT file");
        }
    }
#endif
    else {
        ESP_LOGI(TAG, "pkg_type=%d is not support", pkg_type);
        goto fail0;
    }

    ESP_LOGI(TAG, "wasm runtime initialized.");

    if (!(wasm_module = wasm_runtime_load(buffer,
                                          size,
                                          error_buf,
                                          sizeof(error_buf)))) {
        ESP_LOGE(TAG, "%s", error_buf);
        goto fail0;
    }

    ESP_LOGI(TAG, "wasm runtime load module success.");

#if CONFIG_WAMR_ENABLE_LIBC_WASI != 0
    wasm_runtime_set_wasi_args(wasm_module, dir_list, dir_list_size, NULL, 0,
                               env_list, env_list_size, arg->argv, arg->argc);

    wasm_runtime_set_wasi_addr_pool(wasm_module, addr_pool, addr_pool_size);
#endif

    if (!(wasm_module_inst = wasm_runtime_instantiate(wasm_module,
                                                      arg->stack_size,
                                                      arg->heap_size,
                                                      error_buf,
                                                      sizeof(error_buf)))) {
        ESP_LOGE(TAG, "%s", error_buf);
        goto fail1;
    }

    ESP_LOGI(TAG, "wasm runtime instantiate module success.");

    wasm_application_execute_main(wasm_module_inst, arg->argc, arg->argv);
    if ((exception = wasm_runtime_get_exception(wasm_module_inst))) {
        ESP_LOGE(TAG, "%s", exception);
    }

    ESP_LOGI(TAG, "wasm runtime execute app's main function success.");

    wasm_runtime_deinstantiate(wasm_module_inst);
    ESP_LOGI(TAG, "wasm runtime deinstantiate module success.");

fail1:
    wasm_runtime_unload(wasm_module);
    ESP_LOGI(TAG, "wasm runtime unload module success.");
fail0:
    return NULL;
}

static void start_iwasm_thread(const char *str, uint8_t *buffer, uint32_t size)
{
    int ret;
    pthread_t tid;
    pthread_attr_t attr;
    iwasm_main_arg_t arg = {
        .buffer = buffer,
        .size = size
    };

    if (str && str[0]) {
        ret = str2args(str, &arg.argc, &arg.argv);
        if (ret < 0) {
            ESP_LOGE(TAG, "failed to decode arguments errno=%d", ret);
            return;
        }
    } else {
        arg.argc = 0;
        arg.argv = NULL;
    }

    ret = pthread_attr_init(&attr);
    if (ret != 0) {
        ESP_LOGI(TAG, "failed to init attr errno=%d", errno);
        goto fail1;
    }

    ret = pthread_attr_setstacksize(&attr, CONFIG_WASMACHINE_SHELL_WASM_TASK_STACK_SIZE);
    if (ret != 0) {
        ESP_LOGI(TAG, "failed to set stasksize errno=%d", errno);
        goto fail1;
    }

    if (iwasm_main_arg.stack_size->count) {
        arg.stack_size = iwasm_main_arg.stack_size->ival[0];
    } else {
        arg.stack_size = atoi(CONFIG_WASMACHINE_SHELL_WASM_APP_STACK_SIZE);
    }

    if (iwasm_main_arg.heap_size->count) {
        arg.heap_size = iwasm_main_arg.heap_size->ival[0];
    } else {
        arg.heap_size = atoi(CONFIG_WASMACHINE_SHELL_WASM_APP_HEAP_SIZE);
    }

#if CONFIG_WAMR_ENABLE_LIBC_WASI != 0
    if (iwasm_main_arg.env->count) {
        arg.env = iwasm_main_arg.env->sval[0];
    }

    if (iwasm_main_arg.dir->count) {
        arg.dir = iwasm_main_arg.dir->sval[0];
    }

    if (iwasm_main_arg.addrs->count) {
        arg.addrs = iwasm_main_arg.addrs->sval[0];
    }
#endif

    ret = pthread_create(&tid, &attr, iwasm_main_thread, &arg);
    if (ret != 0) {
        ESP_LOGI(TAG, "failed to create task errno=%d", errno);
        goto fail1;
    }

    ret = pthread_join(tid, NULL);
    if (ret != 0) {
        ESP_LOGI(TAG, "failed to join task errno=%d", errno);
    }

fail1:
    if (arg.argv) {
        wasm_runtime_free(arg.argv);
    }
}

static int iwasm_main(int argc, char **argv)
{
    int ret;
    shell_file_t file;
    const char *args_str;
    
    SHELL_CMD_CHECK(iwasm_main_arg);

    ret = shell_open_file(&file, iwasm_main_arg.file->sval[0]);
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to open file %s", iwasm_main_arg.file->sval[0]);
        return ret;
    }

    if (iwasm_main_arg.args->count &&
        iwasm_main_arg.args->sval[0] &&
        iwasm_main_arg.args->sval[0][0]) {
        args_str = iwasm_main_arg.args->sval[0];
        ESP_LOGI(TAG, "args is: \"%s\"", args_str);
    } else {
        args_str = NULL;
    }

    start_iwasm_thread(args_str, file.payload, file.size);

    shell_close_file(&file);

    return 0;
}

void shell_regitser_cmd_iwasm(void)
{
    int cmd_num = 4;

    iwasm_main_arg.stack_size =
        arg_int0("s", "stack_size", "<stack_size>", "WASM App's max stack size in bytes, default is " CONFIG_WASMACHINE_SHELL_WASM_APP_STACK_SIZE);
    iwasm_main_arg.heap_size =
        arg_int0("h", "heap_size", "<heap_size>", "WASM App's max heap size in bytes, default is " CONFIG_WASMACHINE_SHELL_WASM_APP_HEAP_SIZE);
    iwasm_main_arg.file =
        arg_str1(NULL, NULL, "<file>", "File name of WASM App");
    iwasm_main_arg.args =
        arg_str0(NULL, NULL, "<args>", "WASM App's arguments, need to use \"\" to wrap the args");

#if CONFIG_WAMR_ENABLE_LIBC_WASI != 0
    iwasm_main_arg.env =
        arg_str0("e", "env", "<env>", "Pass wasi environment variables with \"key=value\" to the program, seperated with ',', for example: --env=\"key1=value1\",\"key2=value2\"");
    iwasm_main_arg.dir =
        arg_str0("d", "dir", "<dir>", "Grant wasi access to the given host directories to the program, seperated with ',', for example: --dir=<dir1>,<dir2>");
    iwasm_main_arg.addrs =
        arg_str0("a", "addr-pool", "<addrs>", "Grant wasi access to the given network addresses in CIRD notation to the program, seperated with ',', for example: --addr-pool=1.2.3.4/15,2.3.4.5/16");

    cmd_num += 3;
#endif

    iwasm_main_arg.end = arg_end(cmd_num);

    const esp_console_cmd_t cmd = {
        .command = "iwasm",
        .help = "Load WASM App from file-system and execute it",
        .hint = NULL,
        .func = &iwasm_main,
        .argtable = &iwasm_main_arg
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
