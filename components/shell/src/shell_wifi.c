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

#include <stdio.h>
#include <string.h>

#include <esp_wifi.h>
#include <esp_log.h>
#include "shell_cmd.h"

static const char *TAG = "shell_wifi";

static struct {
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} sta_args;

static void evt_handler(void *arg, esp_event_base_t evt_base, int32_t evt_id, void *evt_data)
{
    if (evt_base == WIFI_EVENT && evt_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
}

/**
 * @brief  A function which implements wifi config command.
 */
static int sta_func(int argc, char **argv)
{
    SHELL_CMD_CHECK(sta_args);

    wifi_config_t wifi_config = { 0 };

    if (sta_args.ssid->count) {
        if (strlen(sta_args.ssid->sval[0]) > sizeof(wifi_config.sta.ssid)) {
            ESP_LOGW(TAG, "SSID of target AP is longer");
            return ESP_FAIL;
        }
        strcpy((char *)wifi_config.sta.ssid, sta_args.ssid->sval[0]);
    }

    if (sta_args.password->count) {
        if (strlen(sta_args.ssid->sval[0]) > sizeof(wifi_config.sta.ssid)) {
            ESP_LOGW(TAG, "Password of target AP is longer");
            return ESP_FAIL;
        }
        strcpy((char *)wifi_config.sta.password, sta_args.password->sval[0]);
    }

    if (strlen((char *)wifi_config.sta.ssid)) {
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
    } else {
        ESP_LOGW(TAG, "SSID of target AP is shorter");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief  Register sta config command.
 */
static void register_sta_config()
{
    sta_args.ssid     = arg_str0("s", "ssid", "<ssid>", "SSID of target AP");
    sta_args.password = arg_str0("p", "password", "<password>", "Password of target AP");
    sta_args.end      = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "sta",
        .help = "Set the target AP configuration of station connected",
        .hint = NULL,
        .func = &sta_func,
        .argtable = &sta_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

void shell_regitser_cmd_wifi(void)
{
    /* Initialize TCP/IP */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Initialize Wi-Fi including netif with default config */
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, evt_handler, NULL));

    register_sta_config();
}
