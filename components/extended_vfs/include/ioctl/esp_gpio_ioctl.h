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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define _GPIOCBASE              (0x8100) /*!< GPIO ioctl command basic value */
#define _GPIOC(nr)              (_GPIOCBASE | (nr)) /*!< GPIO ioctl command macro */

/**
 * @brief GPIO ioctl commands.
 */
#define GPIOCSCFG               _GPIOC(0x0001) /*!< Set GPIO configuration */

/**
 * @brief GPIO configuration flag
 */
#define GPIOC_PULLDOWN_EN       (1 << 0)  /*!< Enable GPIO pin pull-up */
#define GPIOC_PULLUP_EN         (1 << 1)  /*!< Enable GPIO pin pull-down */
#define GPIOC_OPENDRAIN_EN      (1 << 2)  /*!< Enable GPIO pin open-drain */

/**
 * @brief GPIO configuration.
 */
typedef struct gpioc_cfg {
    union {
        struct {
            uint32_t pulldown_en   : 1;  /*!< Enable GPIO pin pull-up */
            uint32_t pullup_en     : 1;  /*!< Enable GPIO pin pull-down */
            uint32_t opendrain_en  : 1;  /*!< Enable GPIO pin open-drain */
        } flags_data;
        uint32_t flags; /*!< GPIO configuration flags */
    };
} gpioc_cfg_t;

#ifdef __cplusplus
}
#endif
