/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int (*fn)(void);   /*!< Pointer to the export function */
} wm_ext_wasm_native_export_fn_t;

/**
 * @brief Define a wasmachine extended wasm native module export function which can be executed automatically, in application layer.
 *
 * @param f  function name (identifier)
 * @param (varargs)  optional, additional attributes for the function declaration (such as IRAM_ATTR)
 *
 * The function defined using this macro must return int on success. Any other value will be
 * logged and the automatic, process in application layer should be abort.
 *
 * There should be at lease one undefined symble to be added in the source code of exported module in order to avoid
 * the optimization of the linker. Because otherwise the linker will ignore exported module as it has
 * no other files depending on any symbols in it.
 *
 * Some thing like this should be added in the CMakeLists.txt of the exported module:
 *  target_link_libraries(${COMPONENT_LIB} INTERFACE "-u wm_ext_wasm_native_libc_export")
 */
#define WM_EXT_WASM_NATIVE_EXPORT_FN(f, ...) \
    static int __VA_ARGS__ __##f(); \
    static __attribute__((used)) _SECTION_ATTR_IMPL(".wm_ext_wasm_native_export_fn", __COUNTER__) \
        wm_ext_wasm_native_export_fn_t _##f = { .fn = ( __##f) }; \
    static int __##f(void)

/**
 * @brief extended wasm native module export function array start.
 */
extern wm_ext_wasm_native_export_fn_t __wm_ext_wasm_native_export_fn_array_start;

/**
 * @brief extended wasm native module export function array end.
 */
extern wm_ext_wasm_native_export_fn_t __wm_ext_wasm_native_export_fn_array_end;

#ifdef __cplusplus
}
#endif
