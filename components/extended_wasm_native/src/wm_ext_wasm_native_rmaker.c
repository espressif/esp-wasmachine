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

#include "esp_log.h"

#include "bh_platform.h"
#include "wasm_export.h"
#include "wasm_native.h"
#include "wasm_runtime_common.h"

#include "wm_ext_wasm_native_macro.h"
#include "wm_ext_wasm_native_export.h"
#include "wm_ext_wasm_native_vfs_ioctl.h"

#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_devices.h>
#include <esp_rmaker_schedule.h>
#include <esp_rmaker_scenes.h>
#include <esp_rmaker_ota.h>
#include <esp_rmaker_common_events.h>
#include <esp_rmaker_user_mapping.h>

#include <esp_rmaker_internal.h>

/**
 * @brief Function ID for the ramker
 */
#define RMAKER_NODE_INIT                        0
#define RMAKER_NODE_DEINIT                      1
#define RMAKER_NODE_SET_STR                     2
#define RMAKER_NODE_GET_STR                     3
#define RMAKER_NODE_SET_DEVICE                  4
#define RMAKER_NODE_GET_DEVICE                  5
#define RMAKER_NODE_GET_NODE                    6
#define RMAKER_NODE_GET_INFO                    7
#define RMAKER_DEVICE_CREATE                    8
#define RMAKER_DEVICE_DELETE                    9
#define RMAKER_DEVICE_SET_STR                   10
#define RMAKER_DEVICE_GET_STR                   11
#define RMAKER_DEVICE_SET_PARAM                 12
#define RMAKER_DEVICE_GET_PARAM                 13
#define RMAKER_DEVICE_ADD_CALLBACK              14
#define RMAKER_DEVICE_CB_SRC_TO_STR             15
#define RMAKER_SERVICE_CREATE                   16
#define RMAKER_SERVICE_SYSTEM_ENABLE            17
#define RMAKER_PARAM_CREATE                     18
#define RMAKER_PARAM_DELETE                     19
#define RMAKER_PARAM_SET_STR                    20
#define RMAKER_PARAM_GET_STR                    21
#define RMAKER_PARAM_SET_VAL                    22
#define RMAKER_PARAM_GET_VAL                    23
#define RMAKER_PARAM_ADD_BOUNDS                 24
#define RMAKER_COMMON_RAISE_ALERT               25

/**
 * @brief Node option ID for the ramker
 */
#define RMAKER_NODE_ADD_ATTRIBUTE               0
#define RMAKER_NODE_ADD_FW_VERSION              1
#define RMAKER_NODE_ADD_MODEL                   2
#define RMAKER_NODE_ADD_SUBTYPE                 3
#define RMAKER_NODE_GET_NODE_ID                 4
#define RMAKER_NODE_ADD_DEVICE                  5
#define RMAKER_NODE_REMOVE_DEVICE               6
#define RMAKER_NODE_GET_DEVICE_BY_NAME          7

/**
 * @brief Device option ID for the ramker
 */
#define RMAKER_DEVICE_ADD_ATTRIBUTE             0
#define RMAKER_DEVICE_ADD_SUBTYPE               1
#define RMAKER_DEVICE_ADD_MODEL                 2
#define RMAKER_DEVICE_GET_NAME                  3
#define RMAKER_DEVICE_GET_TYPE                  4
#define RMAKER_DEVICE_ADD_PARAM                 5
#define RMAKER_DEVICE_ASSIGN_PRIMARY_PARAM      6
#define RMAKER_DEVICE_GET_PARAM_BY_TYPE         7
#define RMAKER_DEVICE_GET_PARAM_BY_NAME         8

/**
 * @brief Param option ID for the ramker
 */
#define RMAKER_PARAM_ADD_UI_TYPE                0
#define RMAKER_PARAM_ADD_ARRAY_MAX_COUNT        1
#define RMAKER_PARAM_GET_NAME                   2
#define RMAKER_PARAM_GET_TYPE                   3
#define RMAKER_PARAM_UPDATE                     4
#define RMAKER_PARAM_UPDATE_AND_REPORT          5
#define RMAKER_PARAM_UPDATE_AND_NOTIFY          6

/**
 * @brief Common option ID for the ramker
 */
#define RMAKER_COMMON_LOCAL_CTRL_START          0
#define RMAKER_COMMON_NODE_DETAILS              1
#define RMAKER_COMMON_OTA_ENABLE                2
#define RMAKER_COMMON_TIMEZONE_ENABLE           3
#define RMAKER_COMMON_SCHEDULE_ENABLE           4
#define RMAKER_COMMON_SCENES_ENABLE             5
#define RMAKER_COMMON_START                     6
#define RMAKER_COMMON_STOP                      7

