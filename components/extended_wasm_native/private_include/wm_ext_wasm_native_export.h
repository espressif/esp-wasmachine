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
