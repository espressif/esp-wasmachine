/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"
#include "esp_err.h"

#include "wm_ext_wasm_vfs.h"
#include "ext_vfs.h"

#ifdef CONFIG_WASMACHINE_EXT_VFS_UART
#include "esp_vfs_dev.h"
#endif

void wm_ext_wasm_vfs_init(void)
{
#ifdef CONFIG_WASMACHINE_EXT_VFS_UART
    esp_vfs_dev_uart_register();
#endif

#ifdef CONFIG_EXTENDED_VFS
    ext_vfs_init();
#endif
}
