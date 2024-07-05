/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct shell_file {
    uint8_t *payload;
    int size;
} shell_file_t;

int shell_open_file(shell_file_t *file, const char *name);
void shell_close_file(shell_file_t *file);

#ifdef __cplusplus
}
#endif
