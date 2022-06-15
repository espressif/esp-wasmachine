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

#define WASM_INTSIZEOF(n)       (((uint32_t)sizeof(n) + 3) & (uint32_t)~3)
#define WASM_VA_ARG(ap, t)      (*(t *)((ap += WASM_INTSIZEOF(t)) - WASM_INTSIZEOF(t)))