#define RMAKER_ARG_BUF_NUM        16
#define RMAKER_ARG_NUM_MAX        64

#define rmaker_native_return_type(type)       type *rmaker_ret = (type *)(args_ret)
#define rmaker_native_get_arg(type, name)     type name = *((type *)(args++))

#define DEFINE_RMAKER_NATIVE_WRAPPER(func_name)                   \
    static int func_name ## _wrapper(wasm_exec_env_t exec_env, \
                                      uint32_t *args,           \
                                      uint32_t *args_ret)

#define RMAKER_NATIVE_WRAPPER(id, func_name, argc)                \
    [id] = { func_name ## _wrapper, argc }

typedef int (*rmaker_func_t)(wasm_exec_env_t exec_env, uint32_t *args, uint32_t *args_ret);

typedef struct rmaker_func_desc {
    rmaker_func_t   func;
    uint32_t        argc;
} rmaker_func_desc_t;

typedef struct __rmaker_wrapper_ctx_t {
    wasm_exec_env_t exec_env;
    uint32_t        scheme_cb;
    uint32_t        write_cb;
    uint32_t        read_cb;
    void            *priv_data;
} rmaker_wrapper_ctx_t __attribute__((aligned(4)));

static const char *TAG = "rmaker_wrapper";

#define CONFIG_RMAKER_HEAP_SIZE 16384

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

    ptr = (char *)addr_app_to_native((uint32_t)str);
    if (!ptr) {
        ESP_LOGE(TAG, "failed to map app_addr=%p", str);
    } else {
        str = ptr;
    }

    return (char *)str; 
}

static bool rmaker_run_wasm(wasm_exec_env_t env, uint32_t cb, int argc, uint32_t *argv)
{
    wasm_exec_env_t exec_env = wasm_runtime_create_exec_env(get_module_inst(env), CONFIG_RMAKER_HEAP_SIZE);
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

static esp_err_t wasm_rmaker_write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
        const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    rmaker_wrapper_ctx_t *rmaker_wrapper_ctx = (rmaker_wrapper_ctx_t *)priv_data;

    uint32_t argv[6];

    argv[0] = (uint32_t)device;
    argv[1] = (uint32_t)param;
    argv[2] = (uint32_t)&val;
    argv[3] = rmaker_wrapper_ctx->write_cb;
    argv[4] = (uint32_t)rmaker_wrapper_ctx->priv_data;
    argv[5] = (uint32_t)ctx ? ctx->src : ESP_RMAKER_REQ_SRC_MAX;

    return rmaker_run_wasm(rmaker_wrapper_ctx->exec_env, rmaker_wrapper_ctx->scheme_cb, 6, argv) ? (esp_err_t)argv[0] : ESP_OK;
}

esp_err_t wasm_rmaker_read_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
        void *priv_data, esp_rmaker_read_ctx_t *ctx)
{
    rmaker_wrapper_ctx_t *rmaker_wrapper_ctx = (rmaker_wrapper_ctx_t *)priv_data;

    if (rmaker_wrapper_ctx->read_cb) {
        uint32_t argv[4];

        argv[0] = (uint32_t)device;
        argv[1] = (uint32_t)param;
        argv[2] = (uint32_t)rmaker_wrapper_ctx->priv_data;
        argv[3] = (uint32_t)ctx;

        return rmaker_run_wasm(rmaker_wrapper_ctx->exec_env, rmaker_wrapper_ctx->read_cb, 4, argv) ? (esp_err_t)argv[0] : ESP_OK;
    }

    return ESP_OK;
}

