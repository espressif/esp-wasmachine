/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_event.h"
#include "esp_littlefs.h"
#include "esp_log.h"

#include "nvs_flash.h"

#include "protocol_examples_common.h"

#ifdef CONFIG_WASMACHINE_SHELL
#include "wm_shell.h"
#endif

#if CONFIG_WASMACHINE_WASM_EXT_NATIVE_LVGL
#include "wm_ext_wasm_native.h"

#include "bsp/esp-bsp.h"
#endif

#include "wm_wamr.h"

static const char *TAG = "wm_main";

static void fs_init(void)
{
    size_t total = 0, used = 0;
    esp_vfs_littlefs_conf_t conf = {
        .base_path = WM_FILE_SYSTEM_BASE_PATH,
        .partition_label = "storage",
        .format_if_mount_failed = false,
        .dont_mount = false,
    };

    ESP_ERROR_CHECK(esp_vfs_littlefs_register(&conf));
    ESP_ERROR_CHECK(esp_littlefs_info(conf.partition_label, &total, &used));

    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
}

#if CONFIG_WASMACHINE_WASM_EXT_NATIVE_LVGL
static void bsp_display_config(void)
{
#if CONFIG_IDF_TARGET_ESP32S3
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
#if CONFIG_BSP_LCD_DRAW_BUF_DOUBLE
        .double_buffer = 1,
#else
        .double_buffer = 0,
#endif
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
        }
    };
#else
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .sw_rotate = false,
        }
    };
#endif

    cfg.lvgl_port_cfg.task_stack = 16384;

    assert(bsp_display_start_with_config(&cfg));
}
#endif

static void bsp_init(void)
{
#if CONFIG_WASMACHINE_WASM_EXT_NATIVE_LVGL
    bsp_display_config();

    wm_ext_wasm_native_lvgl_ops_t lvgl_ops = {
        .backlight_on = bsp_display_backlight_on,
        .backlight_off = bsp_display_backlight_off,
        .lock = bsp_display_lock,
        .unlock = bsp_display_unlock,
    };

    ESP_ERROR_CHECK(wm_ext_wasm_native_lvgl_register_ops(&lvgl_ops));
#endif
}

void app_main(void)
{
    bsp_init();
    fs_init();

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

#ifndef CONFIG_WASMACHINE_SHELL_CMD_WIFI
#if defined(CONFIG_EXAMPLE_CONNECT_WIFI) || defined(CONFIG_EXAMPLE_CONNECT_ETHERNET)
    ESP_ERROR_CHECK(example_connect());
#endif
#endif

    wm_wamr_init();

#ifdef CONFIG_WASMACHINE_APP_MGR
    wm_wamr_app_mgr_init();
#endif

#ifdef CONFIG_WASMACHINE_SHELL
    wm_shell_init();
#endif
}
