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
#include <inttypes.h>

#include "bh_common.h"
#include "bh_log.h"
#include "bh_platform.h"
#include "app_manager_export.h"
#include "module_wasm_app.h"
#include "bi-inc/attr_container.h"
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
#include <esp_wifi.h>
#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>
#include <wifi_provisioning/scheme_softap.h>
#include <wifi_provisioning/scheme_console.h>

static const char *TAG = "wifi_provisioning_wrapper";

/**
 * @brief   Enum for specifying the provisioning scheme to be
 *          followed by the manager
 */
typedef enum {
    WIFI_PROV_CONSOLE,
    WIFI_PROV_SOFTAP,
    WIFI_PROV_BTDM,
    WIFI_PROV_BT,
    WIFI_PROV_BLE,
} wifi_prov_method_t;

typedef struct {
    char                    *ep_name;
    protocomm_req_handler_t handler;
    void                    *user_ctx;
} wifi_prov_endpoint_handler_t;

typedef struct __wifi_prov_wrapper_ctx_t{
    uint32_t                        func_id;
    wasm_exec_env_t                 exec_env;
    wifi_prov_event_handler_t       app_event_handler;
    wifi_prov_endpoint_handler_t    endpoint_handler;
} wifi_prov_wrapper_ctx_t;

static wifi_prov_wrapper_ctx_t *s_wifi_prov_wrapper_ctx = NULL;

/* Event handler for catching system events */
static void wifi_prov_system_event_handler(void* arg, esp_event_base_t event_base,
                                            int32_t event_id, void* event_data)
{
    if (event_base == WIFI_PROV_EVENT) {
        static int retries;
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Provisioning started");
                break;
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Received Wi-Fi credentials"
                         "\n\tSSID     : %s\n\tPassword : %s",
                         (const char *) wifi_sta_cfg->ssid,
                         (const char *) wifi_sta_cfg->password);
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s"
                         "\n\tPlease reset to factory and retry provisioning",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                         "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
                retries ++;
                if (retries >= 5) {
                    ESP_LOGI(TAG, "Failed to connect with provisioned AP, reseting provisioned credentials");
                    wifi_prov_mgr_reset_sm_state_on_failure();
                    retries = 0;
                }
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful");
                retries = 0;
                break;
            case WIFI_PROV_END:
                /* De-initialize manager once provisioning is finished */
                wifi_prov_mgr_deinit();
                break;
            default:
                break;
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
        esp_wifi_connect();
    }
}

/* Event handler for application events */
static void wifi_prov_app_event_handler(void *user_data, wifi_prov_cb_event_t event, void *event_data)
{
    wifi_prov_wrapper_ctx_t *wifi_prov_wrapper_ctx = (wifi_prov_wrapper_ctx_t *)user_data;
    uint32_t event_cb_index = (uint32_t)wifi_prov_wrapper_ctx->app_event_handler.event_cb;

    if (event_cb_index) {
        uint32_t argv[3];

        argv[0] = (uint32_t)wifi_prov_wrapper_ctx->app_event_handler.user_data;
        argv[1] = (uint32_t)event;
        argv[2] = (uint32_t)event_data;

        wasm_runtime_call_indirect(wifi_prov_wrapper_ctx->exec_env, event_cb_index, 3, argv);
    }
}

static esp_err_t wifi_prov_custom_endpoint(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                            uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    wifi_prov_wrapper_ctx_t *wifi_prov_wrapper_ctx = (wifi_prov_wrapper_ctx_t *)priv_data;
    uint32_t event_cb_index = (uint32_t)wifi_prov_wrapper_ctx->endpoint_handler.handler;

    uint32_t argv[6];

    argv[0] = (uint32_t)session_id;
    argv[1] = (uint32_t)inbuf;
    argv[2] = (uint32_t)inlen;
    argv[3] = (uint32_t)outbuf;
    argv[4] = (uint32_t)outlen;
    argv[5] = (uint32_t)wifi_prov_wrapper_ctx->endpoint_handler.user_ctx;

    if (event_cb_index) {
        wasm_runtime_call_indirect(wifi_prov_wrapper_ctx->exec_env, event_cb_index, 6, argv);
    }

    return ESP_OK;
}

