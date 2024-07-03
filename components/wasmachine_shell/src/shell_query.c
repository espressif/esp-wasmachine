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

static struct {
    struct arg_str *name;
    struct arg_end *end;
} query_main_arg;

static int query_main(int argc, char **argv)
{
    module_data *m_data;
    int num = 0;
    int i = 1;
    const char *name = NULL;

    SHELL_CMD_CHECK(query_main_arg);

    if (query_main_arg.name->count) {
        name = query_main_arg.name->sval[0];
    }

    wm_wamr_app_mgr_lock();
    os_mutex_lock(&module_data_list_lock);

    m_data = module_data_list;
    while (m_data) {
        num++;
        m_data = m_data->next;
    }

    m_data = module_data_list;
    printf("{\n");
    if (!name) {
        printf("\t\"num\":\t%d", num);
    }
    while (m_data) {
        if (!name) {
            printf(",\n\t\"applet%d\":\t\"%s\",\n", i, m_data->module_name);
            printf("\t\"heap%d\":\t%d", i, m_data->heap_size);
            i++;
        } else if (!strcmp(name, m_data->module_name)) {
            printf("\t\"applet\":\t\"%s\",\n", m_data->module_name);
            printf("\t\"heap\":\t\t%d", m_data->heap_size);
            break;
        }

        m_data = m_data->next;
    }
    printf("\n}\n");

    os_mutex_unlock(&module_data_list_lock);
    wm_wamr_app_mgr_unlock();
    return 0;
}

void shell_regitser_cmd_query(void)
{
    int cmd_num = 1;

    query_main_arg.name =
        arg_str0("q", NULL, "<App Name>", "name of the application");
    query_main_arg.end = arg_end(cmd_num);

    const esp_console_cmd_t cmd = {
        .command = "query",
        .help = "Query all applications",
        .hint = NULL,
        .func = &query_main,
        .argtable = &query_main_arg
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
