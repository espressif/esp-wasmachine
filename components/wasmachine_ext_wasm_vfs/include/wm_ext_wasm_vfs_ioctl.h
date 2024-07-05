/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "sdkconfig.h"
#include "wasm_export.h"

#ifdef CONFIG_EXTENDED_VFS_GPIO
#include "ioctl/esp_gpio_ioctl.h"
#endif

#ifdef CONFIG_EXTENDED_VFS_I2C
#include "ioctl/esp_i2c_ioctl.h"
#endif

#ifdef CONFIG_EXTENDED_VFS_SPI
#include "ioctl/esp_spi_ioctl.h"
#endif

#ifdef CONFIG_EXTENDED_VFS_LEDC
#include "ioctl/esp_ledc_ioctl.h"
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

#ifdef CONFIG_EXTENDED_VFS_SPI
int wm_ext_wasm_native_spi_ioctl(wasm_exec_env_t exec_env, int fd, int cmd, char *va_args);
#endif

#ifdef CONFIG_EXTENDED_VFS_LEDC
int wm_ext_wasm_native_ledc_ioctl(wasm_exec_env_t exec_env, int fd, int cmd, char *va_args);
#endif

#ifdef __cplusplus
}
#endif
