/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_idf_version.h"

#include "wm_ext_wasm_vfs.h"
#include "ext_vfs.h"

#ifdef CONFIG_WASMACHINE_EXT_VFS_UART
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 3, 0)
#include "esp_vfs_dev.h"
#else
#include "driver/uart_vfs.h"
#endif
#endif

void wm_ext_wasm_vfs_init(void)
{
#ifdef CONFIG_WASMACHINE_EXT_VFS_UART
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 3, 0)
    esp_vfs_dev_uart_register();
#else
    uart_vfs_dev_register();
#endif
#endif

#ifdef CONFIG_EXTENDED_VFS
    ext_vfs_init();
#endif
}
