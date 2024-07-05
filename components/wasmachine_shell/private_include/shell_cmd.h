/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "argtable3/argtable3.h"
#include "esp_console.h"

#include "shell_config.h"

#define SHELL_CMD_CHECK(ins)                            \
    if (arg_parse(argc, argv, (void **)&ins)){          \
        arg_print_errors(stderr, ins.end, argv[0]);     \
        return -1;                                      \
    }

void shell_regitser_cmd_iwasm(void);
void shell_regitser_cmd_install(void);
void shell_regitser_cmd_uninstall(void);
void shell_regitser_cmd_query(void);
void shell_regitser_cmd_ls(void);
void shell_regitser_cmd_free(void);
void shell_regitser_cmd_wifi(void);
