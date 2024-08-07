/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/errno.h>

#include "wasm_export.h"

#include "esp_log.h"
#include "shell_cmd.h"
#include "shell_utils.h"

static const char TAG[] = "shell_utils";

int shell_open_file(shell_file_t *file, const char *name)
{
    int ret;
    int fd;
    char *file_path;
    off_t size;
    uint8_t *pbuf;

    ret = asprintf(&file_path, SHELL_ROOT_FS_PATH"/%s", name);
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to generate path errno=%d", errno);
        return -1;
    }

    fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "Failed to open file %s errno=%d", file_path, errno);
        goto errout_open_file;
    }

    size = lseek(fd, 0, SEEK_END);
    if (size == -1) {
        ESP_LOGE(TAG, "Failed to seek file %s errno=%d", file_path, errno);
        goto errout_lseek_end;
    }

    ret = lseek(fd, 0, SEEK_SET);
    if (ret == -1) {
        ESP_LOGE(TAG, "Failed to seek file %s errno=%d", file_path, errno);
        goto errout_lseek_end;
    }

    pbuf = wasm_runtime_malloc(size);
    if (!pbuf) {
        ESP_LOGE(TAG, "Failed to malloc %lu bytes", size);
        goto errout_lseek_end;
    }

    ret = read(fd, pbuf, size);
    if (ret != size) {
        ESP_LOGE(TAG, "Failed to read ret=%d", ret);
        goto errout_read_fs;
    }

    free(file_path);
    close(fd);

    file->payload = pbuf;
    file->size = size;

    return 0;

errout_read_fs:
    wasm_runtime_free(pbuf);
errout_lseek_end:
    close(fd);
errout_open_file:
    free(file_path);
    return -1;
}

void shell_close_file(shell_file_t *file)
{
    wasm_runtime_free(file->payload);
}
