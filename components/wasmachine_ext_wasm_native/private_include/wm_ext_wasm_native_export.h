/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int wm_ext_wasm_native_libc_export(void);
int wm_ext_wasm_native_libm_export(void);
int wm_ext_wasm_native_mqtt_export(void);
int wm_ext_wasm_native_http_client_export(void);
int wm_ext_wasm_native_lvgl_export(void);
int wm_ext_wasm_native_wifi_provisioning_export(void);
int wm_ext_wasm_native_rmaker_export(void);

#ifdef __cplusplus
}
#endif
