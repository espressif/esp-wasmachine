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
#include <stddef.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DATA_SEQ_V_1    0x1 /*!< Data sequence data frame format version 1 */

typedef uint16_t    data_seq_type_t; /*!< Data type of frame type */ 
typedef uint16_t    data_seq_size_t; /*!< Data type of frame size */

/**
 * @brief Data sequence data frame.
 */
typedef struct data_seq_frame {
    data_seq_type_t type;   /*!< Frame type */ 
    data_seq_size_t size;   /*!< Frame size */ 
    uintptr_t       ptr;    /*!< Frame pointer */ 
} data_seq_frame_t;

/**
 * @brief Data sequence
 */
typedef struct data_seq {
    uint32_t            version;    /*!< Frame data format version */
    uint32_t            num;        /*!< Total number of frame */
    uint32_t            index;      /*!< Pointer of free frame */
    data_seq_frame_t    frame[0];   /*!< Frame array */ 
} data_seq_t;

#define DATA_SEQ_PUSH(ds, t, v)         data_seq_push(ds, t, sizeof(v), &(v))   /*!< Push data to data sequence, and this macro calculates data's length by sizeof(data) */
#define DATA_SEQ_POP(ds, t, v)          data_seq_pop(ds, t, sizeof(v), &(v))    /*!< Pop data from data sequence, and this macro calculates data's length by sizeof(data) */

#define DATA_SEQ_FORCE_PUSH(ds, t, v)   assert(DATA_SEQ_PUSH(ds, t, v) == 0)    /*!< Force to push data to data sequence, and this macro calculates data's length by sizeof(data), if failed it will assert */
#define DATA_SEQ_FORCE_POP(ds, t, v)    assert(DATA_SEQ_POP(ds, t, v) == 0)     /*!< Force to pop data from data sequence, and this macro calculates data's length by sizeof(data), if failed it will assert */

/**
  * @brief  Create data sequence by given number.
  *
  * @param  num This represents the maximum amount of data that can be pushed. If we want to
  *             serialize a struct, this number always is the number of elements of this struct.
  *
  * @return Data sequence pointer if success or NULL if failed.
  */
data_seq_t *data_seq_alloc(uint32_t num);

/**
  * @brief  Free data sequence.
  *
  * @param  ds Data sequence pointer which is created by "data_seq_alloc".
  *
  * @return None.
  */
void data_seq_free(data_seq_t *ds);

/**
  * @brief  Reset data sequence, and pushed data is cleared.
  *
  * @param  ds Data sequence pointer which is created by "data_seq_alloc".
  *
  * @return None.
  */
void data_seq_reset(data_seq_t *ds);

/**
  * @brief  Push data to data sequence.
  *
  * @param  ds   Data sequence pointer which is created by "data_seq_alloc".
  * @param  type Pushed data type, and this must be different from other all pushed data types,
  *              this means all pushed data type is unequal.
  * @param  size Pushed data size.
  * @param  data Pushed data pointer, and this data can't be free after pushed until others pop data
  *              from this data sequence.
  * 
  * @return
  *    - 0: succeed
  *    - -EINVAL: Input parameters are invalid
  *    - -ENOSPC: Data Sequence is full
  */
int data_seq_push(data_seq_t *ds, data_seq_type_t type, data_seq_size_t size, const void *data);

/**
  * @brief  Pop data from data sequence if its type and size are all matched.
  *
  * @param  ds   Data sequence pointer which is created by "data_seq_alloc".
  * @param  type Poped data type, and this must be different from other all pushed data types,
  *              this means all pushed data type is unequal.
  * @param  size Poped data size.
  * @param  data Poped data pointer, and all data will be copied to this data pointer.
  * 
  * @return
  *    - 0: succeed
  *    - -EINVAL: Input parameters are invalid
  *    - -ENODATA: No data is found in data sequence, maybe type is error, or size is error.
  */
int data_seq_pop(data_seq_t *ds, data_seq_type_t type, data_seq_size_t size, void *data);

#ifdef __cplusplus
}
#endif
