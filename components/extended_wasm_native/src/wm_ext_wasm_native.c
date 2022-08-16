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
}