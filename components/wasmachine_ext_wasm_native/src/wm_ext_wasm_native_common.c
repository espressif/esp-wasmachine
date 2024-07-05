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

#include <inttypes.h>

#include "esp_log.h"

#include "wm_ext_wasm_native_macro.h"
#include "wm_ext_wasm_native_common.h"

static const char *TAG = "wm_common";

int wm_ext_data_seq_addr_wasm2c(wasm_exec_env_t exec_env, data_seq_t *ds)
{
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (!wasm_runtime_validate_native_addr(module_inst, ds, sizeof(data_seq_t))) {
        ESP_LOGE(TAG, "failed to check ds");
        return -1;
    }

    for (uint32_t i = 0; i < ds->index; i++) {
        uintptr_t ptr;

        ptr = (uintptr_t)addr_app_to_native(ds->frame[i].ptr);
        if (!ds->frame[i].ptr) {
            ESP_LOGE(TAG, "failed to transform ptr[%"PRIx32"]=%x", i, ds->frame[i].ptr);
            return -1;
        } else {
            ds->frame[i].ptr = ptr;
        }
    }

    return 0;
}

data_seq_t *wm_ext_wasm_native_get_data_seq(wasm_exec_env_t exec_env, char *va_args)
{
    int ret;
    uint32_t addr;
    data_seq_t *ds;
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (!wasm_runtime_validate_native_addr(module_inst, va_args, 4)) {
        ESP_LOGE(TAG, "failed to check addr of va_args");
        return NULL;
    }

    addr = WASM_VA_ARG(va_args, uint32_t);
    if (!addr) {
        ESP_LOGE(TAG, "failed to check get addr from va_args");
        return NULL;
    }

    ds = addr_app_to_native(addr);
    if (!ds) {
        ESP_LOGE(TAG, "failed to check get ds from addr");
        return NULL;
    }

    ret = wm_ext_data_seq_addr_wasm2c(exec_env, ds);
    if (ret < 0) {
        ESP_LOGE(TAG, "failed to transform addr of data_seq");
        return NULL;
    }

    return ds;
}
