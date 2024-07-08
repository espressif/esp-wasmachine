/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

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

WM_EXT_WASM_NATIVE_EXPORT_FN(wm_ext_wasm_native_libm_export)
{
    return wm_ext_wasm_native_libm_export();
}
