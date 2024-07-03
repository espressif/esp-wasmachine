/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#define REG_NATIVE_FUNC(func_name, signature) \
    { #func_name, func_name##_wrapper, signature, NULL }

#define get_module_inst(exec_env) \
    wasm_runtime_get_module_inst(exec_env)

#define validate_app_addr(offset, size) \
    wasm_runtime_validate_app_addr(module_inst, offset, size)

#define validate_app_str_addr(offset) \
    wasm_runtime_validate_app_str_addr(module_inst, offset)

#define validate_native_addr(addr, size) \
    wasm_runtime_validate_native_addr(module_inst, addr, size)

#define addr_app_to_native(offset) \
    wasm_runtime_addr_app_to_native(module_inst, offset)

#define addr_native_to_app(ptr) \
    wasm_runtime_addr_native_to_app(module_inst, ptr)

#define ATTR_CONTAINER_SET_STRING(key, value) if (value && !attr_container_set_string(&args, key, value)) { goto fail; }
#define ATTR_CONTAINER_SET_UINT16(key, value) if (value && !attr_container_set_uint16(&args, key, value)) { goto fail; }
#define ATTR_CONTAINER_SET_INT(key, value) if (value && !attr_container_set_int(&args, key, value)) { goto fail; }
#define ATTR_CONTAINER_SET_BOOL(key, value) if (value && !attr_container_set_bool(&args, key, value)) { goto fail; }

#define ATTR_CONTAINER_GET_STRING(key, value) if (attr_container_contain_key(args, key)) { value = attr_container_get_as_string(args, key); if (!value) goto fail; }
#define ATTR_CONTAINER_GET_UINT16(key, value) if (attr_container_contain_key(args, key)) { value = attr_container_get_as_uint16(args, key); if (!value) goto fail; }
#define ATTR_CONTAINER_GET_INT(key, value) if (attr_container_contain_key(args, key)) { value = attr_container_get_as_int(args, key); if (!value) goto fail; }
#define ATTR_CONTAINER_GET_BOOL(key, value) if (attr_container_contain_key(args, key)) { value = attr_container_get_as_bool(args, key); if (!value) goto fail; }