/* Event handler for catching RainMaker events */
static void wasm_rmaker_event_handler(void* arg, esp_event_base_t event_base,
                          int event_id, void* event_data)
{
    if (event_base == RMAKER_EVENT) {
        switch (event_id) {
            case RMAKER_EVENT_INIT_DONE:
                ESP_LOGI(TAG, "RainMaker Initialised.");
                break;
            case RMAKER_EVENT_CLAIM_STARTED:
                ESP_LOGI(TAG, "RainMaker Claim Started.");
                break;
            case RMAKER_EVENT_CLAIM_SUCCESSFUL:
                ESP_LOGI(TAG, "RainMaker Claim Successful.");
                break;
            case RMAKER_EVENT_CLAIM_FAILED:
                ESP_LOGI(TAG, "RainMaker Claim Failed.");
                break;
            default:
                ESP_LOGW(TAG, "Unhandled RainMaker Event: %d", event_id);
        }
    } else if (event_base == RMAKER_COMMON_EVENT) {
        switch (event_id) {
            case RMAKER_EVENT_REBOOT:
                ESP_LOGI(TAG, "Rebooting in %d seconds.", *((uint8_t *)event_data));
                break;
            case RMAKER_EVENT_WIFI_RESET:
                ESP_LOGI(TAG, "Wi-Fi credentials reset.");
                break;
            case RMAKER_EVENT_FACTORY_RESET:
                ESP_LOGI(TAG, "Node reset to factory defaults.");
                break;
            default:
                ESP_LOGW(TAG, "Unhandled RainMaker Common Event: %d", event_id);
        }
    } else {
        ESP_LOGW(TAG, "Invalid event received!");
    }
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_node_init)
{
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    rmaker_native_get_arg(const esp_rmaker_config_t *, config);
    rmaker_native_get_arg(const char *, name);
    rmaker_native_get_arg(const char *, type);

    if (!validate_app_addr((uint32_t)config, sizeof(esp_rmaker_config_t))) {
        return ESP_FAIL;
    }

    config = (const esp_rmaker_config_t *)addr_app_to_native((uint32_t)config);
    name = map_string(exec_env, name);
    type = map_string(exec_env, type);

    return (int)esp_rmaker_node_init(config, name, type);
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_node_deinit)
{
    rmaker_native_get_arg(const esp_rmaker_node_t *, node);

    return esp_rmaker_node_deinit(node);
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_node_set_string)
{
    esp_err_t ret = ESP_FAIL;

    rmaker_native_get_arg(uint32_t, mode);
    rmaker_native_get_arg(const esp_rmaker_node_t *, node);
    rmaker_native_get_arg(const char *, key);
    rmaker_native_get_arg(const char *, value);

    key = map_string(exec_env, key);
    switch (mode) {
        case RMAKER_NODE_ADD_ATTRIBUTE:
            value = map_string(exec_env, value);
            ret = esp_rmaker_node_add_attribute(node, key, value);
            break;
        case RMAKER_NODE_ADD_FW_VERSION:
            ret = esp_rmaker_node_add_fw_version(node, key);
            break;
        case RMAKER_NODE_ADD_MODEL:
            ret = esp_rmaker_node_add_model(node, key);
            break;
        case RMAKER_NODE_ADD_SUBTYPE:
            ret = esp_rmaker_node_add_subtype(node, key);
            break;
        default:
            break;
    }

    return ret;
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_node_get_string)
{
    esp_err_t ret = ESP_FAIL;
    char *res = NULL;

    rmaker_native_get_arg(uint32_t, mode);

    switch (mode) {
        case RMAKER_NODE_GET_NODE_ID:
            res = esp_rmaker_get_node_id();
            if (res) {
                rmaker_native_get_arg(char *, output);
                rmaker_native_get_arg(uint8_t, len);

                output = map_string(exec_env, (const char *)output);

                memcpy(output, res, len > strlen(output) ? strlen(output) : len);

                ret = ESP_OK;
            }
            break;
        default:
            break;
    }

    return ret;
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_node_set_device)
{
    rmaker_native_get_arg(bool, mode);
    rmaker_native_get_arg(const esp_rmaker_node_t *, node);
    rmaker_native_get_arg(const esp_rmaker_device_t *, device);

    return mode ? esp_rmaker_node_add_device(node, device) : esp_rmaker_node_remove_device(node, device);
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_node_get_device)
{
    esp_rmaker_device_t *device = NULL;

    rmaker_native_get_arg(uint32_t, mode);
    rmaker_native_get_arg(const esp_rmaker_node_t *, node);
    rmaker_native_get_arg(const char *, input);

    input = map_string(exec_env, input);
    
    switch (mode) {
        case RMAKER_NODE_GET_DEVICE_BY_NAME:
            device = esp_rmaker_node_get_device_by_name(node, input);
            break;
        default:
            break;
    }

    return (int)device;
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_node_get_node)
{
    return (int)esp_rmaker_get_node();
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_node_get_info)
{
    rmaker_native_get_arg(const esp_rmaker_node_t *, node);

    esp_rmaker_node_info_t *node_info = esp_rmaker_node_get_info(node);
    if (node_info) {
        rmaker_native_get_arg(char *, name);
        rmaker_native_get_arg(char *, type);
        rmaker_native_get_arg(char *, fw_version);
        rmaker_native_get_arg(char *, model);
        rmaker_native_get_arg(char *, subtype);

        name = map_string(exec_env, name);
        type = map_string(exec_env, type);
        fw_version = map_string(exec_env, fw_version);
        model = map_string(exec_env, model);
        name = map_string(exec_env, name);
        memcpy(name, node_info->name, strlen(node_info->name));
        memcpy(type, node_info->type, strlen(node_info->type));
        memcpy(fw_version, node_info->fw_version, strlen(node_info->fw_version));
        memcpy(model, node_info->model, strlen(node_info->model));
        memcpy(subtype, node_info->subtype, strlen(node_info->subtype));
    }

    return node_info ? ESP_OK : ESP_FAIL;
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_device_create)
{
    rmaker_wrapper_ctx_t *rmaker_wrapper = wasm_runtime_malloc(sizeof(rmaker_wrapper_ctx_t));
    if (!rmaker_wrapper) {
        return ESP_FAIL;
    }

    rmaker_native_get_arg(const char *, dev_name);
    rmaker_native_get_arg(const char *, type);
    rmaker_native_get_arg(void *, priv_data);

    dev_name = map_string(exec_env, dev_name);
    type = map_string(exec_env, type);
    
    rmaker_wrapper->priv_data = priv_data;

    return (int)esp_rmaker_device_create(dev_name, type, rmaker_wrapper);
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_device_delete)
{
    rmaker_native_get_arg(const esp_rmaker_device_t *, device);

    const _esp_rmaker_device_t *rmaker_device = (const _esp_rmaker_device_t *)device;
    const _esp_rmaker_param_t *param = rmaker_device->params;
    wasm_runtime_free(rmaker_device->priv_data);
    while (param) {
        if (param->valid_str_list) {
            for (int i = 0; i < param->valid_str_list->str_list_cnt; i ++) {
                if (!param->valid_str_list->str_list[i]) {
                    break;
                }
                free((void *)param->valid_str_list->str_list[i]);
            }
        }
        param = param->next;
    }

    return esp_rmaker_device_delete(device);
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_device_set_string)
{
    esp_err_t ret = ESP_FAIL;

    rmaker_native_get_arg(uint32_t, mode);
    rmaker_native_get_arg(const esp_rmaker_device_t *, device);
    rmaker_native_get_arg(const char *, input);
    rmaker_native_get_arg(const char *, val);

    input = map_string(exec_env, input);
    switch (mode) {
        case RMAKER_DEVICE_ADD_ATTRIBUTE:
            val = map_string(exec_env, val);
            ret = esp_rmaker_device_add_attribute(device, input, val);
            break;
        case RMAKER_DEVICE_ADD_SUBTYPE:
            ret = esp_rmaker_device_add_subtype(device, input);
            break;
        case RMAKER_DEVICE_ADD_MODEL:
            ret = esp_rmaker_device_add_model(device, input);
            break;
        default:
            break;
    }

    return ret;
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_device_get_string)
{
    const char *res = NULL;

    rmaker_native_get_arg(uint32_t, mode);
    rmaker_native_get_arg(const esp_rmaker_device_t *, device);
    rmaker_native_get_arg(char *, output);
    rmaker_native_get_arg(uint32_t, len);

    switch (mode) {
        case RMAKER_DEVICE_GET_NAME:
            res = esp_rmaker_device_get_name(device);
            break;
        case RMAKER_DEVICE_GET_TYPE:
            res = esp_rmaker_device_get_type(device);
            break;
        default:
            break;
    }

    if (res) {
        output = map_string(exec_env, output);
        memcpy(output, res, len > strlen(res) ? strlen(res) : len);
    }

    return res ? ESP_OK : ESP_FAIL;
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_device_set_param)
{
    esp_err_t ret = ESP_FAIL;

    rmaker_native_get_arg(uint32_t, mode);
    rmaker_native_get_arg(const esp_rmaker_device_t *, device);
    rmaker_native_get_arg(const esp_rmaker_param_t *, param);

    switch (mode) {
        case RMAKER_DEVICE_ASSIGN_PRIMARY_PARAM:
            ret = esp_rmaker_device_assign_primary_param(device, param);
            break;
        case RMAKER_DEVICE_ADD_PARAM:
            ret = esp_rmaker_device_add_param(device, param);
            break;
        default:
            break;
    }

    return ret;
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_device_get_param)
{
    esp_rmaker_param_t *param = NULL;

    rmaker_native_get_arg(uint32_t, mode);
    rmaker_native_get_arg(const esp_rmaker_device_t *, device);
    rmaker_native_get_arg(const char *, input);

    input = map_string(exec_env, input);

    switch (mode) {
        case RMAKER_DEVICE_GET_PARAM_BY_NAME:
            param = esp_rmaker_device_get_param_by_name(device, input);
            break;
        case RMAKER_DEVICE_GET_PARAM_BY_TYPE:
            param = esp_rmaker_device_get_param_by_type(device, input);
            break;
        default:
            break;
    }

    return (int)param;
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_device_add_cb)
{
    rmaker_native_get_arg(const esp_rmaker_device_t *, device);
    rmaker_native_get_arg(uint32_t, scheme_cb);
    rmaker_native_get_arg(uint32_t, write_cb);
    rmaker_native_get_arg(uint32_t, read_cb);

    const _esp_rmaker_device_t *rmaker_device = (const _esp_rmaker_device_t *)device;
    rmaker_wrapper_ctx_t *rmaker_wrapper = rmaker_device->priv_data;

    rmaker_wrapper->exec_env = exec_env;
    rmaker_wrapper->scheme_cb = scheme_cb;
    rmaker_wrapper->write_cb = write_cb;
    rmaker_wrapper->read_cb = read_cb;

    return esp_rmaker_device_add_cb(device, wasm_rmaker_write_cb, wasm_rmaker_read_cb);
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_device_cb_src_to_str)
{
    rmaker_native_get_arg(esp_rmaker_req_src_t, src);
    rmaker_native_get_arg(char *, output);
    rmaker_native_get_arg(uint32_t, len);

    const char *src_to_str = esp_rmaker_device_cb_src_to_str(src);
    output = map_string(exec_env, output);
    if (src_to_str) {
        memcpy(output, src_to_str, len > strlen(src_to_str) ? strlen(src_to_str) : len);
    }

    return src_to_str ? ESP_OK : ESP_FAIL;
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_service_create)
{
    rmaker_wrapper_ctx_t *rmaker_wrapper = wasm_runtime_malloc(sizeof(rmaker_wrapper_ctx_t));
    if (!rmaker_wrapper) {
        return ESP_FAIL;
    }

    rmaker_native_get_arg(const char *, serv_name);
    rmaker_native_get_arg(const char *, type);
    rmaker_native_get_arg(void *, priv_data);

    serv_name = map_string(exec_env, serv_name);
    type = map_string(exec_env, type);

    rmaker_wrapper->priv_data = priv_data;

    return (int)esp_rmaker_service_create(serv_name, type, rmaker_wrapper);
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_system_service_enable)
{
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    rmaker_native_get_arg(esp_rmaker_system_serv_config_t *, config);
    if (!validate_app_addr((uint32_t)config, sizeof(esp_rmaker_system_serv_config_t))) {
        return ESP_FAIL;
    }

    config = (esp_rmaker_system_serv_config_t *)addr_app_to_native((uint32_t)config);

    return esp_rmaker_system_service_enable(config);
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_param_create)
{
    esp_rmaker_param_val_t val;

    rmaker_native_get_arg(const char *, param_name);
    rmaker_native_get_arg(const char *, type);
    rmaker_native_get_arg(esp_rmaker_val_type_t, val_type);
    rmaker_native_get_arg(uint32_t, param_val);
    rmaker_native_get_arg(uint8_t, properties);

    param_name = map_string(exec_env, param_name);
    type = map_string(exec_env, type);
    
    val.type = val_type;
    switch (val_type) {
        case RMAKER_VAL_TYPE_BOOLEAN:
            val.val.b = (bool)param_val;
            break;
        case RMAKER_VAL_TYPE_INTEGER:
            val.val.i = (int)param_val;
            break;
        case RMAKER_VAL_TYPE_FLOAT:
            val.val.f = (float)param_val;
            break;
        case RMAKER_VAL_TYPE_STRING:
        case RMAKER_VAL_TYPE_OBJECT:
        case RMAKER_VAL_TYPE_ARRAY:
            val.val.s = map_string(exec_env, (const char *)param_val);
            break;
        default:
            break;
    }

    return (int)esp_rmaker_param_create(param_name, type, val, properties);
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_param_delete)
{
    rmaker_native_get_arg(const esp_rmaker_param_t *, param);

    return esp_rmaker_param_delete(param);
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_param_set_string)
{
    esp_err_t ret = ESP_FAIL;

    rmaker_native_get_arg(uint32_t, mode);
    rmaker_native_get_arg(const esp_rmaker_param_t *, param);
    rmaker_native_get_arg(uint32_t, input);

    switch (mode) {
        case RMAKER_PARAM_ADD_ARRAY_MAX_COUNT:
            ret = esp_rmaker_param_add_array_max_count(param, input);
            break;
        case RMAKER_PARAM_ADD_UI_TYPE:
            input = (uint32_t)map_string(exec_env, (const char *)input);
            ret = esp_rmaker_param_add_ui_type(param, (const char *)input);
            break;
        default:
            break;
    }

    return ret;
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_param_get_string)
{
    const char *res = NULL;

    rmaker_native_get_arg(uint32_t, mode);
    rmaker_native_get_arg(const esp_rmaker_param_t *, param);

    switch (mode) {
        case RMAKER_PARAM_GET_NAME:
            res = esp_rmaker_param_get_name(param);
            break;
        case RMAKER_PARAM_GET_TYPE:
            res = esp_rmaker_param_get_type(param);
            break;
        default:
            break;
    }

    if (res) {
        rmaker_native_get_arg(char *, output);
        rmaker_native_get_arg(uint32_t, len);
        output = map_string(exec_env, output);
        memcpy(output, res, len > strlen(res) ? strlen(res) : len);
    }

    return res ? ESP_OK : ESP_FAIL;
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_param_set_val)
{
    esp_rmaker_param_val_t val;
    int ret = ESP_FAIL;

    rmaker_native_get_arg(uint32_t, mode);
    rmaker_native_get_arg(const esp_rmaker_param_t *, param);
    rmaker_native_get_arg(esp_rmaker_val_type_t, val_type);
    rmaker_native_get_arg(uint32_t, param_val);
    
    val.type = val_type;
    switch (val_type) {
        case RMAKER_VAL_TYPE_BOOLEAN:
            val.val.b = (bool)param_val;
            break;
        case RMAKER_VAL_TYPE_INTEGER:
            val.val.i = (int)param_val;
            break;
        case RMAKER_VAL_TYPE_FLOAT:
            val.val.f = (float)param_val;
            break;
        case RMAKER_VAL_TYPE_STRING:
        case RMAKER_VAL_TYPE_OBJECT:
        case RMAKER_VAL_TYPE_ARRAY:
            val.val.s = map_string(exec_env, (const char *)param_val);
            break;
        default:
            break;
    }

    switch (mode) {
        case RMAKER_PARAM_UPDATE:
            ret = esp_rmaker_param_update(param, val);
            break;
        case RMAKER_PARAM_UPDATE_AND_REPORT:
            ret = esp_rmaker_param_update_and_report(param, val);
            break;
        case RMAKER_PARAM_UPDATE_AND_NOTIFY:
            ret = esp_rmaker_param_update_and_notify(param, val);
            break;
        default:
            break;
    }

    return ret;
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_param_get_val)
{
    const esp_rmaker_param_val_t *rmaker_val = NULL;

    rmaker_native_get_arg(bool, mode);
    rmaker_native_get_arg(uint32_t, param);

    if (mode) {
        rmaker_val = esp_rmaker_param_get_val((esp_rmaker_param_t *)param);
    } else {
        rmaker_val = (const esp_rmaker_param_val_t *)param;
    }

    if (rmaker_val) {
        wasm_module_inst_t module_inst = get_module_inst(exec_env);

        rmaker_native_get_arg(esp_rmaker_val_type_t *, val_type);
        rmaker_native_get_arg(char *, val);
        rmaker_native_get_arg(uint32_t, len);

        val = map_string(exec_env, val);
        if (!validate_app_addr((uint32_t)val_type, sizeof(esp_rmaker_val_type_t))) {
            return ESP_FAIL;
        }

        val_type = (esp_rmaker_val_type_t *)addr_app_to_native((uint32_t)val_type);
        memcpy(val_type, &rmaker_val->type, sizeof(esp_rmaker_val_type_t));
        switch (rmaker_val->type) {
            case RMAKER_VAL_TYPE_BOOLEAN:
                memcpy(val, &rmaker_val->val.b, sizeof(rmaker_val->val.b));
                break;
            case RMAKER_VAL_TYPE_INTEGER:
                memcpy(val, &rmaker_val->val.i, sizeof(rmaker_val->val.i));
                break;
            case RMAKER_VAL_TYPE_FLOAT:
                memcpy(val, &rmaker_val->val.f, sizeof(rmaker_val->val.f));
                break;
            case RMAKER_VAL_TYPE_STRING:
            case RMAKER_VAL_TYPE_OBJECT:
            case RMAKER_VAL_TYPE_ARRAY:
                memcpy(val, rmaker_val->val.s, len > strlen(rmaker_val->val.s) ? strlen(rmaker_val->val.s) : len);
                break;
            default:
                break;
        }
    }

    return rmaker_val ? ESP_OK : ESP_FAIL;
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_param_add_bounds)
{
    esp_rmaker_param_val_t min, max, step;

    rmaker_native_get_arg(const esp_rmaker_param_t *, param);
    rmaker_native_get_arg(esp_rmaker_val_type_t, min_type);
    rmaker_native_get_arg(uint32_t, min_val);
    rmaker_native_get_arg(esp_rmaker_val_type_t, max_type);
    rmaker_native_get_arg(uint32_t, max_val);
    rmaker_native_get_arg(esp_rmaker_val_type_t, step_type);
    rmaker_native_get_arg(uint32_t, step_val);

    min.type = min_type;
    switch (min_type) {
        case RMAKER_VAL_TYPE_BOOLEAN:
            min.val.b = (bool)min_val;
            break;
        case RMAKER_VAL_TYPE_INTEGER:
            min.val.i = (int)min_val;
            break;
        case RMAKER_VAL_TYPE_FLOAT:
            min.val.f = (float)min_val;
            break;
        case RMAKER_VAL_TYPE_STRING:
        case RMAKER_VAL_TYPE_OBJECT:
        case RMAKER_VAL_TYPE_ARRAY:
            min.val.s = (char *)map_string(exec_env, (const char *)min_val);
            break;
        default:
            break;
    }

    max.type = max_type;
    switch (max_type) {
        case RMAKER_VAL_TYPE_BOOLEAN:
            max.val.b = (bool)max_val;
            break;
        case RMAKER_VAL_TYPE_INTEGER:
            max.val.i = (int)max_val;
            break;
        case RMAKER_VAL_TYPE_FLOAT:
            max.val.f = (float)max_val;
            break;
        case RMAKER_VAL_TYPE_STRING:
        case RMAKER_VAL_TYPE_OBJECT:
        case RMAKER_VAL_TYPE_ARRAY:
            max.val.s = (char *)map_string(exec_env, (const char *)max_val);
            break;
        default:
            break;
    }

    step.type = step_type;
    switch (step_type) {
        case RMAKER_VAL_TYPE_BOOLEAN:
            step.val.b = (bool)step_val;
            break;
        case RMAKER_VAL_TYPE_INTEGER:
            step.val.i = (int)step_val;
            break;
        case RMAKER_VAL_TYPE_FLOAT:
            step.val.f = (float)step_val;
            break;
        case RMAKER_VAL_TYPE_STRING:
        case RMAKER_VAL_TYPE_OBJECT:
        case RMAKER_VAL_TYPE_ARRAY:
            step.val.s = (char *)map_string(exec_env, (const char *)step_val);
            break;
        default:
            break;
    }

    return esp_rmaker_param_add_bounds(param, min, max, step);
}

