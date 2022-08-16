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

#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include "bh_common.h"
#include "bh_log.h"
#include "bh_platform.h"
#include "app_manager_export.h"
#include "module_wasm_app.h"
#include "wasm_export.h"
#include "wasm_native.h"
#include "wasm_runtime_common.h"

#include "wm_ext_wasm_native_macro.h"
#include "wm_ext_wasm_native_export.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_event.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>

static const char *TAG = "http_client_wrapper";

/**
 * @brief Function ID for the HTTP client
 */
#define HTTP_CLIENT_INIT                0
#define HTTP_CLIENT_SET_STR             1
#define HTTP_CLIENT_GET_STR             2
#define HTTP_CLIENT_SET_INT             3
#define HTTP_CLIENT_GET_INT             4
#define HTTP_CLIENT_COMMON              5

/**
 * @brief Set string ID for the HTTP client
 */
#define HTTP_CLIENT_SET_URL             0
#define HTTP_CLIENT_SET_POST_FILED      1
#define HTTP_CLIENT_SET_HEADER          2
#define HTTP_CLIENT_SET_USERNAME        3
#define HTTP_CLIENT_SET_PASSWORD        4
#define HTTP_CLIENT_DELETE_HEADER       5
#define HTTP_CLIENT_WRITE_DATA          6

/**
 * @brief Get string ID for the HTTP client
 */

#define HTTP_CLIENT_GET_POST_FILED      0
#define HTTP_CLIENT_GET_HEADER          1
#define HTTP_CLIENT_GET_USERNAME        2
#define HTTP_CLIENT_GET_PASSWORD        3
#define HTTP_CLIENT_READ_DATA           4
#define HTTP_CLIENT_READ_RESP           5
#define HTTP_CLIENT_GET_URL             6

/**
 * @brief Set value ID for the HTTP client
 */
#define HTTP_CLIENT_SET_AUTHTYPE        0
#define HTTP_CLIENT_SET_METHOD          1
#define HTTP_CLIENT_SET_TIMEOUT         2
#define HTTP_CLIENT_OPEN                3

/**
 * @brief Get value ID for the HTTP client
 */
#define HTTP_CLIENT_GET_ERRNO           0
#define HTTP_CLIENT_FETCH_HEADER        1
#define HTTP_CLIENT_IS_CHUNKED          2
#define HTTP_CLIENT_GET_STATUS_CODE     3
#define HTTP_CLIENT_GET_CONTENT_LENGTH  4
#define HTTP_CLIENT_GET_TRANSPORT_TYPE  5
#define HTTP_CLIENT_IS_COMPLETE         6
#define HTTP_CLIENT_FLUSH_RESPONSE      7
#define HTTP_CLIENT_GET_CHUNK_LENGTH    8

/**
 * @brief Common ID for the HTTP client
 */
#define HTTP_CLIENT_PERFORM             0
#define HTTP_CLIENT_CLOSE               1
#define HTTP_CLIENT_CLEANUP             2
#define HTTP_CLIENT_SET_REDIRECTION     3
#define HTTP_CLIENT_ADD_AUTH            4

#define HTTP_CLIENT_ARG_BUF_NUM        16
#define HTTP_CLIENT_ARG_NUM_MAX        64

#define http_client_native_get_arg(type, name)     type name = *((type *)(args++))
#define http_client_native_set_return(val)         *args_ret = (uint32_t)(val)

#define DEFINE_HTTP_CLIENT_NATIVE_WRAPPER(func_name)                   \
    static int func_name ## _wrapper(wasm_exec_env_t exec_env, \
                                      uint32_t *args,           \
                                      uint32_t *args_ret)

