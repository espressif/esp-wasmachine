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

#include "esp_event.h"
#include "esp_littlefs.h"
#include "esp_log.h"

#include "nvs_flash.h"
#include "bsp_board.h"

#include "protocol_examples_common.h"

#ifdef CONFIG_WASMACHINE_SHELL
#include "wm_shell.h"
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

void app_main(void)
{
    ESP_ERROR_CHECK(bsp_board_init());

    fs_init();

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());

    wm_wamr_init();

#ifdef CONFIG_WASMACHINE_APP_MGR
    wm_wamr_app_mgr_init();
#endif

#ifdef CONFIG_WASMACHINE_SHELL
    wm_shell_init();
#endif
}
