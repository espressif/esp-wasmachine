// Copyright 2023 Espressif Systems (Shanghai) PTE LTD
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

#include <sys/param.h>
#include <sys/errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"

/* Using CONFIG_LITTLEFS_OBJ_NAME_LEN instead of MAXPATHLEN */
#define DIR_PATH_LEN    (CONFIG_LITTLEFS_OBJ_NAME_LEN + 1)

static const char *TAG = "openat";

int openat(int dir_fd, const char *pathname, int flags, ...)
{
    int fd;
    int ret;
    char dir_path[DIR_PATH_LEN];
    char *full_path;

    ret = fcntl(dir_fd, F_GETPATH, dir_path);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to get fd=%d path", dir_fd);
        errno = -EINVAL;
        return -1;
    }

    ret = asprintf(&full_path, "%s/%s", dir_path, pathname);
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to get full path");
        errno = ENOMEM;
        return -1;     
    }

    fd = open(full_path, flags);
    free(full_path);

    return fd;
}
