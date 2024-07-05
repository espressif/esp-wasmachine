/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wasm_export.h"
#include "app_manager_export.h"
#include "app_manager.h"

#include "esp_log.h"
#include "shell_utils.h"
#include "shell_cmd.h"
#include "wm_wamr.h"

#define URL_MAX_LEN 256
#define INSTALL_TIMEOUT 2000
#define _TOCHAR(_d) # _d
#define TOCHAR(_d)  _TOCHAR(_d)

#define url_remain_space (sizeof(url) - strlen(url))

static const char TAG[] = "shell_install";

static struct {
    struct arg_str *file;
    struct arg_str *name;
    struct arg_int *heap_size;
    struct arg_str *type;
    struct arg_int *max_timers;
    struct arg_int *watchdog_interval;
    struct arg_end *end;
} install_main_arg;

static int install_main(int argc, char **argv)
{
    int ret = -1;
    shell_file_t file;
    const char *m_name;
    bool installed = false;
    request_t request[1] = { 0 };
    char url[URL_MAX_LEN] = { 0 };

    SHELL_CMD_CHECK(install_main_arg);

    if (!install_main_arg.name->count) {
        ESP_LOGE(TAG, "App name should be given");
        return -1;
    }
    m_name = install_main_arg.name->sval[0];

    wm_wamr_app_mgr_lock();
    if (app_manager_lookup_module_data(m_name)) {
        ESP_LOGE(TAG, "App %s is already installed", m_name);
        goto fail1;
    }

    ret = shell_open_file(&file, install_main_arg.file->sval[0]);
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to open file %s", install_main_arg.file->sval[0]);
        goto fail1;
    }

    snprintf(url, sizeof(url) - 1, "/applet?name=%s", m_name);
    if (install_main_arg.type->count > 0 && url_remain_space > 0) {
        snprintf(url + strlen(url), url_remain_space, "&type=%s",
                 install_main_arg.type->sval[0]);
    }
    if (install_main_arg.heap_size->count > 0 && url_remain_space > 0) {
        snprintf(url + strlen(url), url_remain_space, "&heap=%d",
                 install_main_arg.heap_size->ival[0]);
    }
    if (install_main_arg.max_timers->count > 0 && url_remain_space > 0) {
        snprintf(url + strlen(url), url_remain_space, "&timers=%d",
                 install_main_arg.max_timers->ival[0]);
    }
    if (install_main_arg.watchdog_interval->count > 0 && url_remain_space > 0) {
        snprintf(url + strlen(url), url_remain_space, "&wd=%d",
                 install_main_arg.watchdog_interval->ival[0]);
    }

    init_request(request, url, COAP_PUT, FMT_APP_RAW_BINARY, file.payload, file.size);
    request->mid = esp_random();

    ret = wm_wamr_app_send_request(request, INSTALL_WASM_APP);
    if (ret) {
        ESP_LOGE(TAG, "Failed to send request");
        goto fail2;
    }

    for (int i = 0; i < INSTALL_TIMEOUT; i += portTICK_PERIOD_MS) {
        if (app_manager_lookup_module_data(m_name)) {
            installed = true;
            break;
        }
        vTaskDelay(1);
    }

    if (!installed) {
        ESP_LOGE(TAG, "Failed to install App %s", m_name);
        goto fail2;
    }

    shell_close_file(&file);
    wm_wamr_app_mgr_unlock();
    return 0;

fail2:
    shell_close_file(&file);
fail1:
    wm_wamr_app_mgr_unlock();
    return -1;
}

void shell_regitser_cmd_install(void)
{
    int cmd_num = 6;

    install_main_arg.file =
        arg_str1(NULL, NULL, "<App File>", "application file name with full path");
    install_main_arg.name =
        arg_str1("i", NULL, "<App Name>", "name of the application");
    install_main_arg.heap_size =
        arg_int0(NULL, "heap", "<Heap Size>", "Heap size of app, default is " TOCHAR(APP_HEAP_SIZE_DEFAULT));
    install_main_arg.type =
        arg_str0(NULL, "type", "<App Type>", "Type of app. Can be 'wasm'(default)");
    install_main_arg.max_timers =
        arg_int0(NULL, "timers", "<Timers Number>", "Max timers number app can use, default is " TOCHAR(DEFAULT_TIMERS_PER_APP));
    install_main_arg.watchdog_interval =
        arg_int0(NULL, "watchdog", "<Watchdog Interval>", "Watchdog interval in ms, default is " TOCHAR(DEFAULT_WATCHDOG_INTERVAL));

    install_main_arg.end = arg_end(cmd_num);

    const esp_console_cmd_t cmd = {
        .command = "install",
        .help = "Install WASM App from file-system",
        .hint = NULL,
        .func = &install_main,
        .argtable = &install_main_arg
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
