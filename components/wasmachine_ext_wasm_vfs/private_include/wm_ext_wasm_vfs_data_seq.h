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

/**
 * @brief SPI ioctl configuration struct member's index of data sequence
 */
#define DATA_SEQ_SPI_CFG_CS_PIN     1  /*!< spi_cfg_t->cs_pin */
#define DATA_SEQ_SPI_CFG_SCLK_PIN   2  /*!< spi_cfg_t->sclk_pin */
#define DATA_SEQ_SPI_CFG_MOSI_PIN   3  /*!< spi_cfg_t->mosi_pin */
#define DATA_SEQ_SPI_CFG_MISO_PIN   4  /*!< spi_cfg_t->miso_pin */
#define DATA_SEQ_SPI_CFG_FLAGS      5  /*!< spi_cfg_t->flags */
#define DATA_SEQ_SPI_CFG_MASTER_CLK 6  /*!< spi_cfg_t->master.clock */

/**
 * @brief SPI ioctl exchange message struct member's index of data sequence
 */
#define DATA_SEQ_SPI_EX_MSG_TXBUF   1  /*!< spi_ex_msg_t->tx_buffer */
#define DATA_SEQ_SPI_EX_MSG_RXBUF   2  /*!< spi_ex_msg_t->rx_buffer */
#define DATA_SEQ_SPI_EX_MSG_SIZE    3  /*!< spi_ex_msg_t->rx_size */ 


/**
 * @brief LEDC ioctl configuration struct member's index of data sequence
 */
#define DATA_SEQ_LEDC_CFG_FREQUENCY 1    /*!< ledc_cfg_t->frequency */
#define DATA_SEQ_LEDC_CFG_CHANNEL_NUM 2  /*!< ledc_cfg_t->channel_num */
#define DATA_SEQ_LEDC_CFG_CHANNEL_CFG 3  /*!< ledc_cfg_t->channel_cfg */ 

#define DATA_SEQ_LEDC_CFG_CHANNEL_SUB(a, b, c)  ((a) | ((b) << 8) | ((c) << 16))

/**
 * @brief LEDC ioctl channel configuration struct member's index of data sequence
 */
#define DATA_SEQ_LEDC_CHANNEL_CFG_OUTPUT_PIN 1  /*!< ledc_channel_cfg_t->output_pin */
#define DATA_SEQ_LEDC_CHANNEL_CFG_DUTY 2        /*!< ledc_channel_cfg_t->duty */
#define DATA_SEQ_LEDC_CHANNEL_CFG_PHASE 3       /*!< ledc_channel_cfg_t->phase */ 

/**
 * @brief LEDC ioctl duty configuration struct member's index of data sequence
 */
#define DATA_SEQ_LEDC_DUTY_CFG_CHANNEL 1    /*!< ledc_duty_cfg_t->channel */
#define DATA_SEQ_LEDC_DUTY_CFG_DUTY 2       /*!< ledc_duty_cfg_t->duty */ 

/**
 * @brief LEDC ioctl phase configuration struct member's index of data sequence
 */
#define DATA_SEQ_LEDC_PHASE_CFG_CHANNEL 1   /*!< ledc_phase_cfg_t->channel */
#define DATA_SEQ_LEDC_PHASE_CFG_PHASE 2     /*!< ledc_phase_cfg_t->phase */ 

#ifdef __cplusplus
}
#endif
