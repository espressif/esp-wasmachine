/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <sys/param.h>

#include "esp_log.h"
#include "esp_heap_caps.h"

#include "wasm_export.h"

#ifdef CONFIG_WASMACHINE_WASM_EXT_NATIVE
#include "wm_ext_wasm_native.h"
#endif

#ifdef CONFIG_WASMACHINE_EXT_VFS
#include "wm_ext_wasm_vfs.h"
#endif

#include "wm_wamr.h"

#define MALLOC_ALIGN_SIZE       8

static const char *TAG = "wm_wamr";

static void *wamr_malloc(unsigned int size)
{
    void *ptr;
#ifdef CONFIG_SPIRAM
    uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
    uint32_t caps = MALLOC_CAP_8BIT;
#endif

    ptr = heap_caps_aligned_alloc(MALLOC_ALIGN_SIZE, size, caps);
    ESP_LOGV(TAG, "malloc ptr=%p size=%u", ptr, size);

    return ptr;
}

static void wamr_free(void *ptr)
{
    ESP_LOGV(TAG, "free ptr=%p", ptr);

    heap_caps_free(ptr);
}

static void *wamr_realloc(void *ptr, unsigned int size)
{
    void *new_ptr;

    new_ptr = wamr_malloc(size);
    if (new_ptr) {
        if (ptr) {
            size_t n = heap_caps_get_allocated_size(ptr);
            size_t m = MIN(size, n);
            memcpy(new_ptr, ptr, m);
            wamr_free(ptr);
        }
    }

    ESP_LOGV(TAG, "realloc ptr=%p size=%u new_ptr=%p", ptr, size, new_ptr);

    return new_ptr;
}

void wm_wamr_init(void)
{
    RuntimeInitArgs init_args;

    memset(&init_args, 0, sizeof(RuntimeInitArgs));
    init_args.mem_alloc_type = Alloc_With_Allocator;
    init_args.mem_alloc_option.allocator.malloc_func  = wamr_malloc;
    init_args.mem_alloc_option.allocator.realloc_func = wamr_realloc;
    init_args.mem_alloc_option.allocator.free_func    = wamr_free;

    assert(wasm_runtime_full_init(&init_args));

#ifdef CONFIG_WASMACHINE_WASM_EXT_NATIVE
    wm_ext_wasm_native_export();
#endif

#ifdef CONFIG_WASMACHINE_EXT_VFS
    wm_ext_wasm_vfs_init();
#endif
}
