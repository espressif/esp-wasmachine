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

#include "sdkconfig.h"
#include "wasm_export.h"

#ifdef CONFIG_EXTENDED_VFS_GPIO
#include "ioctl/esp_gpio_ioctl.h"
#endif

#ifdef CONFIG_EXTENDED_VFS_I2C
#include "ioctl/esp_i2c_ioctl.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_EXTENDED_VFS_GPIO
int wm_ext_wasm_gpio_ioctl(wasm_exec_env_t exec_env, int fd, int cmd, char *va_args);
#endif

#ifdef CONFIG_EXTENDED_VFS_I2C
int wm_ext_wasm_i2c_ioctl(wasm_exec_env_t exec_env, int fd, int cmd, char *va_args);
#endif

#ifdef __cplusplus
}
#endif