static int wasm_wifi_prov_mgr_init_wrapper(wasm_exec_env_t exec_env, uint32_t func_id, uint32_t argc, uint32_t *argv)
{
    if (s_wifi_prov_wrapper_ctx) {
        return ESP_ERR_INVALID_ARG;
    }

    wasm_module_inst_t module_inst = get_module_inst(exec_env);   
    if (!wasm_runtime_validate_native_addr(module_inst, argv, argc * sizeof(uint32_t))) {
        ESP_LOGE(TAG, "argv=%p argc=%"PRIu32" is out of range", argv, argc);
        return ESP_FAIL;
    }

    wifi_prov_wrapper_ctx_t *wifi_prov_wrapper_ctx = wasm_runtime_malloc(sizeof(wifi_prov_wrapper_ctx_t));
    if (!wifi_prov_wrapper_ctx) {
        ESP_LOGE(TAG, "Failed to allocate memory for wifi_prov_wrapper_ctx_t");
        return ESP_FAIL;
    }

    wifi_prov_wrapper_ctx->exec_env                       = exec_env;
    wifi_prov_wrapper_ctx->func_id                        = func_id;
    wifi_prov_wrapper_ctx->app_event_handler.event_cb     = (wifi_prov_cb_func_t)argv[0];
    wifi_prov_wrapper_ctx->app_event_handler.user_data    = (void *)argv[1];

    /* Register our event handler for Wi-Fi, IP and Provisioning related events */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, wifi_prov_system_event_handler, wifi_prov_wrapper_ctx));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_prov_system_event_handler, wifi_prov_wrapper_ctx));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_prov_system_event_handler, wifi_prov_wrapper_ctx));

    /* Configuration for the provisioning manager */
    wifi_prov_mgr_config_t config = {
#if defined(CONFIG_BT_BLUEDROID_ENABLED) || defined(CONFIG_BT_NIMBLE_ENABLED)
        .scheme = wifi_prov_scheme_ble,
#endif
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
        .app_event_handler = {
            .event_cb = wifi_prov_app_event_handler,
            .user_data = wifi_prov_wrapper_ctx
        }
    };

    switch (func_id) {
        case WIFI_PROV_CONSOLE:
            config.scheme = wifi_prov_scheme_console;
            break;
        case WIFI_PROV_SOFTAP:
            config.scheme = wifi_prov_scheme_softap;
            break;
#if defined(CONFIG_BT_BLUEDROID_ENABLED) || defined(CONFIG_BT_NIMBLE_ENABLED)
        case WIFI_PROV_BTDM:
            config.scheme_event_handler.event_cb = wifi_prov_scheme_ble_event_cb_free_btdm;
            break;
        case WIFI_PROV_BT:
            config.scheme_event_handler.event_cb = wifi_prov_scheme_ble_event_cb_free_bt;
            break;
        case WIFI_PROV_BLE:
            config.scheme_event_handler.event_cb = wifi_prov_scheme_ble_event_cb_free_ble;
            break;
#endif
        default:
            return ESP_ERR_INVALID_ARG;
            break;
    }

    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    s_wifi_prov_wrapper_ctx = wifi_prov_wrapper_ctx;

    return ESP_OK;
}

static int wasm_wifi_prov_mgr_deinit_wrapper(wasm_exec_env_t exec_env)
{
    esp_event_handler_unregister(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, wifi_prov_system_event_handler);
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_prov_system_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_prov_system_event_handler);

    wifi_prov_mgr_deinit();

    if (s_wifi_prov_wrapper_ctx) {
        wasm_runtime_free(s_wifi_prov_wrapper_ctx);
        s_wifi_prov_wrapper_ctx = NULL;
    }

    return ESP_OK;
}

static int wasm_wifi_prov_mgr_is_provisioned_wrapper(wasm_exec_env_t exec_env, bool *provisioned)
{
    return wifi_prov_mgr_is_provisioned(provisioned);
}

