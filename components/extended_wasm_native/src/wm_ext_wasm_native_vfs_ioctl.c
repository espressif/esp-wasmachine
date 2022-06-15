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

#include <string.h>
#include <sys/ioctl.h>
#include <sys/errno.h>

#include "esp_log.h"

#include "wm_ext_wasm_native_vfs_ioctl.h"
#include "wm_ext_wasm_native_data_seq.h"
#include "wm_ext_wasm_native_common.h"

static const char *TAG = "wm_vfs_ioctl";

#ifdef CONFIG_WASMACHINE_EXT_VFS_GPIO
int wm_ext_wasm_native_gpio_ioctl(wasm_exec_env_t exec_env, int fd, int cmd, char *va_args)
{
    int ret;
    data_seq_t *ds;

    ds = wm_ext_wasm_native_get_data_seq(exec_env, va_args);
    if (!ds) {
        errno = EINVAL;
        return -1;
    }

    if (cmd == GPIOCSCFG) {
        gpioc_cfg_t cfg;

        memset(&cfg, 0, sizeof(gpioc_cfg_t));
        DATA_SEQ_POP(ds, DATA_SEQ_GPIOC_CFG_FLAGS, cfg.flags);

        ret = ioctl(fd, GPIOCSCFG, &cfg);
    } else {
        ESP_LOGE(TAG, "cmd=%x is not supported", cmd);
        errno = EINVAL;
        ret = -1;
    }

    return ret;
}
#endif

#ifdef CONFIG_WASMACHINE_EXT_VFS_I2C
int wm_ext_wasm_native_i2c_ioctl(wasm_exec_env_t exec_env, int fd, int cmd, char *va_args)
{
    int ret;
    data_seq_t *ds;

    ds = wm_ext_wasm_native_get_data_seq(exec_env, va_args);
    if (!ds) {
        errno = EINVAL;
        return -1;
    }

    if (cmd == I2CIOCSCFG) {
        i2c_cfg_t cfg;

        memset(&cfg, 0, sizeof(i2c_cfg_t));
        DATA_SEQ_POP(ds, DATA_SEQ_I2C_CFG_SDA_PIN, cfg.sda_pin);
        DATA_SEQ_POP(ds, DATA_SEQ_I2C_CFG_SCL_PIN, cfg.scl_pin);
        DATA_SEQ_POP(ds, DATA_SEQ_I2C_CFG_FLAGS,   cfg.flags);
        if (cfg.flags & I2C_MASTER) {
            DATA_SEQ_POP(ds, DATA_SEQ_I2C_CFG_MASTER_CLK, cfg.master.clock);
        } else {
            DATA_SEQ_POP(ds, DATA_SEQ_I2C_CFG_SLAVE_MAX_CLK, cfg.slave.max_clock);
            DATA_SEQ_POP(ds, DATA_SEQ_I2C_CFG_SLAVE_ADDR,    cfg.slave.addr);
        }

        ret = ioctl(fd, cmd, &cfg);
    } else if (cmd == I2CIOCRDWR) {
        i2c_msg_t msg;
        wasm_module_inst_t module_inst = get_module_inst(exec_env);

        memset(&msg, 0, sizeof(i2c_msg_t));
        DATA_SEQ_POP(ds, DATA_SEQ_I2C_MSG_FLAGS, msg.flags);
        DATA_SEQ_POP(ds, DATA_SEQ_I2C_MSG_ADDR,  msg.addr);
        DATA_SEQ_POP(ds, DATA_SEQ_I2C_MSG_SIZE,  msg.size);
        DATA_SEQ_POP(ds, DATA_SEQ_I2C_MSG_BUF,   msg.buffer);

        msg.buffer = addr_app_to_native((uint32_t)msg.buffer);
        if (!msg.buffer) {
            errno = EINVAL;
            return -1;
        }

        ret = ioctl(fd, cmd, &msg);
    } else if (cmd == I2CIOCEXCHANGE) {
        i2c_ex_msg_t ex_msg;
        wasm_module_inst_t module_inst = get_module_inst(exec_env);

        memset(&ex_msg, 0, sizeof(i2c_ex_msg_t));
        DATA_SEQ_POP(ds, DATA_SEQ_I2C_EX_MSG_FLAGS,  ex_msg.flags);
        DATA_SEQ_POP(ds, DATA_SEQ_I2C_EX_MSG_ADDR,   ex_msg.addr);
        DATA_SEQ_POP(ds, DATA_SEQ_I2C_EX_MSG_DELAY_US,  ex_msg.delay_ms);
        DATA_SEQ_POP(ds, DATA_SEQ_I2C_EX_MSG_TXBUF,  ex_msg.tx_buffer);
        DATA_SEQ_POP(ds, DATA_SEQ_I2C_EX_MSG_TXSIZE, ex_msg.tx_size);
        DATA_SEQ_POP(ds, DATA_SEQ_I2C_EX_MSG_RXBUF,  ex_msg.rx_buffer);
        DATA_SEQ_POP(ds, DATA_SEQ_I2C_EX_MSG_RXSIZE, ex_msg.rx_size);

        ex_msg.tx_buffer = addr_app_to_native((uint32_t)ex_msg.tx_buffer);
        if (!ex_msg.tx_buffer) {
            errno = EINVAL;
            return -1;
        }

        ex_msg.rx_buffer = addr_app_to_native((uint32_t)ex_msg.rx_buffer);
        if (!ex_msg.rx_buffer) {
            errno = EINVAL;
            return -1;
        }

        ret = ioctl(fd, cmd, &ex_msg);
    } else {
        ESP_LOGE(TAG, "cmd=%x is not supported", cmd);
        errno = EINVAL;
        ret = -1;
    }

    return ret;
}
#endif