DEFINE_RMAKER_NATIVE_WRAPPER(rmaker_raise_alert)
{
    rmaker_native_get_arg(const char *, alert_str);
    
    alert_str = map_string(exec_env, alert_str);

    return esp_rmaker_raise_alert(alert_str);
}

static const rmaker_func_desc_t rmaker_func_desc_table[] = {
    RMAKER_NATIVE_WRAPPER(RMAKER_NODE_INIT,             rmaker_node_init,               3),
    RMAKER_NATIVE_WRAPPER(RMAKER_NODE_DEINIT,           rmaker_node_deinit,             1),
    RMAKER_NATIVE_WRAPPER(RMAKER_NODE_SET_STR,          rmaker_node_set_string,         4),
    RMAKER_NATIVE_WRAPPER(RMAKER_NODE_GET_STR,          rmaker_node_get_string,         3),
    RMAKER_NATIVE_WRAPPER(RMAKER_NODE_SET_DEVICE,       rmaker_node_set_device,         3),
    RMAKER_NATIVE_WRAPPER(RMAKER_NODE_GET_DEVICE,       rmaker_node_get_device,         3),
    RMAKER_NATIVE_WRAPPER(RMAKER_NODE_GET_NODE,         rmaker_node_get_node,           1),
    RMAKER_NATIVE_WRAPPER(RMAKER_NODE_GET_INFO,         rmaker_node_get_info,           6),
    RMAKER_NATIVE_WRAPPER(RMAKER_DEVICE_CREATE,         rmaker_device_create,           3),
    RMAKER_NATIVE_WRAPPER(RMAKER_DEVICE_DELETE,         rmaker_device_delete,           1),
    RMAKER_NATIVE_WRAPPER(RMAKER_DEVICE_SET_STR,        rmaker_device_set_string,       4),
    RMAKER_NATIVE_WRAPPER(RMAKER_DEVICE_GET_STR,        rmaker_device_get_string,       4),
    RMAKER_NATIVE_WRAPPER(RMAKER_DEVICE_SET_PARAM,      rmaker_device_set_param,        3),
    RMAKER_NATIVE_WRAPPER(RMAKER_DEVICE_GET_PARAM,      rmaker_device_get_param,        3),
    RMAKER_NATIVE_WRAPPER(RMAKER_DEVICE_ADD_CALLBACK,   rmaker_device_add_cb,           4),
    RMAKER_NATIVE_WRAPPER(RMAKER_DEVICE_CB_SRC_TO_STR,  rmaker_device_cb_src_to_str,    3),
    RMAKER_NATIVE_WRAPPER(RMAKER_SERVICE_CREATE,        rmaker_service_create,          3),
    RMAKER_NATIVE_WRAPPER(RMAKER_SERVICE_SYSTEM_ENABLE, rmaker_system_service_enable,   1),
    RMAKER_NATIVE_WRAPPER(RMAKER_PARAM_CREATE,          rmaker_param_create,            5),
    RMAKER_NATIVE_WRAPPER(RMAKER_PARAM_DELETE,          rmaker_param_delete,            1),
    RMAKER_NATIVE_WRAPPER(RMAKER_PARAM_SET_STR,         rmaker_param_set_string,        3),
    RMAKER_NATIVE_WRAPPER(RMAKER_PARAM_GET_STR,         rmaker_param_get_string,        4),
    RMAKER_NATIVE_WRAPPER(RMAKER_PARAM_SET_VAL,         rmaker_param_set_val,           4),
    RMAKER_NATIVE_WRAPPER(RMAKER_PARAM_GET_VAL,         rmaker_param_get_val,           5),
    RMAKER_NATIVE_WRAPPER(RMAKER_PARAM_ADD_BOUNDS,      rmaker_param_add_bounds,        7),
    RMAKER_NATIVE_WRAPPER(RMAKER_COMMON_RAISE_ALERT,    rmaker_raise_alert,             1)
};

