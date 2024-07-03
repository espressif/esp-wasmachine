/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

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