#define HTTP_CLIENT_NATIVE_WRAPPER(id, func_name, argc)                \
    [id] = { func_name ## _wrapper, argc }

typedef int (*http_client_func_t)(wasm_exec_env_t exec_env, uint32_t *args, uint32_t *args_ret);

typedef struct http_client_func_desc {
    http_client_func_t  func;
    uint32_t            argc;
} http_client_func_desc_t;

typedef struct __http_client_wrapper_ctx_t {
    wasm_exec_env_t             exec_env;
    esp_http_client_handle_t    client;
    uint32_t                    event_handler;
    void                        *user_data;
    uint32_t                    crt_bundle_attach;
} http_client_wrapper_ctx_t __attribute__((aligned(4)));

#define CONFIG_HTTP_CLIENT_HEAP_SIZE 16384

static bool ptr_is_in_ram_or_rom(const void *ptr)
{
    bool ret = false;

    if (esp_ptr_in_dram(ptr)) {
        ret = true;
    } else if (esp_ptr_in_drom(ptr)) {
        ret = true;
    } else if (esp_ptr_external_ram(ptr)) {
        ret = true;
    }

    return ret;
}

static char *map_string(wasm_exec_env_t exec_env, const char *str)
{
    void *ptr;
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (ptr_is_in_ram_or_rom(str)) {
        return (void *)str;
    }

    ptr = str ? (char *)addr_app_to_native((uint32_t)str) : NULL;
    if (str && !ptr) {
        ESP_LOGE(TAG, "failed to map str=%p", str);
    } else {
        str = ptr;
    }

    return (char *)str; 
}

static bool http_client_run_wasm(wasm_exec_env_t env, uint32_t cb, int argc, uint32_t *argv)
{
    wasm_exec_env_t exec_env = wasm_runtime_create_exec_env(get_module_inst(env), CONFIG_HTTP_CLIENT_HEAP_SIZE);
    if (!exec_env) {
        ESP_LOGE(TAG, "failed to create execution environment");
        return false;
    } 
    
    bool ret = wasm_runtime_call_indirect(exec_env, cb, argc, argv);
    if (!ret) {
        ESP_LOGE(TAG, "failed to run WASM callback as %s", wasm_runtime_get_exception(get_module_inst(exec_env)));
    }

    wasm_runtime_destroy_exec_env(exec_env);

    return ret;
}

static esp_err_t wasm_http_client_event_handler(esp_http_client_event_t *evt)
{
    http_client_wrapper_ctx_t *http_client_wrapper = evt->user_data;
    wasm_module_inst_t module_inst = get_module_inst(http_client_wrapper->exec_env);
    uint32_t key = 0, value = 0;
    
    uint32_t argv[4] = { 0 };

    argv[0] = (uint32_t)evt->event_id;
    argv[1] = (uint32_t)http_client_wrapper->user_data;

    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            argv[2] = key = (uint32_t)wasm_runtime_module_dup_data(module_inst, evt->header_key, strlen(evt->header_key));
            argv[3] = value = (uint32_t)wasm_runtime_module_dup_data(module_inst, evt->header_value, strlen(evt->header_value));
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            argv[2] = key = (uint32_t)wasm_runtime_module_dup_data(module_inst, evt->data, evt->data_len);
            argv[3] = (uint32_t)evt->data_len;
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            argv[2] = (uint32_t)evt->data;
            argv[3] = (uint32_t)evt->data_len;
            break;
        default:
            break;
    }

    http_client_run_wasm(http_client_wrapper->exec_env, http_client_wrapper->event_handler, sizeof(argv) / sizeof(argv[0]), argv);

    if (key) {
        wasm_runtime_module_free(module_inst, key);
    }

    if (value) {
        wasm_runtime_module_free(module_inst, value);
    }

    return ESP_OK;
}