static int wasm_rmaker_run_wrapper(wasm_exec_env_t exec_env, int mode)
{
    esp_err_t ret = ESP_OK;
    switch (mode) {
        case RMAKER_COMMON_LOCAL_CTRL_START:
#ifdef CONFIG_ESP_RMAKER_LOCAL_CTRL_ENABLE
            ret = esp_rmaker_local_ctrl_service_started();
#endif
            break;
        case RMAKER_COMMON_NODE_DETAILS:
            ret = esp_rmaker_report_node_details();
            break;
        case RMAKER_COMMON_OTA_ENABLE:
            ret = esp_rmaker_ota_enable_default();
            break;
        case RMAKER_COMMON_TIMEZONE_ENABLE:
            ret = esp_rmaker_timezone_service_enable();
            break;
        case RMAKER_COMMON_SCHEDULE_ENABLE:
            ret = esp_rmaker_schedule_enable();
            break;
        case RMAKER_COMMON_SCENES_ENABLE:
            ret = esp_rmaker_scenes_enable();
            break;
        case RMAKER_COMMON_START:
            ESP_ERROR_CHECK(esp_event_handler_register(RMAKER_EVENT, ESP_EVENT_ANY_ID, wasm_rmaker_event_handler, NULL));
            ESP_ERROR_CHECK(esp_event_handler_register(RMAKER_COMMON_EVENT, ESP_EVENT_ANY_ID, wasm_rmaker_event_handler, NULL));

            ret = esp_rmaker_start();
            break;
        case RMAKER_COMMON_STOP:
            ESP_ERROR_CHECK(esp_event_handler_unregister(RMAKER_EVENT, ESP_EVENT_ANY_ID, wasm_rmaker_event_handler));
            ESP_ERROR_CHECK(esp_event_handler_unregister(RMAKER_COMMON_EVENT, ESP_EVENT_ANY_ID, wasm_rmaker_event_handler));

            ret = esp_rmaker_stop();
            break;
        default:
            break;
    }

    return ret;
}

