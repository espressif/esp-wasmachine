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

#include "data_seq.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief GPIO ioctl configuration struct member's index of data sequence
 */
#define DATA_SEQ_GPIOC_CFG_FLAGS    1  /*!< gpioc_cfg_t->flags */

/**
 * @brief I2C ioctl configuration struct member's index of data sequence
 */
#define DATA_SEQ_I2C_CFG_SDA_PIN    1  /*!< i2c_cfg_t->sda_pin */
#define DATA_SEQ_I2C_CFG_SCL_PIN    2  /*!< i2c_cfg_t->scl_pin */
#define DATA_SEQ_I2C_CFG_FLAGS      3  /*!< i2c_cfg_t->flags */
#define DATA_SEQ_I2C_CFG_MASTER_CLK 4  /*!< i2c_cfg_t->master.clock */
#define DATA_SEQ_I2C_CFG_SLAVE_MAX_CLK 5  /*!< i2c_cfg_t->slaves.max_clock */
#define DATA_SEQ_I2C_CFG_SLAVE_ADDR 6  /*!< i2c_cfg_t->slaves.addr */

/**
 * @brief I2C ioctl message struct member's index of data sequence
 */
#define DATA_SEQ_I2C_MSG_FLAGS      1  /*!< i2c_msg_t->flags */
#define DATA_SEQ_I2C_MSG_ADDR       2  /*!< i2c_msg_t->addr */
#define DATA_SEQ_I2C_MSG_BUF        3  /*!< i2c_msg_t->buffer */
#define DATA_SEQ_I2C_MSG_SIZE       4  /*!< i2c_msg_t->size */

/**
 * @brief I2C ioctl exchange message struct member's index of data sequence
 */
#define DATA_SEQ_I2C_EX_MSG_FLAGS   1  /*!< i2c_ex_msg->flags */
#define DATA_SEQ_I2C_EX_MSG_ADDR    2  /*!< i2c_ex_msg->addr */
#define DATA_SEQ_I2C_EX_MSG_DELAY_US 3  /*!< i2c_ex_msg->delay */
#define DATA_SEQ_I2C_EX_MSG_TXBUF   4  /*!< i2c_ex_msg->tx_buffer */
#define DATA_SEQ_I2C_EX_MSG_TXSIZE  5  /*!< i2c_ex_msg->tx_size */
#define DATA_SEQ_I2C_EX_MSG_RXBUF   6  /*!< i2c_ex_msg->rx_buffer */
#define DATA_SEQ_I2C_EX_MSG_RXSIZE  7  /*!< i2c_ex_msg->rx_size */ 

#ifdef __cplusplus
}
#endif