DEFINE_HTTP_CLIENT_NATIVE_WRAPPER(http_client_init)
{
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    http_client_wrapper_ctx_t *http_client_wrapper = wasm_runtime_malloc(sizeof(http_client_wrapper_ctx_t));
    if (!http_client_wrapper) {
        return ESP_FAIL;
    }

    esp_http_client_config_t config = {
        .url                            = (const char *)args[0],
        .host                           = (const char *)args[1],
        .port                           = (int)args[2],
        .username                       = (const char *)args[3],
        .password                       = (const char *)args[4],
        .auth_type                      = (esp_http_client_auth_type_t)args[5],
        .path                           = (const char *)args[6],
        .query                          = (const char *)args[7],
        .cert_pem                       = (const char *)args[8],
        .cert_len                       = (size_t)args[9],
        .client_cert_pem                = (const char *)args[10],
        .client_cert_len                = (size_t)args[11],
        .client_key_pem                 = (const char *)args[12],
        .client_key_len                 = (size_t)args[13],
        .client_key_password            = (const char *)args[14],
        .client_key_password_len        = (size_t)args[15],
        .user_agent                     = (const char *)args[16],
        .method                         = (esp_http_client_method_t)args[17],
        .timeout_ms                     = (int)args[18],
        .disable_auto_redirect          = (bool)args[19],
        .max_redirection_count          = (int)args[20],
        .max_authorization_retries      = (int)args[21],
        .event_handler                  = wasm_http_client_event_handler,
        .transport_type                 = (esp_http_client_transport_t)args[23],
        .buffer_size                    = (int)args[24],
        .buffer_size_tx                 = (int)args[25],
        .user_data                      = (void *)http_client_wrapper,
        .is_async                       = (bool)args[27],
        .use_global_ca_store            = (bool)args[28],
        .skip_cert_common_name_check    = (bool)args[29],
        .crt_bundle_attach              = args[30] ? esp_crt_bundle_attach : NULL,
        .keep_alive_enable              = (bool)args[31],
        .keep_alive_idle                = (int)args[32],
        .keep_alive_interval            = (int)args[33],
        .keep_alive_count               = (int)args[34],
        .if_name                        = (struct ifreq *)args[35],
    };

    http_client_wrapper->exec_env = exec_env;
    http_client_wrapper->event_handler = args[22];
    http_client_wrapper->user_data = (void *)args[26];
    http_client_wrapper->crt_bundle_attach = args[30];

    config.url = map_string(exec_env, config.url);
    config.host = map_string(exec_env, config.host);
    config.username = map_string(exec_env, config.username);
    config.password = map_string(exec_env, config.password);
    config.path = map_string(exec_env, config.path);
    config.query = map_string(exec_env, config.query);
    config.cert_pem = map_string(exec_env, config.cert_pem);
    config.client_cert_pem = map_string(exec_env, config.client_cert_pem);
    config.client_key_pem = map_string(exec_env, config.client_key_pem);
    config.client_key_password = map_string(exec_env, config.client_key_password);
    config.user_agent = map_string(exec_env, config.user_agent);

    if (!validate_app_addr((uint32_t)config.if_name, sizeof(struct ifreq))) {
        ESP_LOGE(TAG, "Failed to check args by runtime");
        goto fail;
    }

    config.if_name = (struct ifreq *)addr_app_to_native((uint32_t)config.if_name);

    http_client_wrapper->client = esp_http_client_init(&config);
    if (!http_client_wrapper->client) {
        ESP_LOGE(TAG, "Failed to start a HTTP client session");
        goto fail;
    }

    http_client_native_set_return(http_client_wrapper);

    return ESP_OK;

fail:
    wasm_runtime_free(http_client_wrapper);
    return ESP_FAIL;
}

DEFINE_HTTP_CLIENT_NATIVE_WRAPPER(http_client_set_str)
{
    esp_err_t ret = ESP_FAIL;

    http_client_native_get_arg(const int, mode);
    http_client_native_get_arg(const http_client_wrapper_ctx_t *, http_client_wrapper);
    http_client_native_get_arg(const char *, buffer);

    buffer = map_string(exec_env, buffer);

    switch (mode) {
        case HTTP_CLIENT_SET_URL:
            ret = esp_http_client_set_url(http_client_wrapper->client, buffer);
            break;
        case HTTP_CLIENT_SET_POST_FILED: {
                http_client_native_get_arg(const int, len);
                ret = esp_http_client_set_post_field(http_client_wrapper->client, buffer, len);
            }
            break;
        case HTTP_CLIENT_SET_HEADER: {
                http_client_native_get_arg(const char *, value);
                value = map_string(exec_env, value);
                ret = esp_http_client_set_header(http_client_wrapper->client, buffer, value);
            }
            break;
        case HTTP_CLIENT_SET_USERNAME:
            ret = esp_http_client_set_username(http_client_wrapper->client, buffer);
            break;
        case HTTP_CLIENT_SET_PASSWORD:
            ret = esp_http_client_set_password(http_client_wrapper->client, buffer);
            break;
        case HTTP_CLIENT_DELETE_HEADER:
            ret = esp_http_client_delete_header(http_client_wrapper->client, buffer);
            break;
        case HTTP_CLIENT_WRITE_DATA: {
                http_client_native_get_arg(const int, len);
                ret = esp_http_client_write(http_client_wrapper->client, buffer, len);
            }
            break;
        default:
            break;
    }

    return ret;
}

