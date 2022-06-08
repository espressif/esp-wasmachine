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

#include <fcntl.h>
#include <sys/errno.h>

#include "esp_vfs.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "wm_ext_vfs_export.h"

static const char *TAG = "wm_ext_vfs_gpio";

static int gpio_open(const char *path, int flags, int mode)
{
    int gpio_num;
    esp_err_t ret;
    gpio_config_t io_conf = { 0 };

    ESP_LOGV(TAG, "open(%s, %x, %x)", path, flags, mode);

    gpio_num = atoi(path + 1);
    if (gpio_num < 0 || gpio_num >= GPIO_PIN_COUNT) {
        ESP_LOGE(TAG, "gpio_num=%d is out of range", gpio_num);
        errno = EINVAL;
        return -1;
    }

    io_conf.intr_type    = GPIO_INTR_DISABLE;
    io_conf.pin_bit_mask = BIT64(gpio_num);
    if (flags & O_RDONLY) {
        io_conf.mode = GPIO_MODE_INPUT;
    } else if (flags & O_WRONLY) {
        io_conf.mode = GPIO_MODE_OUTPUT;
    } else if (flags & O_RDWR) {
        io_conf.mode = GPIO_MODE_INPUT_OUTPUT;
    }
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to gpio_config ret=%d", ret);
        errno = EIO;
        return -1;
    }

    ESP_LOGV(TAG, "gpio_num=%d io_conf.mode=%d", gpio_num, io_conf.mode);

    return gpio_num;
}

static ssize_t gpio_write(int fd, const void *buffer, size_t size)
{
    uint32_t level;
    const uint8_t *data = (const uint8_t *)buffer;

    ESP_LOGV(TAG, "write(%d, %p, %u)", fd, buffer, size);

    if (!data) {
        errno = -EINVAL;
        return -1;
    }

    level = data[0] ? 1 : 0;

    ESP_LOGV(TAG, "gpio_set_level(%d, %d)", fd, level);

    gpio_set_level(fd, level);

    return 1;
}

static ssize_t gpio_read(int fd, void *buffer, size_t size)
{
    uint8_t *data = (uint8_t *)buffer;

    ESP_LOGV(TAG, "read(%d, %p, %u)", fd, buffer, size);

    if (!data) {
        errno = -EINVAL;
        return -1;
    }

    data[0] = gpio_get_level(fd);

    ESP_LOGV(TAG, "gpio_get_level(%d)=%d", fd, data[0]);

    return 1;
}

static int gpio_close(int fd)
{
    ESP_LOGV(TAG, "close(%d)", fd);

    return 0;
}

static int gpio_ioctl(int fd, int cmd, va_list va_args)
{
    return -1;
}

int wm_ext_vfs_gpio_init(void)
{
    static const esp_vfs_t vfs = {
        .flags   = ESP_VFS_FLAG_DEFAULT,
        .open    = gpio_open,
        .write   = gpio_write,
        .read    = gpio_read,
        .ioctl   = gpio_ioctl,
        .close   = gpio_close,
    };
    const char *base_path = "/dev/gpio";

    esp_err_t err = esp_vfs_register(base_path, &vfs, NULL);
    if (err != ESP_OK) {
        return err;
    }

    return 0;
}
