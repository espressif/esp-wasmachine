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

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WASMachine LVGL display operations.
 */
typedef struct wm_ext_wasm_native_lvgl_ops {

    /*!< Turn on backlight */
    esp_err_t (*backlight_on)(void);

    /*!< Turn off backlight */
    esp_err_t (*backlight_off)(void);

    /*!< Lock LVGL core */
    bool (*lock)(uint32_t timeout_ms);

    /*!< Unlock LVGL core */
    void (*unlock)(void);
} wm_ext_wasm_native_lvgl_ops_t;

void wm_ext_wasm_native_export(void);

/**
 * @brief Register WASMachine LVGL display operations
 *
 * @param ops WASMachine LVGL display operations
 *
 * @return
 *      - ESP_OK on success
 *      - Others if failed
 */
esp_err_t wm_ext_wasm_native_lvgl_register_ops(wm_ext_wasm_native_lvgl_ops_t *ops);

#ifdef __cplusplus
}
#endif