static int wasm_wifi_prov_mgr_start_provisioning_wrapper(wasm_exec_env_t exec_env, wifi_prov_security_t security, uint32_t pop_offset, const char *service_name, uint32_t service_key_offset)
{
    const char *service_key = NULL, *pop = NULL;
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (service_key_offset) {
        if (!validate_app_addr(service_key_offset, sizeof(uint32_t))) {
            wasm_runtime_set_exception(module_inst, NULL);
            return ESP_FAIL;
        }

        service_key = (const char *)addr_app_to_native(service_key_offset);
    }

    if (pop_offset) {
        if (!validate_app_addr(pop_offset, sizeof(uint32_t))) {
            wasm_runtime_set_exception(module_inst, NULL);
            return ESP_FAIL;
        }

        pop = (const char *)addr_app_to_native(pop_offset);
    }

    return wifi_prov_mgr_start_provisioning(security, pop, service_name, service_key);
}

static int wasm_wifi_prov_mgr_stop_provisioning_wrapper(wasm_exec_env_t exec_env)
{
    wifi_prov_mgr_stop_provisioning();

    return ESP_OK;
}

static int wasm_wifi_prov_mgr_wait_wrapper(wasm_exec_env_t exec_env)
{
    wifi_prov_mgr_wait();

    return ESP_OK;
}

static int wasm_wifi_prov_mgr_disable_auto_stop_wrapper(wasm_exec_env_t exec_env, uint32_t cleanup_delay)
{
    return wifi_prov_mgr_disable_auto_stop(cleanup_delay);
}

static int wasm_wifi_prov_mgr_set_app_info_wrapper(wasm_exec_env_t exec_env, const char *label, const char *version, int32_t capabilities_offset, size_t total_capabilities)
{
    const char **capabilities;
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    if (!validate_app_addr(capabilities_offset, sizeof(int32))) {
        wasm_runtime_set_exception(module_inst, NULL);
        return ESP_FAIL;
    }

    capabilities = (const char **)addr_app_to_native(capabilities_offset);
    for (int i = 0; i < total_capabilities; i ++) {
        capabilities[i] = (const char *)addr_app_to_native(capabilities[i]);
    }

    return wifi_prov_mgr_set_app_info(label, version, capabilities, total_capabilities);
}

static int wasm_wifi_prov_mgr_endpoint_create_wrapper(wasm_exec_env_t exec_env, const char *ep_name)
{
    return wifi_prov_mgr_endpoint_create(ep_name);
}

static int wasm_wifi_prov_mgr_endpoint_register_wrapper(wasm_exec_env_t exec_env, const char *ep_name, uint32_t handler, uint32_t user_ctx)
{
    wifi_prov_wrapper_ctx_t *wifi_prov_wrapper_ctx = s_wifi_prov_wrapper_ctx;
    if (!wifi_prov_wrapper_ctx) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_prov_wrapper_ctx->endpoint_handler.ep_name  = ep_name;
    wifi_prov_wrapper_ctx->endpoint_handler.handler  = (protocomm_req_handler_t)handler;
    wifi_prov_wrapper_ctx->endpoint_handler.user_ctx = (void *)user_ctx;

    return wifi_prov_mgr_endpoint_register(ep_name, wifi_prov_custom_endpoint, wifi_prov_wrapper_ctx);
}

static int wasm_wifi_prov_mgr_endpoint_unregister_wrapper(wasm_exec_env_t exec_env, const char *ep_name)
{
    wifi_prov_mgr_endpoint_unregister(ep_name);

    return ESP_OK;
}

static int wasm_wifi_prov_mgr_get_wifi_state_wrapper(wasm_exec_env_t exec_env, wifi_prov_sta_state_t *state)
{
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    if (!validate_native_addr((void *)state, sizeof(wifi_prov_sta_state_t))) {
        ESP_LOGE(TAG, "Failed to check for state by runtime");
        return ESP_FAIL;
    }

    return wifi_prov_mgr_get_wifi_state(state);
}

static int wasm_wifi_prov_mgr_get_wifi_disconnect_reason_wrapper(wasm_exec_env_t exec_env, wifi_prov_sta_fail_reason_t *reason)
{
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    if (!validate_native_addr((void *)reason, sizeof(wifi_prov_sta_fail_reason_t))) {
        ESP_LOGE(TAG, "Failed to check for disconnect reason by runtime");
        return ESP_FAIL;
    }

    return wifi_prov_mgr_get_wifi_disconnect_reason(reason);
}