DEFINE_HTTP_CLIENT_NATIVE_WRAPPER(http_client_get_str)
{
    esp_err_t ret = ESP_FAIL;
    char *data = NULL;

    http_client_native_get_arg(const int, mode);
    http_client_native_get_arg(const http_client_wrapper_ctx_t *, http_client_wrapper);
    http_client_native_get_arg(char *, buffer);
    http_client_native_get_arg(const int, len);

    buffer = map_string(exec_env, buffer);

    switch (mode) {
        case HTTP_CLIENT_GET_POST_FILED:
            ret = esp_http_client_get_post_field(http_client_wrapper->client, &data);
            if (data) {
                if (buffer && len == ret) {
                    memcpy(buffer, data, len);
                } else {
                    http_client_native_set_return(ret);
                }
                ret = ESP_OK;
            } else {
                ret = ESP_FAIL;
            }
            break;
        case HTTP_CLIENT_GET_HEADER: {
                http_client_native_get_arg(char *, value);
                value = map_string(exec_env, value);
                ret = esp_http_client_get_header(http_client_wrapper->client, buffer, &data);
                if (data) {
                    if (value && len == strlen(data)) {
                        memcpy(value, data, len);
                    } else {
                        http_client_native_set_return(strlen(data));
                    }
                }
            }
            break;
        case HTTP_CLIENT_GET_USERNAME:
            ret = esp_http_client_get_username(http_client_wrapper->client, &data);
            if (data) {
                if (buffer && len == strlen(data)) {
                    memcpy(buffer, data, len);
                } else {
                    http_client_native_set_return(strlen(data));
                }
            }
            break;
        case HTTP_CLIENT_GET_PASSWORD:
            ret = esp_http_client_get_password(http_client_wrapper->client, &data);
            if (data) {
                if (buffer && len == strlen(data)) {
                    memcpy(buffer, data, len);
                } else {
                    http_client_native_set_return(strlen(data));
                }
            }
            break;
        case HTTP_CLIENT_READ_DATA:
            ret = esp_http_client_read(http_client_wrapper->client, buffer, len);
            break;
        case HTTP_CLIENT_READ_RESP:
            ret = esp_http_client_read_response(http_client_wrapper->client, buffer, len);
            break;
        case HTTP_CLIENT_GET_URL:
            ret = esp_http_client_get_url(http_client_wrapper->client, buffer, len);
            break;
        default:
            break;
    }

    return ret;
}

DEFINE_HTTP_CLIENT_NATIVE_WRAPPER(http_client_set_int)
{
    esp_err_t ret = ESP_FAIL;

    http_client_native_get_arg(const int, mode);
    http_client_native_get_arg(const http_client_wrapper_ctx_t *, http_client_wrapper);
    http_client_native_get_arg(const int, value);

    switch (mode) {
        case HTTP_CLIENT_SET_AUTHTYPE:
            ret = esp_http_client_set_authtype(http_client_wrapper->client, value);
            break;
        case HTTP_CLIENT_SET_METHOD:
            ret = esp_http_client_set_method(http_client_wrapper->client, value);
            break;
        case HTTP_CLIENT_SET_TIMEOUT:
            ret = esp_http_client_set_timeout_ms(http_client_wrapper->client, value);
            break;
        case HTTP_CLIENT_OPEN:
            ret = esp_http_client_open(http_client_wrapper->client, value);
            break;
        default:
            break;
    }

    return ret;
}

DEFINE_HTTP_CLIENT_NATIVE_WRAPPER(http_client_get_int)
{
    int ret = ESP_FAIL;

    http_client_native_get_arg(const int, mode);
    http_client_native_get_arg(const http_client_wrapper_ctx_t *, http_client_wrapper);

    switch (mode) {
        case HTTP_CLIENT_GET_ERRNO:
            ret = esp_http_client_get_errno(http_client_wrapper->client);
            break;
        case HTTP_CLIENT_FETCH_HEADER:
            ret = esp_http_client_fetch_headers(http_client_wrapper->client);
            break;
        case HTTP_CLIENT_IS_CHUNKED:
            ret = esp_http_client_is_chunked_response(http_client_wrapper->client);
            break;
        case HTTP_CLIENT_GET_STATUS_CODE:
            ret = esp_http_client_get_status_code(http_client_wrapper->client);
            break;
        case HTTP_CLIENT_GET_CONTENT_LENGTH:
            ret = esp_http_client_get_content_length(http_client_wrapper->client);
            break;
        case HTTP_CLIENT_GET_TRANSPORT_TYPE:
            ret = esp_http_client_get_transport_type(http_client_wrapper->client);
            break;
        case HTTP_CLIENT_IS_COMPLETE:
            ret = esp_http_client_is_complete_data_received(http_client_wrapper->client);
            break;
        case HTTP_CLIENT_FLUSH_RESPONSE:
            esp_http_client_flush_response(http_client_wrapper->client, &ret);
            break;
        case HTTP_CLIENT_GET_CHUNK_LENGTH:
            esp_http_client_get_chunk_length(http_client_wrapper->client, &ret);
            break;
        default:
            break;
    }

    http_client_native_set_return(ret);

    return (ret == ESP_FAIL) ? ret : ESP_OK;
}

