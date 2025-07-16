/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"

#ifdef CONFIG_EXTENDED_VFS
#include <string.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <inttypes.h>

#include "esp_log.h"
#include "driver/ledc.h"

#include "wm_ext_wasm_vfs_ioctl.h"
#include "wm_ext_wasm_vfs_data_seq.h"
#include "wm_ext_wasm_native_common.h"

#define DATA_SEQ_POP_LEDC_CFG(ds, t, i, v) \
    DATA_SEQ_POP((ds), DATA_SEQ_LEDC_CFG_CHANNEL_SUB(DATA_SEQ_LEDC_CFG_CHANNEL_CFG, (t), (i)), (v))

static const char *TAG = "wm_vfs_ioctl";

#ifdef CONFIG_EXTENDED_VFS_GPIO
int wm_ext_wasm_gpio_ioctl(wasm_exec_env_t exec_env, int fd, int cmd, char *va_args)
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

#ifdef CONFIG_EXTENDED_VFS_I2C
int wm_ext_wasm_i2c_ioctl(wasm_exec_env_t exec_env, int fd, int cmd, char *va_args)
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

#ifdef CONFIG_EXTENDED_VFS_SPI
int wm_ext_wasm_native_spi_ioctl(wasm_exec_env_t exec_env, int fd, int cmd, char *va_args)
{
    int ret;
    data_seq_t *ds;

    ds = wm_ext_wasm_native_get_data_seq(exec_env, va_args);
    if (!ds) {
        errno = EINVAL;
        return -1;
    }

    if (cmd == SPIIOCSCFG) {
        spi_cfg_t cfg;

        DATA_SEQ_POP(ds, DATA_SEQ_SPI_CFG_CS_PIN,   cfg.cs_pin);
        DATA_SEQ_POP(ds, DATA_SEQ_SPI_CFG_SCLK_PIN, cfg.sclk_pin);
        DATA_SEQ_POP(ds, DATA_SEQ_SPI_CFG_MOSI_PIN, cfg.mosi_pin);
        DATA_SEQ_POP(ds, DATA_SEQ_SPI_CFG_MISO_PIN, cfg.miso_pin);
        DATA_SEQ_POP(ds, DATA_SEQ_SPI_CFG_FLAGS,    cfg.flags);
        if (cfg.flags & SPI_MASTER) {
            DATA_SEQ_POP(ds, DATA_SEQ_SPI_CFG_MASTER_CLK, cfg.master.clock);
        }

        ret = ioctl(fd, cmd, &cfg);
    } else if (cmd == SPIIOCEXCHANGE) {
        spi_ex_msg_t ex_msg;
        wasm_module_inst_t module_inst = get_module_inst(exec_env);

        memset(&ex_msg, 0, sizeof(spi_ex_msg_t));
        DATA_SEQ_POP(ds, DATA_SEQ_SPI_EX_MSG_TXBUF, ex_msg.tx_buffer);
        DATA_SEQ_POP(ds, DATA_SEQ_SPI_EX_MSG_RXBUF, ex_msg.rx_buffer);
        DATA_SEQ_POP(ds, DATA_SEQ_SPI_EX_MSG_SIZE,  ex_msg.size);

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

        DATA_SEQ_FORCE_UPDATE(ds, DATA_SEQ_SPI_EX_MSG_SIZE, ex_msg.size);
    } else {
        ESP_LOGE(TAG, "cmd=%x is not supported", cmd);
        errno = EINVAL;
        ret = -1;
    }

    return ret;
}
#endif

#ifdef CONFIG_EXTENDED_VFS_LEDC
int wm_ext_wasm_native_ledc_ioctl(wasm_exec_env_t exec_env, int fd, int cmd, char *va_args)
{
    int ret;
    data_seq_t *ds;

    if (cmd == LEDCIOCSCFG) {
        ledc_cfg_t cfg;
        ledc_channel_cfg_t channel_cfg[LEDC_CHANNEL_MAX];

        ds = wm_ext_wasm_native_get_data_seq(exec_env, va_args);
        if (!ds) {
            errno = EINVAL;
            return -1;
        }

        DATA_SEQ_POP(ds, DATA_SEQ_LEDC_CFG_FREQUENCY,   cfg.frequency);
        DATA_SEQ_POP(ds, DATA_SEQ_LEDC_CFG_CHANNEL_NUM, cfg.channel_num);

        ESP_LOGD(TAG, "frequency=%"PRIu32" channel_num=%"PRIu8"\n", cfg.frequency, cfg.channel_num);

        for (int i = 0; i < cfg.channel_num; i++) {
            DATA_SEQ_POP_LEDC_CFG(ds, DATA_SEQ_LEDC_CHANNEL_CFG_OUTPUT_PIN, i, channel_cfg[i].output_pin);
            DATA_SEQ_POP_LEDC_CFG(ds, DATA_SEQ_LEDC_CHANNEL_CFG_DUTY, i, channel_cfg[i].duty);
            DATA_SEQ_POP_LEDC_CFG(ds, DATA_SEQ_LEDC_CHANNEL_CFG_PHASE, i, channel_cfg[i].phase);
            ESP_LOGD(TAG, "channel=%d: output_pin=%"PRIu8" duty=%"PRIu32" phase=%"PRIu32"\n", i,
                     channel_cfg->output_pin, channel_cfg->duty, channel_cfg->phase);
        }

        cfg.channel_cfg = channel_cfg;

        ret = ioctl(fd, cmd, &cfg);
    } else if (cmd == LEDCIOCSSETFREQ) {
        uint32_t frequency;
        wasm_module_inst_t module_inst = get_module_inst(exec_env);

        if (!wasm_runtime_validate_native_addr(module_inst, va_args, 4)) {
            ESP_LOGE(TAG, "failed to check addr of va_args");
            errno = EINVAL;
            return -1;
        }

        frequency = WASM_VA_ARG(va_args, uint32_t);
        if (!frequency) {
            ESP_LOGE(TAG, "frequency is invalid");
            errno = EINVAL;
            return -1;
        }

        ret = ioctl(fd, cmd, frequency);
    } else if (cmd == LEDCIOCSSETDUTY) {
        ledc_duty_cfg_t duty_cfg;

        ds = wm_ext_wasm_native_get_data_seq(exec_env, va_args);
        if (!ds) {
            errno = EINVAL;
            return -1;
        }

        DATA_SEQ_POP(ds, DATA_SEQ_LEDC_DUTY_CFG_CHANNEL, duty_cfg.channel);
        DATA_SEQ_POP(ds, DATA_SEQ_LEDC_DUTY_CFG_DUTY, duty_cfg.duty);

        ret = ioctl(fd, cmd, &duty_cfg);
    } else if (cmd == LEDCIOCSSETDUTY) {
        ledc_phase_cfg_t phase_cfg;

        ds = wm_ext_wasm_native_get_data_seq(exec_env, va_args);
        if (!ds) {
            errno = EINVAL;
            return -1;
        }

        DATA_SEQ_POP(ds, DATA_SEQ_LEDC_PHASE_CFG_CHANNEL, phase_cfg.channel);
        DATA_SEQ_POP(ds, DATA_SEQ_LEDC_PHASE_CFG_PHASE, phase_cfg.phase);

        ret = ioctl(fd, cmd, &phase_cfg);
    } else if (cmd == LEDCIOCSPAUSE || cmd == LEDCIOCSRESUME) {
        ret = ioctl(fd, cmd);
    } else {
        ESP_LOGE(TAG, "cmd=%x is not supported", cmd);
        errno = EINVAL;
        ret = -1;
    }

    return ret;
}
#endif
#endif
