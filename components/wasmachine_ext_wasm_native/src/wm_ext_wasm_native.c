/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_err.h"

#include "wm_ext_wasm_native.h"
#include "wm_ext_wasm_native_export.h"

void wm_ext_wasm_native_export(void)
{
    for (wm_ext_wasm_native_export_fn_t *p = &__wm_ext_wasm_native_export_fn_array_start; p < &__wm_ext_wasm_native_export_fn_array_end; ++p) {
        ESP_ERROR_CHECK((*(p->fn))());
    }
}