static int wasm_wifi_prov_mgr_configure_sta_wrapper(wasm_exec_env_t exec_env, wifi_config_t *wifi_cfg)
{
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    if (!validate_native_addr((void *)wifi_cfg, sizeof(wifi_config_t))) {
        ESP_LOGE(TAG, "Failed to check for station configure by runtime");
        return ESP_FAIL;
    }

    return wifi_prov_mgr_configure_sta(wifi_cfg);
}

static int wasm_wifi_prov_mgr_reset_provisioning_wrapper(wasm_exec_env_t exec_env)
{
    return wifi_prov_mgr_reset_provisioning();
}

static int wasm_wifi_prov_mgr_reset_sm_state_on_failure_wrapper(wasm_exec_env_t exec_env)
{
    return wifi_prov_mgr_reset_sm_state_on_failure();
}

static int wasm_wifi_prov_scheme_ble_set_service_uuid_wrapper(wasm_exec_env_t exec_env, uint8_t *uuid128)
{
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    if (!validate_native_addr((uint8_t *)uuid128, 16 * sizeof(uint8_t))) {
        ESP_LOGE(TAG, "Failed to check a custom 128 bit UUID by runtime");
        return ESP_FAIL;
    }

    return wifi_prov_scheme_ble_set_service_uuid(uuid128);
}

static int wasm_wifi_prov_scheme_ble_set_mfg_data_wrapper(wasm_exec_env_t exec_env, uint8_t *mfg_data, ssize_t mfg_data_len)
{
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    if (!validate_native_addr((uint8_t *)mfg_data, mfg_data_len)) {
        ESP_LOGE(TAG, "Failed to check manufacturer specific data by runtime");
        return ESP_FAIL;
    }

    return wifi_prov_scheme_ble_set_mfg_data(mfg_data, mfg_data_len);
}

static NativeSymbol wm_wifi_prov_mgr_native_symbol[] = {
    REG_NATIVE_FUNC(wasm_wifi_prov_mgr_init, "(ii*)i"),
    REG_NATIVE_FUNC(wasm_wifi_prov_mgr_deinit, "()i"),
    REG_NATIVE_FUNC(wasm_wifi_prov_mgr_is_provisioned, "(*)i"),
    REG_NATIVE_FUNC(wasm_wifi_prov_mgr_start_provisioning, "(ii*i)i"),
    REG_NATIVE_FUNC(wasm_wifi_prov_mgr_stop_provisioning, "()i"),
    REG_NATIVE_FUNC(wasm_wifi_prov_mgr_wait, "()i"),
    REG_NATIVE_FUNC(wasm_wifi_prov_mgr_disable_auto_stop, "(i)i"),
    REG_NATIVE_FUNC(wasm_wifi_prov_mgr_set_app_info, "(**ii)i"),
    REG_NATIVE_FUNC(wasm_wifi_prov_mgr_endpoint_create, "(*)i"),
    REG_NATIVE_FUNC(wasm_wifi_prov_mgr_endpoint_register, "(*ii)i"),
    REG_NATIVE_FUNC(wasm_wifi_prov_mgr_endpoint_unregister, "(*)i"),
    REG_NATIVE_FUNC(wasm_wifi_prov_mgr_get_wifi_state, "($)i"),
    REG_NATIVE_FUNC(wasm_wifi_prov_mgr_get_wifi_disconnect_reason, "($)i"),
    REG_NATIVE_FUNC(wasm_wifi_prov_mgr_configure_sta, "($)i"),
    REG_NATIVE_FUNC(wasm_wifi_prov_mgr_reset_provisioning, "()i"),
    REG_NATIVE_FUNC(wasm_wifi_prov_mgr_reset_sm_state_on_failure, "()i"),
    REG_NATIVE_FUNC(wasm_wifi_prov_scheme_ble_set_service_uuid, "($)i"),
    REG_NATIVE_FUNC(wasm_wifi_prov_scheme_ble_set_mfg_data, "($i)i")
};

int wm_ext_wasm_native_wifi_provisioning_export(void)
{
    NativeSymbol *sym = (NativeSymbol *)wm_wifi_prov_mgr_native_symbol;
    int num = sizeof(wm_wifi_prov_mgr_native_symbol) / sizeof(wm_wifi_prov_mgr_native_symbol[0]);

    if (!wasm_native_register_natives("env", sym,  num)) {
        return -1;
    }

    return 0;
}
