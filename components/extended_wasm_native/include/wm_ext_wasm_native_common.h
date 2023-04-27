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
#include "wasm_export.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WASM_INTSIZEOF(n)       (((uint32_t)sizeof(n) + 3) & (uint32_t)~3)
#define WASM_VA_ARG(ap, t)      (*(t *)((ap += WASM_INTSIZEOF(t)) - WASM_INTSIZEOF(t)))

/**
  * @brief  Check data sequence space and transform pointer address from WASM to runtime.
  *
  * @param  exec_env WAMR execution envirenment pointer
  * @param  ds data sequence pointer
  *
  * @return 0 if success or a negative value if failed.
  */
int wm_ext_data_seq_addr_wasm2c(wasm_exec_env_t exec_env, data_seq_t *ds);

/**
  * @brief  Get data sequence pointer, check space and transform address from WASM to runtime.
  *
  * @param  exec_env WAMR execution envirenment pointer
  * @param  va_args arguments list pointer
  *
  * @return 0 if success or a negative value if failed.
  */
data_seq_t *wm_ext_wasm_native_get_data_seq(wasm_exec_env_t exec_env, char *va_args);

#ifdef __cplusplus
}
#endif