static int wasm_rmaker_param_add_valid_str_list_wrapper(wasm_exec_env_t exec_env, uint32_t param, const char *strs[], uint8_t count)
{
    const char *str_list[count];

    for (int i = 0; i < count; i ++) {
        strs[i] = map_string(exec_env, strs[i]);
        str_list[i] = strdup(strs[i]);
        if (!str_list[i]) {
            return ESP_ERR_NO_MEM;
        }
    }

    return esp_rmaker_param_add_valid_str_list((const esp_rmaker_param_t *)param, str_list, count);
}

static int wasm_rmaker_call_native_func_wrapper(wasm_exec_env_t exec_env, int32_t func_id, uint32_t argc, uint32_t *argv)
{
    int func_num = sizeof(rmaker_func_desc_table) / sizeof(rmaker_func_desc_table[0]);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    const rmaker_func_desc_t *func_desc = &rmaker_func_desc_table[func_id];

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
        uint32_t argv_copy_buf[RMAKER_ARG_BUF_NUM];
        uint32_t *argv_copy = argv_copy_buf;

        if (argc > RMAKER_ARG_BUF_NUM) {
            if (argc > RMAKER_ARG_NUM_MAX) {
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

static NativeSymbol wm_rmaker_wrapper_native_symbol[] = {
    REG_NATIVE_FUNC(wasm_rmaker_run, "(i)i"),
    REG_NATIVE_FUNC(wasm_rmaker_param_add_valid_str_list, "(i$i)i"),
    REG_NATIVE_FUNC(wasm_rmaker_call_native_func, "(ii*)i"),
};

int wm_ext_wasm_native_rmaker_export(void)
{
    NativeSymbol *sym = (NativeSymbol *)wm_rmaker_wrapper_native_symbol;
    int num = sizeof(wm_rmaker_wrapper_native_symbol) / sizeof(wm_rmaker_wrapper_native_symbol[0]);

    if (!wasm_native_register_natives("env", sym,  num)) {
        return -1;
    }

    return 0;
}