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

#include <string.h>
#include <sys/ioctl.h>
#include <sys/errno.h>

#include "esp_log.h"

#include "wm_ext_wasm_native_vfs_ioctl.h"
#include "wm_ext_wasm_native_data_seq.h"
#include "wm_ext_wasm_native_common.h"

#define WASM_INTSIZEOF(n)       (((uint32_t)sizeof(n) + 3) & (uint32_t)~3)
#define WASM_VA_ARG(ap, t)      (*(t *)((ap += WASM_INTSIZEOF(t)) - WASM_INTSIZEOF(t)))

static const char *TAG = "wm_vfs_ioctl";

#ifdef CONFIG_WASMACHINE_EXT_VFS_GPIO
int wm_ext_wasm_native_gpio_ioctl(wasm_exec_env_t exec_env, int fd, int cmd, char *va_args)
{
    int ret;

    if (cmd == GPIOCSCFG) {
        uint32_t addr;
        data_seq_t *ds;
        gpioc_cfg_t cfg;
        wasm_module_inst_t module_inst = get_module_inst(exec_env);

        if (!wasm_runtime_validate_native_addr(module_inst, va_args, 4)) {
            ESP_LOGE(TAG, "failed to check addr of va_args");
            errno = EINVAL;
            return -1;
        }

        addr = WASM_VA_ARG(va_args, uint32_t);
        if (!addr) {
            ESP_LOGE(TAG, "failed to check get addr from va_args");
            errno = EINVAL;
            return -1;
        }

        ds = (data_seq_t *)addr_app_to_native(addr);
        if (!ds) {
            ESP_LOGE(TAG, "failed to check get ds from addr");
            return -1;
        }

        ret = data_seq_addr_wasm2c(exec_env, ds);
        if (ret < 0) {
            ESP_LOGE(TAG, "failed to transform addr of data_seq");
            errno = EINVAL;
            return -1;
        }

        memset(&cfg, 0, sizeof(gpioc_cfg_t));
        DATA_SEQ_POP(ds, DATA_SEQ_GPIOC_CFG_FLAGS, cfg.flags);

        ret = ioctl(fd, GPIOCSCFG, &cfg);
    } else {
        ESP_LOGE(TAG, "cmd=%x is not supported", cmd);
        errno = EINVAL;
        ret = -1;
    }

    return ret;
}
#endif
