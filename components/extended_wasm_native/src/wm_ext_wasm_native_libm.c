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

#include <unistd.h>
#include <math.h>
#include <fcntl.h>
#include <sys/errno.h>

#include "esp_log.h"

#include "bh_platform.h"
#include "wasm_export.h"
#include "wasm_native.h"
#include "wasm_runtime_common.h"

#include "wm_ext_wasm_native_macro.h"
#include "wm_ext_wasm_native_export.h"

static float sinf_wrapper(wasm_exec_env_t exec_env, float value)
{
    return sinf(value);
}

static float cosf_wrapper(wasm_exec_env_t exec_env, float value)
{
    return cosf(value);
}

static NativeSymbol wm_math_wrapper_native_symbol[] = {
    REG_NATIVE_FUNC(sinf,   "(f)f"),
    REG_NATIVE_FUNC(cosf,   "(f)f")
};

int wm_ext_wasm_native_libm_export(void)
{
    NativeSymbol *sym = (NativeSymbol *)wm_math_wrapper_native_symbol;
    int num = sizeof(wm_math_wrapper_native_symbol) / sizeof(wm_math_wrapper_native_symbol[0]);

    if (!wasm_native_register_natives("env", sym,  num)) {
        return -1;
    }

    return 0;
}
