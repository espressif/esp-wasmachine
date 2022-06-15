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

#include "wm_ext_vfs.h"
#include "wm_ext_vfs_export.h"

#ifdef CONFIG_WASMACHINE_EXT_VFS_UART
#include "esp_vfs_dev.h"
#endif

void wm_ext_vfs_init(void)
{
#ifdef CONFIG_WASMACHINE_EXT_VFS_UART
    esp_vfs_dev_uart_register();
#endif

#ifdef CONFIG_WASMACHINE_EXT_VFS_GPIO
    ESP_ERROR_CHECK(wm_ext_vfs_gpio_init());
#endif

#ifdef CONFIG_WASMACHINE_EXT_VFS_I2C
    ESP_ERROR_CHECK(wm_ext_vfs_i2c_init());
#endif
}
