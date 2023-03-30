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
