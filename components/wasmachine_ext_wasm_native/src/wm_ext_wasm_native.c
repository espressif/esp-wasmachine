/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"
#include "esp_err.h"

#include "wm_ext_wasm_native.h"
#include "wm_ext_wasm_native_export.h"

void wm_ext_wasm_native_export(void)
{
#ifdef CONFIG_WASMACHINE_WASM_EXT_NATIVE_LIBC
    ESP_ERROR_CHECK(wm_ext_wasm_native_libc_export());
#endif

#ifdef CONFIG_WASMACHINE_WASM_EXT_NATIVE_MQTT
    ESP_ERROR_CHECK(wm_ext_wasm_native_mqtt_export());
#endif

#ifdef CONFIG_WASMACHINE_WASM_EXT_NATIVE_HTTP_CLIENT
    ESP_ERROR_CHECK(wm_ext_wasm_native_http_client_export());
#endif

#ifdef CONFIG_WASMACHINE_WASM_EXT_NATIVE_LVGL
    ESP_ERROR_CHECK(wm_ext_wasm_native_lvgl_export());
#endif

#ifdef CONFIG_WASMACHINE_WASM_EXT_NATIVE_WIFI_PROVISIONING
    ESP_ERROR_CHECK(wm_ext_wasm_native_wifi_provisioning_export());
#endif

#ifdef CONFIG_WASMACHINE_WASM_EXT_NATIVE_RMAKER
    ESP_ERROR_CHECK(wm_ext_wasm_native_rmaker_export());
#endif

#ifdef CONFIG_WASMACHINE_WASM_EXT_NATIVE_LIBMATH
    ESP_ERROR_CHECK(wm_ext_wasm_native_libm_export());
#endif
}