DEFINE_HTTP_CLIENT_NATIVE_WRAPPER(http_client_common)
{
    esp_err_t ret = ESP_FAIL;

    http_client_native_get_arg(const int, mode);
    http_client_native_get_arg(const http_client_wrapper_ctx_t *, http_client_wrapper);

    switch (mode) {
        case HTTP_CLIENT_PERFORM:
            ret = esp_http_client_perform(http_client_wrapper->client);
            break;
        case HTTP_CLIENT_CLOSE:
            ret = esp_http_client_close(http_client_wrapper->client);
            break;
        case HTTP_CLIENT_CLEANUP:
            ret = esp_http_client_cleanup(http_client_wrapper->client);
            wasm_runtime_free(http_client_wrapper);
            break;
        case HTTP_CLIENT_SET_REDIRECTION:
            ret = esp_http_client_set_redirection(http_client_wrapper->client);
            break;
        case HTTP_CLIENT_ADD_AUTH:
            esp_http_client_add_auth(http_client_wrapper->client);
            ret = ESP_OK;
            break;
        default:
            break;
    }

    return ret;
}

static const http_client_func_desc_t http_client_func_desc_table[] = {
    HTTP_CLIENT_NATIVE_WRAPPER(HTTP_CLIENT_INIT,       http_client_init,        36),
    HTTP_CLIENT_NATIVE_WRAPPER(HTTP_CLIENT_SET_STR,    http_client_set_str,     4),
    HTTP_CLIENT_NATIVE_WRAPPER(HTTP_CLIENT_GET_STR,    http_client_get_str,     5),
    HTTP_CLIENT_NATIVE_WRAPPER(HTTP_CLIENT_SET_INT,    http_client_set_int,     3),
    HTTP_CLIENT_NATIVE_WRAPPER(HTTP_CLIENT_GET_INT,    http_client_get_int,     2),
    HTTP_CLIENT_NATIVE_WRAPPER(HTTP_CLIENT_COMMON,     http_client_common,      2),
};

static int wasm_http_client_call_native_func_wrapper(wasm_exec_env_t exec_env, int32_t func_id, uint32_t argc, uint32_t *argv)
{
    int func_num = sizeof(http_client_func_desc_table) / sizeof(http_client_func_desc_table[0]);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    const http_client_func_desc_t *func_desc = &http_client_func_desc_table[func_id];

    if (func_id >= func_num) {
        ESP_LOGE(TAG, "func_id=%d is out of range", func_id);
        return ESP_ERR_INVALID_ARG;
    }

    if (!wasm_runtime_validate_native_addr(module_inst,
                                           argv,
                                           argc * sizeof(uint32_t))) {
        ESP_LOGE(TAG, "argv=%p argc=%d is out of range", argv, argc);
        return ESP_ERR_INVALID_ARG;
    }

    if (func_desc->argc == argc) {
        uint32_t size;
        uint32_t argv_copy_buf[HTTP_CLIENT_ARG_BUF_NUM];
        uint32_t *argv_copy = argv_copy_buf;

        if (argc > HTTP_CLIENT_ARG_BUF_NUM) {
            if (argc > HTTP_CLIENT_ARG_NUM_MAX) {
                ESP_LOGE(TAG, "argc=%d is out of range", argc);
                return ESP_ERR_INVALID_ARG;
            }

            size = sizeof(uint32_t) * argc;
            argv_copy = wasm_runtime_malloc(size);
            if (!argv_copy) {
                ESP_LOGE(TAG, "failed to malloc for argv_copy");
                return ESP_ERR_NO_MEM;
            }

            memset(argv_copy, 0, (uint32_t)size);
        }

        for (int i = 0; i < argc; i++) {
            argv_copy[i] = argv[i];
        }

        ESP_LOGD(TAG, "func_id=%x is to do", func_id);

        int ret = func_desc->func(exec_env, argv_copy, argv);

        if (argv_copy != argv_copy_buf)
            wasm_runtime_free(argv_copy);

        ESP_LOGD(TAG, "func_id=%x is done", func_id);

        return ret;
    } else {
        ESP_LOGE(TAG, "func_id=%d is not found", func_id);
    }

    return ESP_ERR_INVALID_ARG;
}

static NativeSymbol wm_http_client_wrapper_native_symbol[] = {
    REG_NATIVE_FUNC(wasm_http_client_call_native_func, "(ii*)i"),
};

int wm_ext_wasm_native_http_client_export(void)
{
    NativeSymbol *sym = (NativeSymbol *)wm_http_client_wrapper_native_symbol;
    int num = sizeof(wm_http_client_wrapper_native_symbol) / sizeof(wm_http_client_wrapper_native_symbol[0]);

    if (!wasm_native_register_natives("env", sym,  num)) {
        return -1;
    }

    return 0;
}
