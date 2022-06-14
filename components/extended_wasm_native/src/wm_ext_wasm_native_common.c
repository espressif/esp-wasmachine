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

#include "wm_ext_wasm_native_common.h"

static const char *TAG = "wm_common";

int data_seq_addr_wasm2c(wasm_exec_env_t exec_env, data_seq_t *ds)
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
            ESP_LOGE(TAG, "failed to transform ptr[%d]=%x", i, ds->frame[i].ptr);
            return -1;
        } else {
            ds->frame[i].ptr = ptr;
        }
    }

    return 0;
}
