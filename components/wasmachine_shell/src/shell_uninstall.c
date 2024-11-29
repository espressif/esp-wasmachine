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
#include "coap_ext.h"

#include "esp_log.h"
#include "shell_utils.h"
#include "shell_cmd.h"
#include "wm_wamr.h"

#define URL_MAX_LEN 256
#define UNISTALL_TIMEOUT 1000
#define TOCHAR(_d) # _d

#define url_remain_space (sizeof(url) - strlen(url))

static const char TAG[] = "shell_uninstall";

static struct {
    struct arg_str *name;
    struct arg_str *type;
    struct arg_end *end;
} uninstall_main_arg;

static int uninstall_main(int argc, char **argv)
{
    int ret = -1;
    const char *m_name;
    bool uninstalled = false;
    request_t request[1] = { 0 };
    char url[URL_MAX_LEN] = { 0 };

    SHELL_CMD_CHECK(uninstall_main_arg);

    if (!uninstall_main_arg.name->count) {
        ESP_LOGE(TAG, "App name should be given");
        return -1;
    }
    m_name = uninstall_main_arg.name->sval[0];

    wm_wamr_app_mgr_lock();
    if (!app_manager_lookup_module_data(m_name)) {
        ESP_LOGE(TAG, "App %s is not installed", m_name);
        goto fail1;
    }

    snprintf(url, sizeof(url) - 1, "/applet?name=%s", m_name);
    if (uninstall_main_arg.type->count > 0 && url_remain_space > 0) {
        snprintf(url + strlen(url), url_remain_space, "&type=%s",
                 uninstall_main_arg.type->sval[0]);
    }

    init_request(request, url, COAP_DELETE, FMT_ATTR_CONTAINER, NULL, 0);
    request->mid = esp_random();

    ret = wm_wamr_app_send_request(request, REQUEST_PACKET);
    if (ret) {
        ESP_LOGE(TAG, "Failed to send request");
        goto fail1;
    }

    for (int i = 0; i < UNISTALL_TIMEOUT; i += portTICK_PERIOD_MS) {
        if (!app_manager_lookup_module_data(m_name)) {
            uninstalled = true;
            break;
        }
        vTaskDelay(1);
    }

    if (!uninstalled) {
        ESP_LOGE(TAG, "Failed to uninstall App %s", m_name);
        goto fail1;
    }

    wm_wamr_app_mgr_unlock();
    return 0;

fail1:
    wm_wamr_app_mgr_unlock();
    return 1;
}

void shell_regitser_cmd_uninstall(void)
{
    int cmd_num = 2;

    uninstall_main_arg.name =
        arg_str1("u", NULL, "<App Name>", "name of the application");
    uninstall_main_arg.type =
        arg_str0(NULL, "type", "<App Type>", "Type of app. Can be 'wasm'(default)");

    uninstall_main_arg.end = arg_end(cmd_num);

    const esp_console_cmd_t cmd = {
        .command = "uninstall",
        .help = "Uninstall WASM App",
        .hint = NULL,
        .func = &uninstall_main,
        .argtable = &uninstall_main_arg
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
