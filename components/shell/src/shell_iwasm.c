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

#include "shell_cmd.h"

typedef struct iwasm_main_arg {
    uint8_t *buffer;
    uint32_t size;
    uint32_t stack_size;
    uint32_t heap_size;
    int argc;
    char **argv;
} iwasm_main_arg_t;

static const char TAG[] = "shell_iwasm";

static struct {
    struct arg_int *stack_size;
    struct arg_int *heap_size;
    struct arg_str *file;
    struct arg_str *args;
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

    pbuf = malloc(n * sizeof(char *) + len);
    if (!pbuf) {
        return -ENOMEM;    
    }

    argv_buf = (char **)pbuf;
    s = pbuf + n * sizeof(char *);
    memcpy(s, str, len);
    ret = esp_console_split_argv(s, argv_buf, n);
    if (ret < 0) {
        free(pbuf);
        return -EINVAL;
    }

    *argc = ret;
    *argv = argv_buf;

    return 0;
}

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
        }
    } else {
        arg.argc = 0;
        arg.argv = NULL;
    }

    ret = pthread_attr_init(&attr);
    if (ret < 0) {
        ESP_LOGI(TAG, "failed to init attr errno=%d", errno);
        goto exit;
    }

    ret = pthread_attr_setstacksize(&attr, CONFIG_WASMACHINE_SHELL_WASM_TASK_STACK_SIZE);
    if (ret < 0) {
        ESP_LOGI(TAG, "failed to set stasksize errno=%d", errno);
        return ;
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

    ret = pthread_create(&tid, &attr, iwasm_main_thread, &arg);
    if (ret < 0) {
        ESP_LOGI(TAG, "failed to create task errno=%d", errno);
        return ;
    }

    ret = pthread_join(tid, NULL);
    if (ret < 0) {
        ESP_LOGI(TAG, "failed to join task errno=%d", errno);
        return ;
    }

exit:
    if (arg.argv) {
        free(arg.argv);
    }
}

static int iwasm_main(int argc, char **argv)
{
    int fd;
    int ret;
    off_t size;
    uint8_t *pbuf;
    char *file_path;
    const char *args_str;
    
    SHELL_CMD_CHECK(iwasm_main_arg);

    ret = asprintf(&file_path, SHELL_ROOT_FS_PATH"/%s", iwasm_main_arg.file->sval[0]);
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to generate path errno=%d", errno);
        return -1;
    }

    ESP_LOGI(TAG, "Opening file %s", file_path);

    fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "Failed to open file %s errno=%d", file_path, errno);
        goto errout_open_file;
    }

    size = lseek(fd, 0, SEEK_END);
    if (size == -1) {
        ESP_LOGE(TAG, "Failed to seek file %s errno=%d", file_path, errno);
        goto errout_lseek_end;
    }

    ret = lseek(fd, 0, SEEK_SET);
    if (ret == -1) {
        ESP_LOGE(TAG, "Failed to seek file %s errno=%d", file_path, errno);
        goto errout_lseek_end;
    }

    pbuf = malloc(size);
    if (!pbuf) {
        ESP_LOGE(TAG, "Failed to malloc %lu bytes", size);
        goto errout_lseek_end;
    }

    ESP_LOGI(TAG, "Total %lu bytes", size);
    ret = read(fd, pbuf, size);
    if (ret != size) {
        ESP_LOGE(TAG, "Failed to read ret=%d", ret);
        goto errout_read_fs;
    }

    if (iwasm_main_arg.args->count &&
        iwasm_main_arg.args->sval[0] &&
        iwasm_main_arg.args->sval[0][0]) {
        args_str = iwasm_main_arg.args->sval[0];
        ESP_LOGI(TAG, "args is: \"%s\"", args_str);
    } else {
        args_str = NULL;
    }

    start_iwasm_thread(args_str, pbuf, size);

    free(pbuf);
    close(fd);
    free(file_path);

    return 0;

errout_read_fs:
    free(pbuf);
errout_lseek_end:
    close(fd);
errout_open_file:
    free(file_path);
    return -1;
}

void shell_regitser_cmd_iwasm(void)
{
    iwasm_main_arg.stack_size =
        arg_int0("s", "stack_size", "<stack_size>", "WASM App's max stack size in bytes, default is " CONFIG_WASMACHINE_SHELL_WASM_APP_STACK_SIZE);
    iwasm_main_arg.heap_size =
        arg_int0("h", "heap_size", "<heap_size>", "WASM App's max heap size in bytes, default is " CONFIG_WASMACHINE_SHELL_WASM_APP_HEAP_SIZE);
    iwasm_main_arg.file =
        arg_str1(NULL, NULL, "<file>", "File name of WASM App");
    iwasm_main_arg.args =
        arg_str0(NULL, NULL, "<args>", "WASM App's arguments, need to use \"\" to wrap the args");

    iwasm_main_arg.end = arg_end(4);

    const esp_console_cmd_t cmd = {
        .command = "iwasm",
        .help = "Load WASM App from file-system and execute it",
        .hint = NULL,
        .func = &iwasm_main,
        .argtable = &iwasm_main_arg
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
