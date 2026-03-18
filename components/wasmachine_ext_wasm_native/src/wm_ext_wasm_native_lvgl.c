/*
 * SPDX-FileCopyrightText: 2022-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sys/lock.h>
#include <inttypes.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_memory_utils.h"

#include "bh_platform.h"
#include "wasm_export.h"
#include "wasm_native.h"
#include "wasm_runtime_common.h"

#include "wm_ext_wasm_native.h"
#include "wm_ext_wasm_native_macro.h"
#include "wm_ext_wasm_native_export.h"
#include "wm_ext_wasm_native_lvgl.h"

#include "lvgl.h"
#include "src/core/lv_obj_private.h"
#include "src/misc/lv_types.h"
#include "src/misc/lv_event_private.h"
#include "src/misc/lv_timer_private.h"
#include "src/misc/lv_area_private.h"
#include "src/draw/lv_draw_private.h"
#include "src/display/lv_display_private.h"
#include "src/others/observer/lv_observer.h"
#include "src/others/observer/lv_observer_private.h"
#include "src/draw/sw/lv_draw_sw.h"

#if CONFIG_WASMACHINE_WASM_EXT_NATIVE_LVGL_USE_WASM_HEAP
#if configNUM_THREAD_LOCAL_STORAGE_POINTERS < 3
#error "configNUM_THREAD_LOCAL_STORAGE_POINTERS should be at least 3"
#else
#define LVGL_WASM_TASK_LOCAL_STORAGE_INDEX 2
#endif
#endif

#define LVGL_ARG_BUF_NUM        16
#define LVGL_ARG_NUM_MAX        64

#define LVGL_WASM_CALLBACK_STACK_SIZE   (8192)

#define MAX_BUTTONMAP_ITER      256
#define MAX_TEXT_ITER           65536

#define lvgl_native_return_type(type)       type *lvgl_ret = (type *)(args_ret)
#define lvgl_native_get_arg(type, name)     type name = *((type *)(args++))
#define lvgl_native_set_return(val)         *lvgl_ret = (val)

#define DEFINE_LVGL_NATIVE_WRAPPER(func_name)                   \
    static void func_name ## _wrapper(wasm_exec_env_t exec_env, \
                                      uint32_t *args,           \
                                      uint32_t *args_ret)

#define LVGL_NATIVE_WRAPPER(id, func_name, argc)                \
    [id] = { func_name ## _wrapper, argc }

#define LVGL_TRACE_ABORT()  abort()

#define LV_VERSION_VAL(major, minor, patch) ((major << 16) | (minor << 8) | (patch))
#define LV_VERSION  LV_VERSION_VAL(WM_LV_VERSION_MAJOR, \
                                   WM_LV_VERSION_MINOR, \
                                   WM_LV_VERSION_PATCH)

#define _VERSION_STR_HELPER(x) #x
#define _VERSION_STR(major, minor, patch) "v" _VERSION_STR_HELPER(major) "." \
                                              _VERSION_STR_HELPER(minor) "." \
                                              _VERSION_STR_HELPER(patch)
#define LV_VERSION_STR _VERSION_STR(WM_LV_VERSION_MAJOR, \
                                    WM_LV_VERSION_MINOR, \
                                    WM_LV_VERSION_PATCH)
#define ESP_BROOKESIA_SELECTOR_TRANS_VALUE             0xFFFFFFFF

typedef void (*lvgl_func_t)(wasm_exec_env_t exec_env, uint32_t *args, uint32_t *args_ret);
typedef void (*lv_async_cb_t)(void *);

typedef struct lvgl_func_desc {
    lvgl_func_t     func;
    uint32_t        argc;
} lvgl_func_desc_t;

typedef struct {
    lv_obj_t *scr;
    int count_val;
} timer_context_t;

typedef struct {
    uint32_t event_cb;
    wasm_module_inst_t module_inst;
} lvgl_cb_wrapper_t;

typedef struct {
    uint32_t custom_exec_cb;
    uint32_t start_cb;
    uint32_t ready_cb;
    uint32_t deleted_cb;
    uint32_t get_value_cb;
    uint32_t path_cb;
    wasm_module_inst_t module_inst;
    uint8_t cb_type;
} lvgl_anim_cb_wrapper_t;

typedef struct _lv_async_info_t {
    lv_async_cb_t cb;
    void *user_data;
} lv_async_info_t;

typedef struct {
    char **map;
    int size;
} wasm_text_map_t;

static const char *TAG = "wm_lvgl_wrapper";

static uint8_t s_lvgl_ref;
static wm_ext_wasm_native_lvgl_ops_t s_lvgl_ops;

static int esp_lvgl_init_wrapper(wasm_exec_env_t exec_env, uint32_t version)
{
    int ret = -1;
    if (LV_VERSION != version) {
        uint16_t major = (uint16_t)((version >> 16) & 0xFFFF);
        if (major != WM_LV_VERSION_MAJOR) {
            ESP_LOGE(TAG, "Failed to match version, wasmachine LVGL version: %s, WDF LVGL version: v%"PRIu32".%"PRIu32".%"PRIu32"\n",
                     LV_VERSION_STR, (version >> 16) & 0xFFFF, (version >> 8) & 0xFF, version & 0xFF);
            return ret;
        }
    }

    ret = 0;
    if (s_lvgl_ref == 0) {
        ret = s_lvgl_ops.backlight_on();
    }

    s_lvgl_ref++;
    return ret;
}

static int esp_lvgl_deinit_wrapper(wasm_exec_env_t exec_env)
{
    int ret = 0;
    if (s_lvgl_ref > 0) {
        if (s_lvgl_ref == 1) {
            ret = s_lvgl_ops.backlight_off();
        }

        s_lvgl_ref--;
    }

    return ret;
}

static void lvgl_lock_wrapper(void)
{
    s_lvgl_ops.lock(0);
}

static void lvgl_unlock_wrapper(void)
{
    s_lvgl_ops.unlock();
}

#if CONFIG_IDF_TARGET_ESP32P4
static bool ptr_is_in_external_ram(const void *ptr)
{
    return ((intptr_t)ptr >= SOC_EXTRAM_LOW) &&
           ((intptr_t)ptr < SOC_EXTRAM_HIGH);
}
#endif

static bool ptr_is_in_ram_or_rom(const void *ptr)
{
    bool ret = false;

    if (esp_ptr_in_dram(ptr)) {
        ret = true;
    } else if (esp_ptr_in_drom(ptr)) {
        ret = true;
#if CONFIG_IDF_TARGET_ESP32P4
    } else {
        ret = ptr_is_in_external_ram(ptr);
#else
    } else if (esp_ptr_external_ram(ptr)) {
        ret = true;
#endif
    }

    return ret;
}

static void *map_ptr(wasm_exec_env_t exec_env, const void *app_addr)
{
    void *ptr;
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (ptr_is_in_ram_or_rom(app_addr)) {
        return (void *)app_addr;
    }

    ptr = (void *)addr_app_to_native((uint32_t)app_addr);
    if (!ptr) {
        ESP_LOGE(TAG, "failed to map app_addr=%p", app_addr);
        return NULL;
    }

    return ptr;
}

static bool lvgl_run_wasm(void *_module_inst, uint32_t cb, int argc, uint32_t *argv)
{
    bool ret;
    const char *exception;
    wasm_module_inst_t module_inst = (wasm_module_inst_t)_module_inst;
    wasm_exec_env_t exec_env = wasm_runtime_create_exec_env(module_inst, LVGL_WASM_CALLBACK_STACK_SIZE);

    ret = wasm_runtime_call_indirect(exec_env, cb, argc, argv);
    if (!ret) {
        ESP_LOGE(TAG, "failed to run WASM callback cb=0x%"PRIx32" argc=%d argv=%p ra=%p",
                 cb, argc, argv, __builtin_return_address(0));
        if ((exception = wasm_runtime_get_exception(module_inst))) {
            ESP_LOGE(TAG, "%s", exception);
        }
    }

    wasm_runtime_destroy_exec_env(exec_env);

    return ret;
}

static void lvgl_event_cb_wrapper(lv_event_t *e)
{
    uint32_t argv[1];

    if (e && e->ext_data.data) {
        lvgl_cb_wrapper_t *wrapper = (lvgl_cb_wrapper_t *)e->ext_data.data;
        if (wrapper && wrapper->module_inst) {
            argv[0] = (uint32_t)e;
            lvgl_run_wasm(wrapper->module_inst, wrapper->event_cb, 1, argv);
        }
    }
}

static void lvgl_event_wrapper_destructor(void *ext_data)
{
    free(ext_data);
}

static bool esp_addr_executable(uint32_t addr)
{
    bool is_psram = false;
#if CONFIG_IDF_TARGET_ESP32P4
    is_psram = ptr_is_in_external_ram((const void *)addr);
#endif
    return (esp_ptr_executable((const void *)addr) || is_psram);
}

static void lvgl_timer_cb_wrapper(lv_timer_t *timer)
{
    uint32_t argv[1];

    if (timer) {
        lvgl_cb_wrapper_t *wrapper = (lvgl_cb_wrapper_t *)timer->ext_data.data;
        void (* timer_cb)(lv_timer_t *timer);
        if (wrapper && wrapper->module_inst && !esp_addr_executable(wrapper->event_cb)) {
            argv[0] = (uint32_t)timer;

            lvgl_run_wasm(wrapper->module_inst, wrapper->event_cb, 1, argv);
        } else if (wrapper && wrapper->event_cb) {
            timer_cb = (void *)wrapper->event_cb;
            timer_cb(timer);
        }
    }
}

static void lvgl_timer_cb_wrapper_destructor(void *ext_data)
{
    free(ext_data);
}

static void lvgl_anim_custom_exec_cb_wrapper(lv_anim_t *var, int32_t value)
{
    uint32_t argv[2];
    if (var) {
        lvgl_anim_cb_wrapper_t *wrapper = (lvgl_anim_cb_wrapper_t *)var->ext_data.data;
        if (!wrapper) {
            return;
        }

        void (* custom_exec_cb)(lv_anim_t *var, int32_t value);
        if (wrapper && wrapper->module_inst && !esp_addr_executable(wrapper->custom_exec_cb)) {
            wasm_module_inst_t module_inst = wrapper->module_inst;
            uint32_t wasm_anim = 0;
            if (wrapper->cb_type) {
                wasm_anim = addr_native_to_app((void *)var);
                argv[0] = wasm_anim ? wasm_anim : (uint32_t)var;
            } else {
                wasm_anim = addr_native_to_app((void *)(var->var));
                argv[0] = wasm_anim ? wasm_anim : (uint32_t)var->var;
            }

            ESP_LOGD(TAG, "lvgl_anim_custom_exec_cb_wrapper: %p -> %"PRIx32"", var, wasm_anim);

            argv[1] = value;

            lvgl_run_wasm(wrapper->module_inst, wrapper->custom_exec_cb, 2, argv);
        } else {
            custom_exec_cb = (void (*)(lv_anim_t *, int32_t))wrapper->custom_exec_cb;
            if (wrapper->cb_type) {
                custom_exec_cb(var, value);
            } else {
                custom_exec_cb((lv_anim_t *)(var->var), value);
            }

        }
    }
}

static void lvgl_anim_start_cb_wrapper(lv_anim_t *var)
{
    uint32_t argv[1];
    if (var) {
        lvgl_anim_cb_wrapper_t *wrapper = (lvgl_anim_cb_wrapper_t *)var->ext_data.data;
        if (!wrapper) {
            return;
        }

        void (* start_cb)(lv_anim_t *var);
        if (wrapper && wrapper->module_inst && !esp_addr_executable(wrapper->start_cb)) {
            wasm_module_inst_t module_inst = wrapper->module_inst;
            uint32_t wasm_anim = addr_native_to_app((void *)var);

            ESP_LOGD(TAG, "lvgl_anim_start_cb_wrapper: %p -> %"PRIx32"", var, wasm_anim);

            argv[0] = wasm_anim ? wasm_anim : (uint32_t)var;

            lvgl_run_wasm(wrapper->module_inst, wrapper->start_cb, 1, argv);
        } else {
            start_cb = (void (*)(lv_anim_t *))wrapper->start_cb;
            start_cb(var);
        }
    }
}

static void lvgl_anim_ready_cb_wrapper(lv_anim_t *var)
{
    uint32_t argv[1];
    if (var) {
        lvgl_anim_cb_wrapper_t *wrapper = (lvgl_anim_cb_wrapper_t *)var->ext_data.data;
        if (!wrapper) {
            return;
        }

        void (* ready_cb)(lv_anim_t *var);
        if (wrapper && wrapper->module_inst && !esp_addr_executable(wrapper->ready_cb)) {
            wasm_module_inst_t module_inst = wrapper->module_inst;
            uint32_t wasm_anim = addr_native_to_app((void *)var);

            ESP_LOGD(TAG, "lvgl_anim_ready_cb_wrapper: %p -> %"PRIx32"", var, wasm_anim);

            argv[0] = wasm_anim ? wasm_anim : (uint32_t)var;

            lvgl_run_wasm(wrapper->module_inst, wrapper->ready_cb, 1, argv);
        } else {
            ready_cb = (void (*)(lv_anim_t *))wrapper->ready_cb;
            ready_cb(var);
        }
    }
}

static void lvgl_anim_deleted_cb_wrapper(lv_anim_t *var)
{
    uint32_t argv[1];
    if (var) {
        lvgl_anim_cb_wrapper_t *wrapper = (lvgl_anim_cb_wrapper_t *)var->ext_data.data;
        if (!wrapper) {
            return;
        }

        void (* deleted_cb)(lv_anim_t *var);
        if (wrapper && wrapper->module_inst && !esp_addr_executable(wrapper->deleted_cb)) {
            wasm_module_inst_t module_inst = wrapper->module_inst;
            uint32_t wasm_anim = addr_native_to_app((void *)var);

            ESP_LOGD(TAG, "lvgl_anim_deleted_cb_wrapper: %p -> %"PRIx32"", var, wasm_anim);

            argv[0] = wasm_anim ? wasm_anim : (uint32_t)var;

            lvgl_run_wasm(wrapper->module_inst, wrapper->deleted_cb, 1, argv);
        } else {
            deleted_cb = (void (*)(lv_anim_t *))wrapper->deleted_cb;
            deleted_cb(var);
        }
    }
}

static int32_t lvgl_anim_get_value_cb_wrapper(lv_anim_t *var)
{
    bool ret;
    uint32_t argv[1];
    if (var) {
        lvgl_anim_cb_wrapper_t *wrapper = (lvgl_anim_cb_wrapper_t *)var->ext_data.data;
        if (!wrapper) {
            return 0;
        }

        int32_t (* get_value_cb)(lv_anim_t *var);
        if (wrapper && wrapper->module_inst && !esp_addr_executable(wrapper->get_value_cb)) {
            wasm_module_inst_t module_inst = wrapper->module_inst;
            uint32_t wasm_anim = addr_native_to_app((void *)var);

            ESP_LOGD(TAG, "lvgl_anim_get_value_cb_wrapper: %p -> %"PRIx32"", var, wasm_anim);

            argv[0] = wasm_anim ? wasm_anim : (uint32_t)var;

            ret = lvgl_run_wasm(wrapper->module_inst, wrapper->get_value_cb, 1, argv);

            return ret ? argv[0] : 0;
        } else {
            get_value_cb = (int32_t (*)(lv_anim_t *))wrapper->get_value_cb;
            return get_value_cb(var);
        }
    }

    return 0;
}

static int32_t lvgl_anim_path_cb_wrapper(const lv_anim_t *var)
{
    bool ret;
    uint32_t argv[1];
    if (var) {
        lvgl_anim_cb_wrapper_t *wrapper = (lvgl_anim_cb_wrapper_t *)var->ext_data.data;
        if (!wrapper) {
            return 0;
        }

        int32_t (* path_cb)(const lv_anim_t *var);
        if (wrapper && wrapper->module_inst && !esp_addr_executable(wrapper->path_cb)) {
            wasm_module_inst_t module_inst = wrapper->module_inst;
            uint32_t wasm_anim = addr_native_to_app((void *)var);

            ESP_LOGD(TAG, "lvgl_anim_path_cb_wrapper: %p -> %"PRIu32"", var, wasm_anim);

            argv[0] = wasm_anim ? wasm_anim : (uint32_t)var;

            ret = lvgl_run_wasm(wrapper->module_inst, wrapper->path_cb, 1, argv);

            return ret ? argv[0] : 0;
        } else {
            path_cb = (int32_t (*)(const lv_anim_t *))wrapper->path_cb;
            return path_cb(var);
        }
    }

    return -1;
}

static void lvgl_anim_cb_wrapper_destructor(void *ext_data)
{
    free(ext_data);
}

static lv_image_dsc_t *lvgl_clone_img_desc(wasm_exec_env_t exec_env, const void *_dsc)
{
    lv_image_dsc_t *res;
    lv_image_dsc_t *dsc;
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (ptr_is_in_ram_or_rom(_dsc)) {
        return (lv_image_dsc_t *)_dsc;
    }

    dsc = (lv_image_dsc_t *)addr_app_to_native((uint32_t)_dsc);
    if (!dsc) {
        ESP_LOGE(TAG, "failed to map _dsc=%p", _dsc);
        return NULL;
    }

    res = calloc(1, sizeof(lv_image_dsc_t));
    if (!res) {
        ESP_LOGE(TAG, "failed to allocate memory for res");
        return NULL;
    }

    memcpy(res, dsc, sizeof(lv_image_dsc_t));
    res->data = (const uint8_t *)addr_app_to_native((uint32_t)dsc->data);
    if (!res->data) {
        free(res);
        ESP_LOGE(TAG, "failed to map dsc->data=%p", dsc->data);
        return NULL;
    }

    return res;
}

static uint32_t lv_selector_revert(lv_style_selector_t selector)
{
    if (selector == ESP_BROOKESIA_SELECTOR_TRANS_VALUE) {
        selector = 0;
    }

    return selector;
}

static void lv_img_dsc_destructor(void *ext_data)
{
    free(ext_data);
}

static void lv_imagebutton_dsc_destructor(void *ext_data)
{
    if (ext_data) {
        free(ext_data);
    }
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_display_get_next)
{
    lv_display_t *res;
    lvgl_native_return_type(lv_display_t *);
    lvgl_native_get_arg(lv_display_t *, disp);

    res = lv_display_get_next(disp);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_display_get_default)
{
    lv_display_t *res;
    lvgl_native_return_type(lv_display_t *);

    res = lv_display_get_default();

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_display_get_screen_active)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_display_t *, disp);

    res = lv_display_get_screen_active(disp);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_display_get_horizontal_resolution)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(lv_display_t *, disp);

    res = lv_display_get_horizontal_resolution(disp);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_display_get_vertical_resolution)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(lv_display_t *, disp);

    res = lv_display_get_vertical_resolution(disp);

    lvgl_native_set_return(res);
}

static void lvgl_subject_cb_wrapper_destructor(void *ext_data)
{
    free(ext_data);
}

static void lvgl_subject_cb_wrapper(lv_observer_t *observer, lv_subject_t *subject)
{
    uint32_t argv[2];

    if (observer && observer->cb && subject && subject->ext_data.data) {
        lvgl_cb_wrapper_t *wrapper = (lvgl_cb_wrapper_t *)subject->ext_data.data;
        void (* observer_cb)(lv_observer_t *observer, lv_subject_t *subject);
        if (wrapper && wrapper->module_inst && wrapper->event_cb && !esp_addr_executable(wrapper->event_cb)) {
            argv[0] = (uint32_t)observer;
            argv[1] = (uint32_t)subject;
            lvgl_run_wasm(wrapper->module_inst, wrapper->event_cb, 2, argv);
        } else {
            if (wrapper && wrapper->event_cb) {
                observer_cb = (void *)wrapper->event_cb;
                observer_cb(observer, subject);
            }
        }
    }
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_subject_add_observer_obj)
{
    lv_observer_t *res = NULL;
    lvgl_native_return_type(lv_observer_t *);

    lvgl_native_get_arg(lv_subject_t *, subject);
    lvgl_native_get_arg(lv_observer_cb_t, cb);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(void *, user_data);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (!subject->ext_data.data) {
        lvgl_cb_wrapper_t *wrapper = calloc(1, sizeof(lvgl_cb_wrapper_t));
        if (wrapper) {
            wrapper->event_cb = (uint32_t)cb;
            wrapper->module_inst = module_inst;
            lv_subject_set_external_data(subject, wrapper, lvgl_subject_cb_wrapper_destructor);
            res = lv_subject_add_observer_obj(subject, lvgl_subject_cb_wrapper, obj, user_data);
            if (!res) {
                free(wrapper);
            }
        }
    } else {
        if (!cb) {
            res = lv_subject_add_observer_obj(subject, NULL, obj, user_data);
            if (res) {
                free(subject->ext_data.data);
                subject->ext_data.data = NULL;
            }
        }
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_remove_style)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_remove_style(obj, style, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_bg_opa)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_opa_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_bg_opa(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_pos)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, x);
    lvgl_native_get_arg(int32_t, y);

    lv_obj_set_pos(obj, x, y);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_align_to)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_obj_t *, base);
    lvgl_native_get_arg(lv_align_t, align);
    lvgl_native_get_arg(int32_t, x_ofs);
    lvgl_native_get_arg(int32_t, y_ofs);

    lv_obj_align_to(obj, base, align, x_ofs, y_ofs);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, parent);

    res = lv_obj_create(parent);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_width)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_obj_get_width(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_height)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_obj_get_height(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_size)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, w);
    lvgl_native_get_arg(int32_t, h);

    lv_obj_set_size(obj, w, h);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_align)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_align_t, align);
    lvgl_native_get_arg(int32_t, x_ofs);
    lvgl_native_get_arg(int32_t, y_ofs);

    lv_obj_align(obj, align, x_ofs, y_ofs);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_update_layout)
{
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_obj_update_layout(obj);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_clean)
{
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_obj_clean(obj);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_flex_flow)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_flex_flow_t, flow);

    lv_obj_set_flex_flow(obj, flow);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_content_width)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_obj_get_content_width(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_width)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, w);

    lv_obj_set_width(obj, w);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_line_color)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, color_packed);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_color_t value = {
        .blue = color_packed & 0xFF,
        .green = (color_packed >> 8) & 0xFF,
        .red = (color_packed >> 16) & 0xFF
    };

    lv_obj_set_style_line_color(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_arc_color)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, color_packed);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_color_t value = {
        .blue = color_packed & 0xFF,
        .green = (color_packed >> 8) & 0xFF,
        .red = (color_packed >> 16) & 0xFF
    };

    lv_obj_set_style_arc_color(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_image_recolor)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, color_packed);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_color_t value = {
        .blue = color_packed & 0xFF,
        .green = (color_packed >> 8) & 0xFF,
        .red = (color_packed >> 16) & 0xFF
    };

    lv_obj_set_style_image_recolor(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_text_color)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, color_packed);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_color_t value = {
        .blue = color_packed & 0xFF,
        .green = (color_packed >> 8) & 0xFF,
        .red = (color_packed >> 16) & 0xFF
    };

    lv_obj_set_style_text_color(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_x)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, x);

    lv_obj_set_x(obj, x);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_y)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, y);

    lv_obj_set_y(obj, y);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_add_style)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_obj_add_style: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_obj_add_style(obj, (const lv_style_t *)style, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_bg_color)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, color_packed);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_color_t value = {
        .blue = color_packed & 0xFF,
        .green = (color_packed >> 8) & 0xFF,
        .red = (color_packed >> 16) & 0xFF
    };

    lv_obj_set_style_bg_color(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_border_color)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, color_packed);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_color_t value = {
        .blue = color_packed & 0xFF,
        .green = (color_packed >> 8) & 0xFF,
        .red = (color_packed >> 16) & 0xFF
    };

    lv_obj_set_style_border_color(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_shadow_color)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, color_packed);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_color_t value = {
        .blue = color_packed & 0xFF,
        .green = (color_packed >> 8) & 0xFF,
        .red = (color_packed >> 16) & 0xFF
    };

    lv_obj_set_style_shadow_color(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_label_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, parent);

    res = lv_label_create(parent);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_label_set_text)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const char *, txt);

    const void *orig_txt = txt;
    txt = (const char *)map_ptr(exec_env, (const void *)txt);
    if (!txt) {
        ESP_LOGE(TAG, "lv_label_set_text: map_ptr failed for txt=%p", orig_txt);
        return;
    }

    lv_label_set_text(obj, txt);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_table_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, parent);

    res = lv_table_create(parent);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_table_set_column_count)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, col_cnt);

    lv_table_set_column_count(obj, col_cnt);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_table_set_column_width)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, col_id);
    lvgl_native_get_arg(int32_t, w);

    lv_table_set_column_width(obj, col_id, w);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_table_set_cell_ctrl)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, row);
    lvgl_native_get_arg(uint32_t, col);
    lvgl_native_get_arg(lv_table_cell_ctrl_t, ctrl);

    lv_table_set_cell_ctrl(obj, row, col, ctrl);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_table_set_cell_value)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, row);
    lvgl_native_get_arg(uint32_t, col);
    lvgl_native_get_arg(const char *, txt);

    const void *orig_txt = txt;
    txt = (const char *)map_ptr(exec_env, (const void *)txt);
    if (!txt) {
        ESP_LOGE(TAG, "lv_table_set_cell_value: map_ptr failed for txt=%p", orig_txt);
        return;
    }

    lv_table_set_cell_value(obj, row, col, txt);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_table_set_row_count)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, row_cnt);

    lv_table_set_row_cnt(obj, row_cnt);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_timer_create)
{
    lv_timer_t *res = NULL;
    lvgl_native_return_type(lv_timer_t *);
    lvgl_native_get_arg(lv_timer_cb_t, timer_xcb);
    lvgl_native_get_arg(uint32_t, period);
    lvgl_native_get_arg(void *, user_data);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    lvgl_cb_wrapper_t *wrapper = calloc(1, sizeof(lvgl_cb_wrapper_t));
    if (wrapper) {
        wrapper->event_cb = (uint32_t)timer_xcb;
        wrapper->module_inst = module_inst;

        res = lv_timer_create(lvgl_timer_cb_wrapper, period, user_data);
        if (res) {
            lv_timer_set_external_data(res, wrapper, lvgl_timer_cb_wrapper_destructor);
        } else {
            free(wrapper);
        }
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_timer_set_repeat_count)
{
    lvgl_native_get_arg(lv_timer_t *, timer);
    lvgl_native_get_arg(int32_t, repeat_count);

    lv_timer_set_repeat_count(timer, repeat_count);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_init)
{
    lvgl_native_get_arg(lv_style_t *, style);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_init: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_style_init(style);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_reset)
{
    lvgl_native_get_arg(lv_style_t *, style);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_reset: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_style_reset(style);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_bg_opa)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_opa_t, value);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_bg_opa: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_style_set_bg_opa(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_radius)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(int32_t, value);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_radius: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_style_set_radius(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_border_width)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(int32_t, value);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_border_width: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_style_set_border_width(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_border_opa)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_opa_t, value);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_border_opa: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_style_set_border_opa(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_border_side)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_border_side_t, value);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_border_side: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_style_set_border_side(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_shadow_opa)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_opa_t, value);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_shadow_opa: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_style_set_shadow_opa(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_shadow_width)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(int32_t, value);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_shadow_width: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_style_set_shadow_width(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_shadow_offset_x)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(int32_t, value);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_shadow_offset_x: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_style_set_shadow_offset_x(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_shadow_offset_y)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(int32_t, value);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_shadow_offset_y: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_style_set_shadow_offset_y(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_shadow_spread)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(int32_t, value);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_shadow_spread: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_style_set_shadow_spread(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_image_opa)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_opa_t, value);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_image_opa: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_style_set_image_opa(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_image_recolor_opa)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_opa_t, value);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_image_recolor_opa: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_style_set_image_recolor_opa(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_text_font)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(const lv_font_t *, font);

    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);

    if (!ptr_is_in_ram_or_rom(font)) {
        ESP_LOGE(TAG, "Wrong font addr %p", font);
        LVGL_TRACE_ABORT();
    }

    lv_style_set_text_font(style, font);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_text_opa)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_opa_t, value);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_text_opa: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_style_set_text_opa(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_line_width)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(int32_t, value);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_line_width: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_style_set_line_width(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_line_opa)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_opa_t, value);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_line_opa: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_style_set_line_opa(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_arc_width)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(int32_t, value);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_arc_width: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_style_set_arc_width(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_arc_opa)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_opa_t, value);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_arc_opa: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_style_set_arc_opa(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_blend_mode)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_blend_mode_t, value);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_blend_mode: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_style_set_blend_mode(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_text_color)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(uint32_t, color_packed);

    void *orig_style = style;
    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_text_color: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_color_t value = {
        .blue = color_packed & 0xFF,
        .green = (color_packed >> 8) & 0xFF,
        .red = (color_packed >> 16) & 0xFF
    };

    lv_style_set_text_color(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_line_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, parent);

    res = lv_line_create(parent);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_line_set_points)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const lv_point_precise_t *, points);
    lvgl_native_get_arg(uint32_t, point_num);

    points = (const lv_point_precise_t *)map_ptr(exec_env, (const void *)points);

    lv_line_set_points(obj, (const lv_point_precise_t *)points, point_num);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_arc_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, parent);

    res = lv_arc_create(parent);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_arc_set_start_angle)
{
    lvgl_native_get_arg(lv_obj_t *, arc);
    lvgl_native_get_arg(lv_value_precise_t, start);

    lv_arc_set_start_angle(arc, start);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_arc_set_end_angle)
{
    lvgl_native_get_arg(lv_obj_t *, arc);
    lvgl_native_get_arg(lv_value_precise_t, end);

    lv_arc_set_end_angle(arc, end);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_image_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, parent);

    parent = map_ptr(exec_env, parent);
    res = lv_image_create(parent);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_image_set_src)
{
    lv_image_dsc_t *res = NULL;
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_image_dsc_t *, src);

    res = lvgl_clone_img_desc(exec_env, (const void *)src);
    if (res) {
        lv_image_set_src(obj, res);
        lv_obj_set_external_data(obj, res, lv_img_dsc_destructor);
    }
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_image_set_rotation)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, angle);

    lv_image_set_rotation(obj, angle);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_image_set_scale)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, zoom);

    lv_image_set_scale(obj, zoom);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_image_set_antialias)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(bool, antialias);

    lv_image_set_antialias(obj, antialias);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_init)
{
    lvgl_native_get_arg(lv_anim_t *, a);

    a = (lv_anim_t *)map_ptr(exec_env, (const void *)a);

    lv_anim_init(a);

}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_start)
{
    lvgl_anim_cb_wrapper_t *wrapper = NULL;
    lv_anim_t *res = NULL;
    lvgl_native_return_type(lv_anim_t *);
    lvgl_native_get_arg(lv_anim_t *, a);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    a = (lv_anim_t *)map_ptr(exec_env, (const void *)a);

#if CONFIG_WASMACHINE_WASM_EXT_NATIVE_LVGL_USE_WASM_HEAP
    vTaskSetThreadLocalStoragePointer(NULL, LVGL_WASM_TASK_LOCAL_STORAGE_INDEX, module_inst);
#endif

    wrapper = calloc(1, sizeof(lvgl_anim_cb_wrapper_t));
    if (wrapper) {
        lv_anim_t a_new;
        memcpy(&a_new, a, sizeof(lv_anim_t));
        if (a->exec_cb || a->custom_exec_cb) {
            if (a->exec_cb) {
                wrapper->cb_type = 0;
                wrapper->custom_exec_cb = (uint32_t)a->exec_cb;
                a->custom_exec_cb = (lv_anim_custom_exec_cb_t)a->exec_cb;
                a_new.exec_cb = (lv_anim_exec_xcb_t)NULL;
            } else {
                wrapper->cb_type = 1;
                wrapper->custom_exec_cb = (uint32_t)a->custom_exec_cb;
            }

            a_new.custom_exec_cb = lvgl_anim_custom_exec_cb_wrapper;
        }

        if (a->start_cb) {
            wrapper->start_cb = (uint32_t)a->start_cb;
            a_new.start_cb = lvgl_anim_start_cb_wrapper;
        }

        if (a->deleted_cb) {
            wrapper->deleted_cb = (uint32_t)a->deleted_cb;
            a_new.deleted_cb = lvgl_anim_deleted_cb_wrapper;
        }

        if (a->completed_cb) {
            wrapper->ready_cb = (uint32_t)a->completed_cb;
            a_new.completed_cb = lvgl_anim_ready_cb_wrapper;
        }

        if (a->get_value_cb) {
            wrapper->get_value_cb = (uint32_t)a->get_value_cb;
            a_new.get_value_cb = lvgl_anim_get_value_cb_wrapper;
        }

        if (a->path_cb) {
            wrapper->path_cb = (uint32_t)a->path_cb;
            a_new.path_cb = lvgl_anim_path_cb_wrapper;
        }

        wrapper->module_inst = module_inst;

        lv_anim_set_external_data(&a_new, wrapper, lvgl_anim_cb_wrapper_destructor);

        res = lv_anim_start(&a_new);
        if (!res) {
            free(wrapper);
        }
    }

#if CONFIG_WASMACHINE_WASM_EXT_NATIVE_LVGL_USE_WASM_HEAP
    vTaskSetThreadLocalStoragePointer(NULL, LVGL_WASM_TASK_LOCAL_STORAGE_INDEX, NULL);
#endif

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_theme_get_font_small)
{
    const lv_font_t *res;
    lvgl_native_return_type(const lv_font_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_theme_get_font_small(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_theme_get_font_normal)
{
    const lv_font_t *res;
    lvgl_native_return_type(const lv_font_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_theme_get_font_normal(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_theme_get_font_large)
{
    const lv_font_t *res;
    lvgl_native_return_type(const lv_font_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_theme_get_font_large(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_theme_default_init)
{
    lv_theme_t *res;
    lvgl_native_return_type(lv_theme_t *);
    lvgl_native_get_arg(lv_display_t *, disp);
    lvgl_native_get_arg(uint32_t, color_primary_packed);
    lvgl_native_get_arg(uint32_t, color_secondary_packed);
    lvgl_native_get_arg(bool, dark);
    lvgl_native_get_arg(const lv_font_t *, font);

    if (!ptr_is_in_ram_or_rom(font)) {
        ESP_LOGE(TAG, "Wrong font addr %p", font);
        LVGL_TRACE_ABORT();
    }

    lv_color_t color_primary = {
        .blue = color_primary_packed & 0xFF,
        .green = (color_primary_packed >> 8) & 0xFF,
        .red = (color_primary_packed >> 16) & 0xFF
    };

    lv_color_t color_secondary = {
        .blue = color_secondary_packed & 0xFF,
        .green = (color_secondary_packed >> 8) & 0xFF,
        .red = (color_secondary_packed >> 16) & 0xFF
    };

    res = lv_theme_default_init(disp, color_primary, color_secondary, dark, font);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_theme_get_color_primary)
{
    lv_color_t res;
    lvgl_native_return_type(uint32_t);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_theme_get_color_primary(obj);

    lvgl_native_set_return((uint32_t)((res.red << 16) | (res.green << 8) | res.blue));
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_font_get_bitmap_fmt_txt)
{
    const void *res;
    lvgl_native_return_type(const void *);
    lvgl_native_get_arg(lv_font_glyph_dsc_t *, g_dsc);
    lvgl_native_get_arg(lv_draw_buf_t *, draw_buf);

    g_dsc = (lv_font_glyph_dsc_t *)map_ptr(exec_env, (const void *)g_dsc);
    draw_buf = (lv_draw_buf_t *)map_ptr(exec_env, (const void *)draw_buf);

    res = lv_font_get_bitmap_fmt_txt(g_dsc, draw_buf);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_font_get_glyph_dsc_fmt_txt)
{
    bool res;
    lvgl_native_return_type(bool);
    lvgl_native_get_arg(const lv_font_t *, font);
    lvgl_native_get_arg(lv_font_glyph_dsc_t *, dsc_out);
    lvgl_native_get_arg(uint32_t, unicode_letter);
    lvgl_native_get_arg(uint32_t, unicode_letter_next);

    dsc_out = (lv_font_glyph_dsc_t *)map_ptr(exec_env, (const void *)dsc_out);
    if (!ptr_is_in_ram_or_rom(font)) {
        ESP_LOGE(TAG, "Wrong font addr %p", font);
        LVGL_TRACE_ABORT();
    }

    res = lv_font_get_glyph_dsc_fmt_txt(font, dsc_out, unicode_letter, unicode_letter_next);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_palette_main)
{
    lv_color_t res;
    lvgl_native_return_type(uint32_t);
    lvgl_native_get_arg(lv_palette_t, p);

    res = lv_palette_main(p);

    lvgl_native_set_return((uint32_t)((res.red << 16) | (res.green << 8) | res.blue));
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_tabview_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, parent);

    res = lv_tabview_create(parent);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_text_font)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const lv_font_t *, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    value = (const lv_font_t *)map_ptr(exec_env, (const void *)value);

    lv_obj_set_style_text_font(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_tabview_get_tab_bar)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_tabview_get_tab_bar(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_pad_left)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_pad_left(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_tabview_add_tab)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, tv);
    lvgl_native_get_arg(const char *, name);

    name = (const char *)map_ptr(exec_env, (const void *)name);

    res = lv_tabview_add_tab(tv, name);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_height)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);

    lv_obj_set_height(obj, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_label_set_long_mode)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_label_long_mode_t, value);

    lv_label_set_long_mode(obj, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_button_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_button_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_add_state)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_state_t, value);

    lv_obj_add_state(obj, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_keyboard_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_keyboard_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_add_flag)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_obj_flag_t, value);

    lv_obj_add_flag(obj, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_textarea_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_textarea_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_textarea_set_one_line)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(bool, value);

    lv_textarea_set_one_line(obj, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_textarea_set_placeholder_text)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const char *, value);

    value = (const char *)map_ptr(exec_env, (const void *)value);

    lv_textarea_set_placeholder_text(obj, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_add_event_cb)
{
    lvgl_cb_wrapper_t *wrapper;
    lv_event_dsc_t *res = NULL;
    lvgl_native_return_type(struct _lv_event_dsc_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_event_cb_t, event_cb);
    lvgl_native_get_arg(lv_event_code_t, filter);
    lvgl_native_get_arg(void *, user_data);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    wrapper = calloc(1, sizeof(lvgl_cb_wrapper_t));
    if (wrapper) {
        res = lv_obj_add_event_cb(obj, lvgl_event_cb_wrapper, filter, user_data);
        if (res) {
            wrapper->event_cb = (uint32_t)event_cb;
            wrapper->module_inst = module_inst;
            lv_event_desc_set_external_data(res, wrapper, lvgl_event_wrapper_destructor);
        } else {
            free(wrapper);
        }
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_textarea_set_password_mode)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(bool, value);

    lv_textarea_set_password_mode(obj, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_dropdown_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_dropdown_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_dropdown_set_options_static)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const char *, value);

    value = (const char *)map_ptr(exec_env, (const void *)value);

    lv_dropdown_set_options_static(obj, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_slider_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_slider_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_refresh_ext_draw_size)
{
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_obj_refresh_ext_draw_size(obj);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_switch_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_switch_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_grid_dsc_array)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const int32_t *, col_dsc);
    lvgl_native_get_arg(const int32_t *, row_dsc);

    col_dsc = map_ptr(exec_env, col_dsc);
    row_dsc = map_ptr(exec_env, row_dsc);

    lv_obj_set_grid_dsc_array(obj, col_dsc, row_dsc);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_grid_cell)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_grid_align_t, x_align);
    lvgl_native_get_arg(int32_t, col_pos);
    lvgl_native_get_arg(int32_t, col_span);
    lvgl_native_get_arg(lv_grid_align_t, y_align);
    lvgl_native_get_arg(int32_t, row_pos);
    lvgl_native_get_arg(int32_t, row_span);

    lv_obj_set_grid_cell(obj, x_align, col_pos, col_span, y_align, row_pos, row_span);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_text_align)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_text_align_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_text_align(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_flex_grow)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint8_t, value);

    lv_obj_set_flex_grow(obj, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_max_height)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_max_height(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_chart_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_group_get_default)
{
    lv_group_t *res;
    lvgl_native_return_type(lv_group_t *);

    res = lv_group_get_default();

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_group_add_obj)
{
    lvgl_native_get_arg(lv_group_t *, group);
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_group_add_obj(group, obj);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_set_div_line_count)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint8_t, hdiv);
    lvgl_native_get_arg(uint8_t, vdiv);

    lv_chart_set_div_line_count(obj, hdiv, vdiv);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_set_point_count)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, value);

    lv_chart_set_point_count(obj, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_border_side)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_border_side_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_border_side(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_radius)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_border_side_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_radius(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_add_series)
{
    lv_chart_series_t *res;
    lvgl_native_return_type(lv_chart_series_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, color_packed);
    lvgl_native_get_arg(lv_chart_axis_t, axis);

    lv_color_t color = {
        .blue = color_packed & 0xFF,
        .green = (color_packed >> 8) & 0xFF,
        .red = (color_packed >> 16) & 0xFF
    };

    res = lv_chart_add_series(obj, color, axis);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_rand)
{
    uint32_t res;
    lvgl_native_return_type(uint32_t);
    lvgl_native_get_arg(uint32_t, min);
    lvgl_native_get_arg(uint32_t, max);

    res = lv_rand(min, max);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_set_next_value)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_chart_series_t *, ser);
    lvgl_native_get_arg(int32_t, value);

    lv_chart_set_next_value(obj, ser, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_set_type)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_chart_type_t, type);

    lv_chart_set_type(obj, type);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_pad_row)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_pad_row(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_pad_column)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_pad_column(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_palette_lighten)
{
    lv_color_t res;
    lvgl_native_return_type(uint32_t);
    lvgl_native_get_arg(lv_palette_t, p);
    lvgl_native_get_arg(uint8_t, lvl);

    res = lv_palette_lighten(p, lvl);

    lvgl_native_set_return((uint32_t)((res.red << 16) | (res.green << 8) | res.blue));
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_parent)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_obj_get_parent(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_pad_right)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_pad_right(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_width)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_width(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_height)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_height(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_palette_darken)
{
    lv_color_t res;
    lvgl_native_return_type(uint32_t);
    lvgl_native_get_arg(lv_palette_t, p);
    lvgl_native_get_arg(uint8_t, lvl);

    res = lv_palette_darken(p, lvl);

    lvgl_native_set_return((uint32_t)((res.red << 16) | (res.green << 8) | res.blue));
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_outline_color)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, color_packed);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_color_t value = {
        .blue = color_packed & 0xFF,
        .green = (color_packed >> 8) & 0xFF,
        .red = (color_packed >> 16) & 0xFF
    };

    lv_obj_set_style_outline_color(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_outline_width)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_outline_width(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_pad_bottom)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_pad_bottom(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_display_get_dpi)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(const lv_display_t *, disp);

    res = lv_display_get_dpi(disp);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_checkbox_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_checkbox_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_checkbox_set_text)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const char *, value);

    value = (const char *)map_ptr(exec_env, (const void *)value);

    lv_checkbox_set_text(obj, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_flex_align)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_flex_align_t, main_place);
    lvgl_native_get_arg(lv_flex_align_t, cross_place);
    lvgl_native_get_arg(lv_flex_align_t, track_cross_place);

    lv_obj_set_flex_align(obj, main_place, cross_place, track_cross_place);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_opa)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_opa_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_opa(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_remove_flag)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_obj_flag_t, f);

    lv_obj_remove_flag(obj, f);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_pad_top)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_pad_top(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_shadow_width)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_shadow_width(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_bg_image_src)
{
    lv_image_dsc_t *res = NULL;
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_image_dsc_t *, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    if (selector) {
        selector = lv_selector_revert(selector);
        res = lvgl_clone_img_desc(exec_env, (const void *)value);
        if (res) {
            lv_obj_set_external_data(obj, res, lv_img_dsc_destructor);
            lv_obj_set_style_bg_image_src(obj, res, selector);
        }
    } else {
        res = (lv_image_dsc_t *)map_ptr(exec_env, (const void *)value);
        lv_obj_set_style_bg_image_src(obj, res, selector);
    }
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_event_get_code)
{
    lv_event_code_t res;
    lvgl_native_return_type(lv_event_code_t);
    lvgl_native_get_arg(lv_event_t *, e);

    res = lv_event_get_code(e);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_event_get_target)
{
    void *res;
    lvgl_native_return_type(void *);
    lvgl_native_get_arg(lv_event_t *, e);

    e = map_ptr(exec_env, e);

    res = lv_event_get_target(e);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_event_get_user_data)
{
    void *res;
    lvgl_native_return_type(void *);
    lvgl_native_get_arg(lv_event_t *, e);

    e = map_ptr(exec_env, e);

    res = lv_event_get_user_data(e);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_indev_active)
{
    lv_indev_t *res;
    lvgl_native_return_type(lv_indev_t *);

    res = lv_indev_active();

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_indev_get_type)
{
    lv_indev_type_t res;
    lvgl_native_return_type(lv_indev_type_t);
    lvgl_native_get_arg(const lv_indev_t *, indev);

    res = lv_indev_get_type(indev);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_keyboard_set_textarea)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_obj_t *, ta);

    lv_keyboard_set_textarea(obj, ta);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_scroll_to_view_recursive)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_anim_enable_t, anim_en);

    lv_obj_scroll_to_view_recursive(obj, anim_en);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_indev_reset)
{
    lvgl_native_get_arg(lv_indev_t *, indev);
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_indev_reset(indev, obj);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_remove_state)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_state_t, state);

    lv_obj_remove_state(obj, state);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_display_get_layer_top)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_display_t *, disp);

    res = lv_display_get_layer_top(disp);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_calendar_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_calendar_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_calendar_set_month_shown)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, year);
    lvgl_native_get_arg(uint32_t, month);

    lv_calendar_set_month_shown(obj, year, month);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_calendar_add_header_dropdown)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_calendar_add_header_dropdown(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_event_get_param)
{
    void *res;
    lvgl_native_return_type(void *);
    lvgl_native_get_arg(lv_event_t *, e);

    res = lv_event_get_param(e);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_has_state)
{
    bool res;
    lvgl_native_return_type(bool);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_state_t, state);

    res = lv_obj_has_state(obj, state);
    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_bar_get_value)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_bar_get_value(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_text_get_size)
{
    lvgl_native_get_arg(lv_point_t *, size_res);
    lvgl_native_get_arg(const char *, text);
    lvgl_native_get_arg(const lv_font_t *, font);
    lvgl_native_get_arg(int32_t, letter_space);
    lvgl_native_get_arg(int32_t, line_space);
    lvgl_native_get_arg(int32_t, max_width);
    lvgl_native_get_arg(lv_text_flag_t, flag);

    size_res = map_ptr(exec_env, size_res);
    const void *orig_text = text;
    text = (const char *)map_ptr(exec_env, (const void *)text);
    if (!text) {
        ESP_LOGE(TAG, "lv_text_get_size: map_ptr failed for text=%p", orig_text);
        return;
    }

    if (!ptr_is_in_ram_or_rom(font)) {
        ESP_LOGE(TAG, "Wrong font addr %p", font);
        LVGL_TRACE_ABORT();
    }

    lv_text_get_size(size_res, text, font, letter_space, line_space, max_width, flag);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_rect_dsc_init)
{
    lvgl_native_get_arg(lv_draw_rect_dsc_t *, dsc);

    dsc = map_ptr(exec_env, dsc);

    lv_draw_rect_dsc_init(dsc);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_rect)
{
    lvgl_native_get_arg(lv_layer_t *, layer);
    lvgl_native_get_arg(const lv_draw_rect_dsc_t *, dsc);
    lvgl_native_get_arg(const lv_area_t *, coords);

    layer = map_ptr(exec_env, layer);
    dsc = map_ptr(exec_env, dsc);
    coords = map_ptr(exec_env, coords);

    lv_draw_rect(layer, dsc, coords);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_label_dsc_init)
{
    lvgl_native_get_arg(lv_draw_label_dsc_t *, dsc);

    dsc = map_ptr(exec_env, dsc);

    lv_draw_label_dsc_init(dsc);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_label)
{
    lvgl_native_get_arg(lv_layer_t *, layer);
    lvgl_native_get_arg(lv_draw_label_dsc_t *, dsc);
    lvgl_native_get_arg(const lv_area_t *, coords);

    layer = map_ptr(exec_env, layer);
    dsc = map_ptr(exec_env, dsc);
    coords = map_ptr(exec_env, coords);
    dsc->text = map_ptr(exec_env, dsc->text);
    if (!dsc->font) {
        dsc->font = &lv_font_montserrat_14;
    }

    lv_draw_label(layer, dsc, coords);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_event_get_current_target)
{
    void *res;
    lvgl_native_return_type(void *);
    lvgl_native_get_arg(lv_event_t *, e);

    res = lv_event_get_current_target(e);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_calendar_get_pressed_date)
{
    lv_result_t res;
    lvgl_native_return_type(lv_result_t);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_calendar_date_t *, data);

    data = map_ptr(exec_env, data);

    res = lv_calendar_get_pressed_date(obj, data);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_textarea_set_text)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const char *, value);

    value = (const char *)map_ptr(exec_env, (const void *)value);

    lv_textarea_set_text(obj, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_delete)
{
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_obj_delete(obj);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_invalidate)
{
    lvgl_native_get_arg(const lv_obj_t *, obj);

    lv_obj_invalidate(obj);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_get_type)
{
    lv_chart_type_t res;
    lvgl_native_return_type(lv_chart_type_t);
    lvgl_native_get_arg(const lv_obj_t *, obj);

    res = lv_chart_get_type(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_area_intersect)
{
    bool res;
    lvgl_native_return_type(bool);
    lvgl_native_get_arg(lv_area_t *, res_p);
    lvgl_native_get_arg(lv_area_t *, a1_p);
    lvgl_native_get_arg(lv_area_t *, a2_p);

    res_p = map_ptr(exec_env, res_p);
    a1_p = map_ptr(exec_env, a1_p);
    a2_p = map_ptr(exec_env, a2_p);

    res = lv_area_intersect(res_p, a1_p, a2_p);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_get_pressed_point)
{
    uint32_t res;
    lvgl_native_return_type(uint32_t);
    lvgl_native_get_arg(const lv_obj_t *, obj);

    res = lv_chart_get_pressed_point(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_get_series_next)
{
    lv_chart_series_t *res;
    lvgl_native_return_type(lv_chart_series_t *);
    lvgl_native_get_arg(const lv_obj_t *, chart);
    lvgl_native_get_arg(const lv_chart_series_t *, ser);

    res = lv_chart_get_series_next(chart, ser);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_child)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(const lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, id);

    res = lv_obj_get_child(obj, id);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_set_series_color)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_chart_series_t *, series);
    lvgl_native_get_arg(uint32_t, color_packed);

    lv_color_t color = {
        .blue = color_packed & 0xFF,
        .green = (color_packed >> 8) & 0xFF,
        .red = (color_packed >> 16) & 0xFF
    };

    lv_chart_set_series_color(obj, series, color);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_map)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(int32_t, x);
    lvgl_native_get_arg(int32_t, min_in);
    lvgl_native_get_arg(int32_t, max_in);
    lvgl_native_get_arg(int32_t, min_out);
    lvgl_native_get_arg(int32_t, max_out);

    res = lv_map(x, min_in, max_in, min_out, max_out);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_child_count)
{
    uint32_t res;
    lvgl_native_return_type(uint32_t);
    lvgl_native_get_arg(const lv_obj_t *, obj);

    res = lv_obj_get_child_count(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_mem_test)
{
    lv_result_t res;
    lvgl_native_return_type(lv_result_t);

    res = lv_mem_test();

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_mem_monitor)
{
    lvgl_native_get_arg(lv_mem_monitor_t *, mon_p);

    mon_p = map_ptr(exec_env, mon_p);

    lv_mem_monitor(mon_p);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_tabview_set_active)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, id);
    lvgl_native_get_arg(lv_anim_enable_t, anim_en);

    lv_tabview_set_active(obj, id, anim_en);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_delete_async)
{
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_obj_delete_async(obj);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_bar_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_bar_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_bar_set_range)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, min);
    lvgl_native_get_arg(int32_t, max);

    lv_bar_set_range(obj, min, max);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_bar_set_value)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_anim_enable_t, anim);

    lv_bar_set_value(obj, value, anim);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_bar_set_start_value)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, start_value);
    lvgl_native_get_arg(lv_anim_enable_t, anim);

    lv_bar_set_start_value(obj, start_value, anim);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_anim_duration)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_anim_duration(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_win_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_win_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_win_add_title)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const char *, txt);

    txt = (const char *)map_ptr(exec_env, (const void *)txt);

    res = lv_win_add_title(obj, txt);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_win_add_button)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const void *, icon);
    lvgl_native_get_arg(int32_t, btn_w);

    icon = map_ptr(exec_env, icon);

    res = lv_win_add_button(obj, icon, btn_w);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_win_get_content)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_win_get_content(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_keyboard_set_mode)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_keyboard_mode_t, mode);

    lv_keyboard_set_mode(obj, mode);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_dropdown_set_options)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const char *, options);

    options = (const char *)map_ptr(exec_env, (const void *)options);

    lv_dropdown_set_options(obj, options);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_dropdown_open)
{
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_dropdown_open(obj);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_dropdown_set_selected)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, sel_opt);

    lv_dropdown_set_selected(obj, sel_opt);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_roller_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_roller_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_roller_set_options)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const char *, options);
    lvgl_native_get_arg(lv_roller_mode_t, mode);

    options = (const char *)map_ptr(exec_env, (const void *)options);

    lv_roller_set_options(obj, options, mode);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_roller_set_selected)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, sel_opt);
    lvgl_native_get_arg(lv_anim_enable_t, anim);

    lv_roller_set_selected(obj, sel_opt, anim);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_msgbox_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_msgbox_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_tileview_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_tileview_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_tileview_add_tile)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint8_t, row_id);
    lvgl_native_get_arg(uint8_t, col_id);
    lvgl_native_get_arg(lv_dir_t, dir);

    res = lv_tileview_add_tile(obj, row_id, col_id, dir);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_tileview_set_tile_by_index)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, col_id);
    lvgl_native_get_arg(uint32_t, row_id);
    lvgl_native_get_arg(lv_anim_enable_t, anim_en);

    lv_tileview_set_tile_by_index(obj, col_id, row_id, anim_en);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_list_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_list_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_list_add_button)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const void *, icon);
    lvgl_native_get_arg(const char *, txt);

    icon = map_ptr(exec_env, icon);
    txt = (const char *)map_ptr(exec_env, (const void *)txt);

    res = lv_list_add_button(obj, icon, txt);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_scroll_to_view)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_anim_enable_t, anim_en);

    lv_obj_scroll_to_view(obj, anim_en);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_textarea_set_cursor_pos)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, pos);

    lv_textarea_set_cursor_pos(obj, pos);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_textarea_add_char)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, c);

    lv_textarea_add_char(obj, c);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_textarea_add_text)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const char *, txt);

    txt = (const char *)map_ptr(exec_env, (const void *)txt);

    lv_textarea_add_text(obj, txt);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_spinbox_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_spinbox_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_spinbox_set_digit_format)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, digit_count);
    lvgl_native_get_arg(uint32_t, separator_position);

    lv_spinbox_set_digit_format(obj, digit_count, separator_position);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_spinbox_set_value)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);

    lv_spinbox_set_value(obj, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_spinbox_set_step)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, value);

    lv_spinbox_set_step(obj, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_spinbox_increment)
{
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_spinbox_increment(obj);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_scroll_by)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, x);
    lvgl_native_get_arg(int32_t, y);
    lvgl_native_get_arg(lv_anim_enable_t, anim_en);

    lv_obj_scroll_by(obj, x, y, anim_en);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_msgbox_close)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lv_msgbox_close(obj);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_width)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(int32_t, value);

    style = map_ptr(exec_env, style);

    lv_style_set_width(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_bg_color)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(uint32_t, color_packed);

    void *orig_style = style;
    style = map_ptr(exec_env, style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_bg_color: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_color_t value = {
        .blue = color_packed & 0xFF,
        .green = (color_packed >> 8) & 0xFF,
        .red = (color_packed >> 16) & 0xFF
    };

    lv_style_set_bg_color(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_pad_right)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(int32_t, value);

    style = map_ptr(exec_env, style);

    lv_style_set_pad_right(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_grid_column_dsc_array)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(const int32_t *, value);

    style = map_ptr(exec_env, style);
    value = map_ptr(exec_env, value);

    lv_style_set_grid_column_dsc_array(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_grid_row_dsc_array)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(const int32_t *, value);

    style = map_ptr(exec_env, style);
    value = map_ptr(exec_env, value);

    lv_style_set_grid_row_dsc_array(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_grid_row_align)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_grid_align_t, value);

    style = map_ptr(exec_env, style);

    lv_style_set_grid_row_align(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_layout)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(uint16_t, value);

    style = map_ptr(exec_env, style);

    if (!value) {
        value = LV_LAYOUT_GRID;
    }

    lv_style_set_layout(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_index)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(const lv_obj_t *, obj);

    res = lv_obj_get_index(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_scroll_snap_y)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_scroll_snap_t, align);

    lv_obj_set_scroll_snap_y(obj, align);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_border_width)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_border_width(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_scroll_dir)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_dir_t, dir);

    lv_obj_set_scroll_dir(obj, dir);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_imagebutton_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_imagebutton_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_imagebutton_set_src)
{
    lv_image_dsc_t *res[3] = {NULL};
    lvgl_native_get_arg(lv_obj_t *, imgbtn);
    lvgl_native_get_arg(lv_imagebutton_state_t, state);
    lvgl_native_get_arg(const void *, src_left);
    lvgl_native_get_arg(const void *, src_mid);
    lvgl_native_get_arg(const void *, src_right);

    if ((int)state < 0 || (int)state >= LV_IMAGEBUTTON_STATE_NUM) {
        ESP_LOGE(TAG, "invalid state=%d", (int)state);
        return;
    }

    // Check if this button already has external data allocated for image descriptors
    // If it does and it's our destructor, reuse it; otherwise allocate new
    lv_image_dsc_t *button_dsc = NULL;
    if (imgbtn->ext_data.data && imgbtn->ext_data.free_cb == lv_imagebutton_dsc_destructor) {
        // External data already exists for this button, reuse it
        button_dsc = (lv_image_dsc_t *)imgbtn->ext_data.data;
    } else {
        // Allocate new descriptors for this button instance
        button_dsc = (lv_image_dsc_t *)calloc(1, 3 * LV_IMAGEBUTTON_STATE_NUM * sizeof(lv_image_dsc_t));
        if (!button_dsc) {
            ESP_LOGE(TAG, "failed to allocate memory for button descriptors");
            return;
        }

        // Associate the allocation with the button for automatic cleanup
        lv_obj_set_external_data(imgbtn, button_dsc, lv_imagebutton_dsc_destructor);
    }

    lv_image_dsc_t *dsc;
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (src_left) {
        dsc = (lv_image_dsc_t *)addr_app_to_native((uint32_t)src_left);
        if (!dsc) {
            ESP_LOGE(TAG, "failed to map _dsc=%p", src_left);
            return;
        }

        res[0] = &button_dsc[state];
        memcpy(res[0], dsc, sizeof(lv_image_dsc_t));

        res[0]->data = (const uint8_t *)addr_app_to_native((uint32_t)dsc->data);
        if (!res[0]->data) {
            ESP_LOGE(TAG, "failed to map dsc->data=%p", dsc->data);
            return;
        }
    }

    if (src_mid) {
        dsc = (lv_image_dsc_t *)addr_app_to_native((uint32_t)src_mid);
        if (!dsc) {
            ESP_LOGE(TAG, "failed to map _dsc=%p", src_mid);
            return;
        }

        res[1] = &button_dsc[LV_IMAGEBUTTON_STATE_NUM + state];
        memcpy(res[1], dsc, sizeof(lv_image_dsc_t));

        res[1]->data = (const uint8_t *)addr_app_to_native((uint32_t)dsc->data);
        if (!res[1]->data) {
            ESP_LOGE(TAG, "failed to map dsc->data=%p", dsc->data);
            return;
        }
    }

    if (src_right) {
        dsc = (lv_image_dsc_t *)addr_app_to_native((uint32_t)src_right);
        if (!dsc) {
            ESP_LOGE(TAG, "failed to map _dsc=%p", src_right);
            return;
        }

        res[2] = &button_dsc[2 * LV_IMAGEBUTTON_STATE_NUM + state];
        memcpy(res[2], dsc, sizeof(lv_image_dsc_t));

        res[2]->data = (const uint8_t *)addr_app_to_native((uint32_t)dsc->data);
        if (!res[2]->data) {
            ESP_LOGE(TAG, "failed to map dsc->data=%p", dsc->data);
            return;
        }
    }

    if (src_left || src_mid || src_right) {
        lv_imagebutton_set_src(imgbtn, state, res[0], res[1], res[2]);
    }
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_bg_grad_dir)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_grad_dir_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_bg_grad_dir(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_bg_grad_color)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, color_packed);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_color_t value = {
        .blue = color_packed & 0xFF,
        .green = (color_packed >> 8) & 0xFF,
        .red = (color_packed >> 16) & 0xFF
    };

    lv_obj_set_style_bg_grad_color(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_grid_row_align)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_grad_dir_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_grid_row_align(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_timer_pause)
{
    lvgl_native_get_arg(lv_timer_t *, timer);

    lv_timer_pause(timer);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_path_bounce)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(const lv_anim_t *, a);

    a = map_ptr(exec_env, a);

    res = lv_anim_path_bounce(a);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_fade_in)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, value);
    lvgl_native_get_arg(uint32_t, delay);

    lv_obj_fade_in(obj, value, delay);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_path_ease_out)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(const lv_anim_t *, a);

    a = map_ptr(exec_env, a);

    res = lv_anim_path_ease_out(a);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_move_to_index)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, index);

    lv_obj_move_to_index(obj, index);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_text_line_space)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_text_line_space(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_fade_out)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, time);
    lvgl_native_get_arg(uint32_t, delay);

    lv_obj_fade_out(obj, time, delay);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_timer_resume)
{
    lvgl_native_get_arg(lv_timer_t *, timer);

    lv_timer_resume(timer);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_path_linear)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(const lv_anim_t *, a);

    a = map_ptr(exec_env, a);

    res = lv_anim_path_linear(a);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_path_overshoot)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(lv_anim_t *, a);

    a = map_ptr(exec_env, a);

    res = lv_anim_path_overshoot(a);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_delete)
{
    bool res;
    lvgl_native_return_type(bool);
    lvgl_native_get_arg(void *, var);
    lvgl_native_get_arg(lv_anim_exec_xcb_t, exec_cb);

    res = lv_anim_delete(var, exec_cb);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_event_set_ext_draw_size)
{
    lvgl_native_get_arg(lv_event_t *, e);
    lvgl_native_get_arg(int32_t, size);

    lv_event_set_ext_draw_size(e, size);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_event_set_cover_res)
{
    lvgl_native_get_arg(lv_event_t *, e);
    lvgl_native_get_arg(lv_cover_res_t, value);

    lv_event_set_cover_res(e, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_style_prop)
{
    lv_style_value_t res;
    lvgl_native_return_type(lv_style_value_t);
    lvgl_native_get_arg(const lv_obj_t *, obj);
    lvgl_native_get_arg(lv_part_t, part);
    lvgl_native_get_arg(lv_style_prop_t, prop);

    res = lv_obj_get_style_prop(obj, part, prop);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_image_get_scale)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_image_get_scale(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_trigo_sin)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(int16_t, angle);

    res = lv_trigo_sin(angle);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_indev_get_gesture_dir)
{
    lv_dir_t res;
    lvgl_native_return_type(lv_dir_t);
    lvgl_native_get_arg(const lv_indev_t *, indev);

    res = lv_indev_get_gesture_dir(indev);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_path_ease_in)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(const lv_anim_t *, a);

    a = map_ptr(exec_env, a);

    res = lv_anim_path_ease_in(a);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_timer_get_user_data)
{
    void *res;
    lvgl_native_return_type(void *);
    lvgl_native_get_arg(lv_timer_t *, timer);

    res = timer->user_data;

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_data)
{
    int res = -1;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int, type);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    pdata = map_ptr(exec_env, pdata);

    if (type == LV_OBJ_COORDS && n == sizeof(obj->coords)) {
        memcpy(pdata, &obj->coords, sizeof(obj->coords));
        res = 0;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_rect_dsc_get_data)
{
    int res = -1;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(lv_draw_rect_dsc_t *, dsc);
    lvgl_native_get_arg(int, type);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    pdata = map_ptr(exec_env, pdata);

    if (type == LV_DRAW_RECT_DSC_BASE && n == sizeof(dsc->base)) {
        memcpy(pdata, &dsc->base, sizeof(dsc->base));
        res = 0;
    } else if (type == LV_DRAW_RECT_DSC_RADIUS && n == sizeof(dsc->radius)) {
        memcpy(pdata, &dsc->radius, sizeof(dsc->radius));
        res = 0;
    } else if (type == LV_DRAW_RECT_DSC_BG_OPA && n == sizeof(dsc->bg_opa)) {
        memcpy(pdata, &dsc->bg_opa, sizeof(dsc->bg_opa));
        res = 0;
    } else if (type == LV_DRAW_RECT_DSC_BG_COLOR && n == sizeof(dsc->bg_color)) {
        memcpy(pdata, &dsc->bg_color, sizeof(dsc->bg_color));
        res = 0;
    } else if (type == LV_DRAW_RECT_DSC_BG_GRAD && n == sizeof(dsc->bg_grad)) {
        memcpy(pdata, &dsc->bg_grad, sizeof(dsc->bg_grad));
        res = 0;
    } else if (type == LV_DRAW_RECT_DSC_BG_IMAGE_RECOLOR && n == sizeof(dsc->bg_image_recolor)) {
        memcpy(pdata, &dsc->bg_image_recolor, sizeof(dsc->bg_image_recolor));
        res = 0;
    } else if (type == LV_DRAW_RECT_DSC_BG_IMAGE_OPA && n == sizeof(dsc->bg_image_opa)) {
        memcpy(pdata, &dsc->bg_image_opa, sizeof(dsc->bg_image_opa));
        res = 0;
    } else if (type == LV_DRAW_RECT_DSC_BG_IMAGE_RECOLOR_OPA && n == sizeof(dsc->bg_image_recolor_opa)) {
        memcpy(pdata, &dsc->bg_image_recolor_opa, sizeof(dsc->bg_image_recolor_opa));
        res = 0;
    } else if (type == LV_DRAW_RECT_DSC_BG_IMAGE_TILED && n == sizeof(dsc->bg_image_tiled)) {
        memcpy(pdata, &dsc->bg_image_tiled, sizeof(dsc->bg_image_tiled));
        res = 0;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_rect_dsc_set_data)
{
    int res = 0;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(lv_draw_rect_dsc_t *, dsc);
    lvgl_native_get_arg(int, type);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    pdata = map_ptr(exec_env, pdata);

    if (type == LV_DRAW_RECT_DSC_BASE && n == sizeof(dsc->base)) {
        memcpy(&dsc->base, pdata, sizeof(dsc->base));
        res = 0;
    } else if (type == LV_DRAW_RECT_DSC_RADIUS && n == sizeof(dsc->radius)) {
        memcpy(&dsc->radius, pdata, sizeof(dsc->radius));
        res = 0;
    } else if (type == LV_DRAW_RECT_DSC_BG_OPA && n == sizeof(dsc->bg_opa)) {
        memcpy(&dsc->bg_opa, pdata, sizeof(dsc->bg_opa));
        res = 0;
    } else if (type == LV_DRAW_RECT_DSC_BG_COLOR && n == sizeof(dsc->bg_color)) {
        memcpy(&dsc->bg_color, pdata, sizeof(dsc->bg_color));
        res = 0;
    } else if (type == LV_DRAW_RECT_DSC_BG_GRAD && n == sizeof(dsc->bg_grad)) {
        memcpy(&dsc->bg_grad, pdata, sizeof(dsc->bg_grad));
        res = 0;
    } else if (type == LV_DRAW_RECT_DSC_BG_IMAGE_RECOLOR && n == sizeof(dsc->bg_image_recolor)) {
        memcpy(&dsc->bg_image_recolor, pdata, sizeof(dsc->bg_image_recolor));
        res = 0;
    } else if (type == LV_DRAW_RECT_DSC_BG_IMAGE_OPA && n == sizeof(dsc->bg_image_opa)) {
        memcpy(&dsc->bg_image_opa, pdata, sizeof(dsc->bg_image_opa));
        res = 0;
    } else if (type == LV_DRAW_RECT_DSC_BG_IMAGE_RECOLOR_OPA && n == sizeof(dsc->bg_image_recolor_opa)) {
        memcpy(&dsc->bg_image_recolor_opa, pdata, sizeof(dsc->bg_image_recolor_opa));
        res = 0;
    } else if (type == LV_DRAW_RECT_DSC_BG_IMAGE_TILED && n == sizeof(dsc->bg_image_tiled)) {
        memcpy(&dsc->bg_image_tiled, pdata, sizeof(dsc->bg_image_tiled));
        res = 0;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_font_get_font)
{
    const lv_font_t *res;
    lvgl_native_return_type(const lv_font_t *);
    lvgl_native_get_arg(int, type);

    switch (type) {
#if LV_FONT_MONTSERRAT_8
    case LV_FONT_MONTSERRAT_8_FONT:
        res = &lv_font_montserrat_8;
        break;
#endif
#if LV_FONT_MONTSERRAT_10
    case LV_FONT_MONTSERRAT_10_FONT:
        res = &lv_font_montserrat_10;
        break;
#endif
#if LV_FONT_MONTSERRAT_12
    case LV_FONT_MONTSERRAT_12_FONT:
        res = &lv_font_montserrat_12;
        break;
#endif
#if LV_FONT_MONTSERRAT_14
    case LV_FONT_MONTSERRAT_14_FONT:
        res = &lv_font_montserrat_14;
        break;
#endif
#if LV_FONT_MONTSERRAT_16
    case LV_FONT_MONTSERRAT_16_FONT:
        res = &lv_font_montserrat_16;
        break;
#endif
#if LV_FONT_MONTSERRAT_18
    case LV_FONT_MONTSERRAT_18_FONT:
        res = &lv_font_montserrat_18;
        break;
#endif
#if LV_FONT_MONTSERRAT_20
    case LV_FONT_MONTSERRAT_20_FONT:
        res = &lv_font_montserrat_20;
        break;
#endif
#if LV_FONT_MONTSERRAT_22
    case LV_FONT_MONTSERRAT_22_FONT:
        res = &lv_font_montserrat_22;
        break;
#endif
#if LV_FONT_MONTSERRAT_24
    case LV_FONT_MONTSERRAT_24_FONT:
        res = &lv_font_montserrat_24;
        break;
#endif
#if LV_FONT_MONTSERRAT_26
    case LV_FONT_MONTSERRAT_26_FONT:
        res = &lv_font_montserrat_26;
        break;
#endif
#if LV_FONT_MONTSERRAT_28
    case LV_FONT_MONTSERRAT_28_FONT:
        res = &lv_font_montserrat_28;
        break;
#endif
#if LV_FONT_MONTSERRAT_30
    case LV_FONT_MONTSERRAT_30_FONT:
        res = &lv_font_montserrat_30;
        break;
#endif
#if LV_FONT_MONTSERRAT_32
    case LV_FONT_MONTSERRAT_32_FONT:
        res = &lv_font_montserrat_32;
        break;
#endif
#if LV_FONT_MONTSERRAT_34
    case LV_FONT_MONTSERRAT_34_FONT:
        res = &lv_font_montserrat_34;
        break;
#endif
#if LV_FONT_MONTSERRAT_36
    case LV_FONT_MONTSERRAT_36_FONT:
        res = &lv_font_montserrat_36;
        break;
#endif
#if LV_FONT_MONTSERRAT_38
    case LV_FONT_MONTSERRAT_38_FONT:
        res = &lv_font_montserrat_38;
        break;
#endif
#if LV_FONT_MONTSERRAT_40
    case LV_FONT_MONTSERRAT_40_FONT:
        res = &lv_font_montserrat_40;
        break;
#endif
#if LV_FONT_MONTSERRAT_42
    case LV_FONT_MONTSERRAT_42_FONT:
        res = &lv_font_montserrat_42;
        break;
#endif
#if LV_FONT_MONTSERRAT_44
    case LV_FONT_MONTSERRAT_44_FONT:
        res = &lv_font_montserrat_44;
        break;
#endif
#if LV_FONT_MONTSERRAT_46
    case LV_FONT_MONTSERRAT_46_FONT:
        res = &lv_font_montserrat_46;
        break;
#endif
#if LV_FONT_MONTSERRAT_48
    case LV_FONT_MONTSERRAT_48_FONT:
        res = &lv_font_montserrat_48;
        break;
#endif
#if LV_FONT_MONTSERRAT_28_COMPRESSED
    case LV_FONT_MONTSERRAT_28_COMPRESSED_FONT:
        res = &lv_font_montserrat_28_compressed;
        break;
#endif
#if LV_FONT_MONTSERRAT_12_SUBPX
    case LV_FONT_MONTSERRAT_12_SUBPX_FONT:
        res = &lv_font_montserrat_12_subpx;
        break;
#endif
#if LV_FONT_UNSCII_8
    case LV_FONT_UNSCII_8_FONT:
        res = &lv_font_unscii_8;
        break;
#endif
#if LV_FONT_UNSCII_16
    case LV_FONT_UNSCII_16_FONT:
        res = &lv_font_unscii_16;
        break;
#endif
#if LV_FONT_DEJAVU_16_PERSIAN_HEBREW
    case LV_FONT_DEJAVU_16_PERSIAN_HEBREW_FONT:
        res = &lv_font_dejavu_16_persian_hebrew;
        break;
#endif
#if LV_FONT_SIMSUN_16_CJK
    case LV_FONT_SIMSUN_16_CJK_FONT:
        res = &lv_font_simsun_16_cjk;
        break;
#endif
#if LV_USE_DEMO_BENCHMARK
    case LV_FONT_BENCHMARK_MONTSERRAT_12_COMPR_AZ_FONT: {
        extern lv_font_t lv_font_benchmark_montserrat_12_aligned;

        res = &lv_font_benchmark_montserrat_12_aligned;
        break;
    }
    case LV_FONT_BENCHMARK_MONTSERRAT_14_COMPR_AZ_FONT: {
        extern lv_font_t lv_font_benchmark_montserrat_14_aligned;

        res = &lv_font_benchmark_montserrat_14_aligned;
        break;
    }
    case LV_FONT_BENCHMARK_MONTSERRAT_16_COMPR_AZ_FONT: {
        extern lv_font_t lv_font_benchmark_montserrat_16_aligned;

        res = &lv_font_benchmark_montserrat_16_aligned;
        break;
    }
    case LV_FONT_BENCHMARK_MONTSERRAT_18_COMPR_AZ_FONT: {
        extern lv_font_t lv_font_benchmark_montserrat_18_aligned;

        res = &lv_font_benchmark_montserrat_18_aligned;
        break;
    }
    case LV_FONT_BENCHMARK_MONTSERRAT_20_COMPR_AZ_FONT: {
        extern lv_font_t lv_font_benchmark_montserrat_20_aligned;

        res = &lv_font_benchmark_montserrat_20_aligned;
        break;
    }
    case LV_FONT_BENCHMARK_MONTSERRAT_24_COMPR_AZ_FONT: {
        extern lv_font_t lv_font_benchmark_montserrat_24_aligned;

        res = &lv_font_benchmark_montserrat_24_aligned;
        break;
    }
    case LV_FONT_BENCHMARK_MONTSERRAT_26_COMPR_AZ_FONT: {
        extern lv_font_t lv_font_benchmark_montserrat_26_aligned;

        res = &lv_font_benchmark_montserrat_26_aligned;
        break;
    }
#endif
    default:
        res = NULL;
        break;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_font_get_data)
{
    int res = -1;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(const lv_font_t *, font);
    lvgl_native_get_arg(int, type);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    if (!ptr_is_in_ram_or_rom(font)) {
        ESP_LOGE(TAG, "Wrong font addr %p", font);
        LVGL_TRACE_ABORT();
    }

    pdata = map_ptr(exec_env, pdata);

    if (type == LV_FONT_LINE_HEIGHT && n == sizeof(font->line_height)) {
        memcpy(pdata, &font->line_height, sizeof(font->line_height));
        res = 0;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_label_set_text_static)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const char *, text);

    text = (const char *)map_ptr(exec_env, (const void *)text);

    lv_label_set_text_static(obj, text);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_border_color)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(uint32_t, color_packed);

    void *orig_style = style;
    style = map_ptr(exec_env, style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_border_color: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_color_t value = {
        .blue = color_packed & 0xFF,
        .green = (color_packed >> 8) & 0xFF,
        .red = (color_packed >> 16) & 0xFF
    };

    lv_style_set_border_color(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_shadow_color)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(uint32_t, color_packed);

    void *orig_style = style;
    style = map_ptr(exec_env, style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_shadow_color: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_color_t value = {
        .blue = color_packed & 0xFF,
        .green = (color_packed >> 8) & 0xFF,
        .red = (color_packed >> 16) & 0xFF
    };

    lv_style_set_shadow_color(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_outline_color)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(uint32_t, color_packed);

    void *orig_style = style;
    style = map_ptr(exec_env, style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_outline_color: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_color_t value = {
        .blue = color_packed & 0xFF,
        .green = (color_packed >> 8) & 0xFF,
        .red = (color_packed >> 16) & 0xFF
    };

    lv_style_set_outline_color(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_outline_width)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(int32_t, value);

    style = map_ptr(exec_env, style);

    lv_style_set_outline_width(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_indev_get_next)
{
    lv_indev_t *res;
    lvgl_native_return_type(lv_indev_t *);
    lvgl_native_get_arg(lv_indev_t *, indev);

    res = lv_indev_get_next(indev);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_group_create)
{
    lv_group_t *res;
    lvgl_native_return_type(lv_group_t *);

    res = lv_group_create();

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_indev_set_group)
{
    lvgl_native_get_arg(lv_indev_t *, indev);
    lvgl_native_get_arg(lv_group_t *, group);

    lv_indev_set_group(indev, group);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_shadow_opa)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_opa_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_shadow_opa(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_indev_enable)
{
    lvgl_native_get_arg(lv_indev_t *, indev);
    lvgl_native_get_arg(bool, en);

    lv_indev_enable(indev, en);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_has_flag)
{
    bool res;
    lvgl_native_return_type(bool);
    lvgl_native_get_arg(const lv_obj_t *, obj);
    lvgl_native_get_arg(lv_obj_flag_t, f);

    res = lv_obj_has_flag(obj, f);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_arc_set_bg_angles)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_value_precise_t, start);
    lvgl_native_get_arg(lv_value_precise_t, end);

    lv_arc_set_bg_angles(obj, start, end);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_arc_set_value)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);

    lv_arc_set_value(obj, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_arc_width)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_arc_width(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_arc_set_rotation)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);

    lv_arc_set_rotation(obj, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_image_opa)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_opa_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_image_opa(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_timer_delete)
{
    lvgl_native_get_arg(lv_timer_t *, timer);

    s_lvgl_ops.lock(0);
    lv_timer_delete(timer);
    s_lvgl_ops.unlock();
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_user_data)
{
    void *res;
    lvgl_native_return_type(void *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_obj_get_user_data(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_user_data)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(void *, user_data);

    lv_obj_set_user_data(obj, user_data);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_scrollbar_mode)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_scrollbar_mode_t, mode);

    lv_obj_set_scrollbar_mode(obj, mode);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_group_remove_all_objs)
{
    lvgl_native_get_arg(lv_group_t *, group);

    lv_group_remove_all_objs(group);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_label_set_recolor)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(bool, en);

    lv_label_set_recolor(obj, en);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_tabview_get_tab_active)
{
    uint32_t res;
    lvgl_native_return_type(uint32_t);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_tabview_get_tab_active(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_shadow_offset_x)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_shadow_offset_x(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_shadow_offset_y)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_shadow_offset_y(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_led_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_led_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_led_off)
{
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_led_off(obj);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_led_on)
{
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_led_on(obj);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_click_area)
{
    lvgl_native_get_arg(const lv_obj_t *, obj);
    lvgl_native_get_arg(lv_area_t *, area);

    area = map_ptr(exec_env, area);

    lv_obj_get_click_area(obj, area);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_indev_set_button_points)
{
    lvgl_native_get_arg(lv_indev_t *, indev);
    lvgl_native_get_arg(const lv_point_t *, points);

    if (points) {
        points = map_ptr(exec_env, points);
    }

    lv_indev_set_button_points(indev, points);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_qrcode_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_qrcode_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_qrcode_update)
{
    lv_result_t res;
    lvgl_native_return_type(lv_result_t);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const void *, data);
    lvgl_native_get_arg(uint32_t, data_len);

    data = map_ptr(exec_env, data);

    res = lv_qrcode_update(obj, data, data_len);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_group_focus_obj)
{
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_group_focus_obj(obj);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_group_focus_freeze)
{
    lvgl_native_get_arg(lv_group_t *, group);
    lvgl_native_get_arg(bool, en);

    lv_group_focus_freeze(group, en);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_display_get_refr_timer)
{
    lv_timer_t *res;
    lvgl_native_return_type(lv_timer_t *);
    lvgl_native_get_arg(lv_display_t *, disp);

    res = lv_display_get_refr_timer(disp);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_timer_set_period)
{
    lvgl_native_get_arg(lv_timer_t *, timer);
    lvgl_native_get_arg(uint32_t, period);

    timer = map_ptr(exec_env, timer);

    lv_timer_set_period(timer, period);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_get_timer)
{
    lv_timer_t *res;
    lvgl_native_return_type(lv_timer_t *);

    res = lv_anim_get_timer();

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_timer_get_data)
{
    int res = -1;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(lv_timer_t *, anim_timer);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    pdata = map_ptr(exec_env, pdata);
    if (n == sizeof(anim_timer->period)) {
        memcpy(pdata, &anim_timer->period, sizeof(anim_timer->period));
        res = 0;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_style_opa_recursive)
{
    lv_opa_t res;
    lvgl_native_return_type(lv_opa_t);
    lvgl_native_get_arg(const lv_obj_t *, obj);
    lvgl_native_get_arg(lv_part_t, part);

    res = lv_obj_get_style_opa_recursive(obj, part);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_timer_ctx_get_data)
{
    int res = -1;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(timer_context_t *, timer_ctx);
    lvgl_native_get_arg(int, type);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    timer_ctx = map_ptr(exec_env, timer_ctx);
    pdata = map_ptr(exec_env, pdata);

    if (type == LV_TIMER_CTX_COUNT_VAL && n == sizeof(timer_ctx->count_val)) {
        memcpy(pdata, &timer_ctx->count_val, sizeof(timer_ctx->count_val));
        res = 0;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_timer_ctx_set_data)
{
    int res = 0;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(timer_context_t *, timer_ctx);
    lvgl_native_get_arg(int, type);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    timer_ctx = map_ptr(exec_env, timer_ctx);
    pdata = map_ptr(exec_env, pdata);

    if (type == LV_TIMER_CTX_COUNT_VAL && n == sizeof(timer_ctx->count_val)) {
        memcpy(&timer_ctx->count_val, pdata, sizeof(timer_ctx->count_val));
        res = 0;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_timer_ready)
{
    lvgl_native_get_arg(lv_timer_t *, timer);

    timer = map_ptr(exec_env, timer);

    lv_timer_ready(timer);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_screen_active)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);

    res = lv_screen_active();

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_tabview_set_tab_bar_size)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, size);

    lv_tabview_set_tab_bar_size(obj, size);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_set_var)
{
    lvgl_native_get_arg(lv_anim_t *, a);
    lvgl_native_get_arg(void *, var);

    a = (lv_anim_t *)map_ptr(exec_env, (const void *)a);
    lv_anim_set_var(a, var);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_set_duration)
{
    lvgl_native_get_arg(lv_anim_t *, a);
    lvgl_native_get_arg(uint32_t, duration);

    a = (lv_anim_t *)map_ptr(exec_env, (const void *)a);
    lv_anim_set_duration(a, duration);
}


DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_set_delay)
{
    lvgl_native_get_arg(lv_anim_t *, a);
    lvgl_native_get_arg(uint32_t, delay);

    a = (lv_anim_t *)map_ptr(exec_env, (const void *)a);
    lv_anim_set_delay(a, delay);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_delete_anim_completed_cb)
{
    lvgl_native_get_arg(lv_anim_t *, a);

    a = (lv_anim_t *)map_ptr(exec_env, (const void *)a);
    lv_obj_delete_anim_completed_cb(a);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_set_completed_cb)
{
    lvgl_native_get_arg(lv_anim_t *, a);
    lvgl_native_get_arg(lv_anim_completed_cb_t, completed_cb);

    a = (lv_anim_t *)map_ptr(exec_env, (const void *)a);
    lv_anim_set_completed_cb(a, completed_cb);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_set_exec_cb)
{
    lvgl_native_get_arg(lv_anim_t *, a);
    lvgl_native_get_arg(lv_anim_exec_xcb_t, exec_cb);

    a = (lv_anim_t *)map_ptr(exec_env, (const void *)a);
    lv_anim_set_exec_cb(a, exec_cb);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_set_path_cb)
{
    lvgl_native_get_arg(lv_anim_t *, a);
    lvgl_native_get_arg(lv_anim_path_cb_t, path_cb);

    a = (lv_anim_t *)map_ptr(exec_env, (const void *)a);
    lv_anim_set_path_cb(a, path_cb);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_set_values)
{
    lvgl_native_get_arg(lv_anim_t *, a);
    lvgl_native_get_arg(int32_t, start);
    lvgl_native_get_arg(int32_t, end);

    a = (lv_anim_t *)map_ptr(exec_env, (const void *)a);
    lv_anim_set_values(a, start, end);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_set_reverse_duration)
{
    lvgl_native_get_arg(lv_anim_t *, a);
    lvgl_native_get_arg(uint32_t, duration);

    a = (lv_anim_t *)map_ptr(exec_env, (const void *)a);
    lv_anim_set_reverse_duration(a, duration);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_set_repeat_count)
{
    lvgl_native_get_arg(lv_anim_t *, a);
    lvgl_native_get_arg(uint32_t, cnt);

    a = (lv_anim_t *)map_ptr(exec_env, (const void *)a);
    lv_anim_set_repeat_count(a, cnt);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_scale_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_scale_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_scale_set_mode)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_scale_mode_t, mode);

    lv_scale_set_mode(obj, mode);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_msgbox_add_title)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const char *, title);

    title = (const char *)map_ptr(exec_env, (const void *)title);

    res = lv_msgbox_add_title(obj, title);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_msgbox_add_header_button)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const void *, icon);

    icon = map_ptr(exec_env, icon);

    res = lv_msgbox_add_header_button(obj, icon);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_msgbox_add_text)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const char *, text);

    text = (const char *)map_ptr(exec_env, (const void *)text);

    res = lv_msgbox_add_text(obj, text);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_msgbox_add_footer_button)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const char *, text);

    text = (const char *)map_ptr(exec_env, (const void *)text);

    res = lv_msgbox_add_footer_button(obj, text);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_textarea_delete_char_forward)
{
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_textarea_delete_char_forward(obj);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_layer_top)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);

    res = lv_layer_top();

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_center)
{
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_obj_center(obj);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_task_get_type)
{
    lv_draw_task_type_t res;
    lvgl_native_return_type(lv_draw_task_type_t);
    lvgl_native_get_arg(const lv_draw_task_t *, obj);

    res = lv_draw_task_get_type(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_dsc_base_get_data)
{
    int res = -1;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(lv_draw_dsc_base_t *, dsc);
    lvgl_native_get_arg(int, type);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    pdata = map_ptr(exec_env, pdata);

    if (type == LV_DRAW_DSC_BASE_OBJ && n == sizeof(*dsc->obj)) {
        memcpy(pdata, dsc->obj, sizeof(*dsc->obj));
        res = 0;
    } else if (type == LV_DRAW_DSC_BASE_PART && n == sizeof(dsc->part)) {
        memcpy(pdata, &dsc->part, sizeof(dsc->part));
        res = 0;
    } else if (type == LV_DRAW_DSC_BASE_ID1 && n == sizeof(dsc->id1)) {
        memcpy(pdata, &dsc->id1, sizeof(dsc->id1));
        res = 0;
    } else if (type == LV_DRAW_DSC_BASE_ID2 && n == sizeof(dsc->id2)) {
        memcpy(pdata, &dsc->id2, sizeof(dsc->id2));
        res = 0;
    } else if (type == LV_DRAW_DSC_BASE_LAYER && n == sizeof(*dsc->layer)) {
        memcpy(pdata, dsc->layer, sizeof(*dsc->layer));
        res = 0;
    } else if (type == LV_DRAW_DSC_BASE_DSC_SIZE && n == sizeof(dsc->dsc_size)) {
        memcpy(pdata, &dsc->dsc_size, sizeof(dsc->dsc_size));
        res = 0;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_dsc_base_set_data)
{
    int res = -1;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(lv_draw_dsc_base_t *, dsc);
    lvgl_native_get_arg(int, type);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    // Check if dsc is NULL
    if (!dsc) {
        lvgl_native_set_return(res);
        return;
    }

    pdata = map_ptr(exec_env, pdata);

    if (type == LV_DRAW_DSC_BASE_OBJ && n == sizeof(*dsc->obj)) {
        // Check if dsc->obj is NULL before memcpy
        if (dsc->obj) {
            memcpy(dsc->obj, pdata, sizeof(*dsc->obj));
            res = 0;
        }
    } else if (type == LV_DRAW_DSC_BASE_PART && n == sizeof(dsc->part)) {
        memcpy(&dsc->part, pdata, sizeof(dsc->part));
        res = 0;
    } else if (type == LV_DRAW_DSC_BASE_ID1 && n == sizeof(dsc->id1)) {
        memcpy(&dsc->id1, pdata, sizeof(dsc->id1));
        res = 0;
    } else if (type == LV_DRAW_DSC_BASE_ID2 && n == sizeof(dsc->id2)) {
        memcpy(&dsc->id2, pdata, sizeof(dsc->id2));
        res = 0;
    } else if (type == LV_DRAW_DSC_BASE_LAYER && n == sizeof(*dsc->layer)) {
        // Check if dsc->layer is NULL before memcpy
        if (dsc->layer) {
            memcpy(dsc->layer, pdata, sizeof(*dsc->layer));
            res = 0;
        }
    } else if (type == LV_DRAW_DSC_BASE_DSC_SIZE && n == sizeof(dsc->dsc_size)) {
        memcpy(&dsc->dsc_size, pdata, sizeof(dsc->dsc_size));
        res = 0;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_line_dsc_get_data)
{
    int res = -1;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(lv_draw_line_dsc_t *, dsc);
    lvgl_native_get_arg(int, type);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    pdata = map_ptr(exec_env, pdata);

    if (type == LV_DRAW_LINE_DSC_BASE && n == sizeof(dsc->base)) {
        memcpy(pdata, &dsc->base, sizeof(dsc->base));
        res = 0;
    } else if (type == LV_DRAW_LINE_DSC_P1 && n == sizeof(dsc->p1)) {
        memcpy(pdata, &dsc->p1, sizeof(dsc->p1));
        res = 0;
    } else if (type == LV_DRAW_LINE_DSC_P2 && n == sizeof(dsc->p2)) {
        memcpy(pdata, &dsc->p2, sizeof(dsc->p2));
        res = 0;
    } else if (type == LV_DRAW_LINE_DSC_COLOR && n == sizeof(dsc->color)) {
        memcpy(pdata, &dsc->color, sizeof(dsc->color));
        res = 0;
    } else if (type == LV_DRAW_LINE_DSC_WIDTH && n == sizeof(dsc->width)) {
        memcpy(pdata, &dsc->width, sizeof(dsc->width));
        res = 0;
    } else if (type == LV_DRAW_LINE_DSC_DASH_WIDTH && n == sizeof(dsc->dash_width)) {
        memcpy(pdata, &dsc->dash_width, sizeof(dsc->dash_width));
        res = 0;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_line_dsc_set_data)
{
    int res = -1;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(lv_draw_line_dsc_t *, dsc);
    lvgl_native_get_arg(int, type);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    pdata = map_ptr(exec_env, pdata);

    if (type == LV_DRAW_LINE_DSC_BASE && n == sizeof(dsc->base)) {
        memcpy(&dsc->base, pdata, sizeof(dsc->base));
        res = 0;
    } else if (type == LV_DRAW_LINE_DSC_P1 && n == sizeof(dsc->p1)) {
        memcpy(&dsc->p1, pdata, sizeof(dsc->p1));
        res = 0;
    } else if (type == LV_DRAW_LINE_DSC_P2 && n == sizeof(dsc->p2)) {
        memcpy(&dsc->p2, pdata, sizeof(dsc->p2));
        res = 0;
    } else if (type == LV_DRAW_LINE_DSC_COLOR && n == sizeof(dsc->color)) {
        memcpy(&dsc->color, pdata, sizeof(dsc->color));
        res = 0;
    } else if (type == LV_DRAW_LINE_DSC_WIDTH && n == sizeof(dsc->width)) {
        memcpy(&dsc->width, pdata, sizeof(dsc->width));
        res = 0;
    } else if (type == LV_DRAW_LINE_DSC_DASH_WIDTH && n == sizeof(dsc->dash_width)) {
        memcpy(&dsc->dash_width, pdata, sizeof(dsc->dash_width));
        res = 0;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_fill_dsc_get_data)
{
    int res = -1;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(lv_draw_fill_dsc_t *, dsc);
    lvgl_native_get_arg(int, type);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    pdata = map_ptr(exec_env, pdata);

    if (type == LV_DRAW_FILL_DSC_RADIUS && n == sizeof(dsc->radius)) {
        memcpy(pdata, &dsc->radius, sizeof(dsc->radius));
        res = 0;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_scale_set_angle_range)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, angle_range);

    lv_scale_set_angle_range(obj, angle_range);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_scale_set_total_tick_count)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, total_tick_count);

    lv_scale_set_total_tick_count(obj, total_tick_count);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_scale_set_major_tick_every)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, major_tick_every);

    lv_scale_set_major_tick_every(obj, major_tick_every);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_scale_set_range)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, min);
    lvgl_native_get_arg(int32_t, max);

    lv_scale_set_range(obj, min, max);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_scale_set_rotation)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, rotation);

    lv_scale_set_rotation(obj, rotation);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_scale_add_section)
{
    lv_scale_section_t *res;
    lvgl_native_return_type(lv_scale_section_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_scale_add_section(obj);

    lvgl_native_set_return(res);
}

static void lv_scale_set_text_src_destructor(void *ext_data)
{
    wasm_text_map_t *text_map = (wasm_text_map_t *)ext_data;
    if (text_map && text_map->map) {
        free(text_map->map);
    }
    free(text_map);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_scale_set_text_src)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, txt_src);

    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    char **app_txt = (char **)addr_app_to_native(txt_src);
    if (!app_txt) {
        ESP_LOGW(TAG, "lv_scale_set_text_src: failed to map txt_src=0x%"PRIx32, txt_src);
        return;
    }

    int txt_size = 1;
    do {
        if (txt_size > MAX_TEXT_ITER) {
            ESP_LOGW(TAG, "lv_scale_set_text_src: maximum iteration limit (%d) exceeded, capping", MAX_TEXT_ITER);
            txt_size = MAX_TEXT_ITER;
            break;
        }

        if (app_txt[txt_size - 1] == NULL) {
            break;
        }

        char *str = (char *)addr_app_to_native((uint32_t)app_txt[txt_size - 1]);
        if (!str) {
            ESP_LOGW(TAG, "lv_scale_set_text_src: failed to map app_txt[%d]=0x%"PRIx32, txt_size - 1, (uint32_t)app_txt[txt_size - 1]);
            return;
        }

        txt_size++;
    } while (1);

    char **wasm_map = (char **)malloc((txt_size + 1) * sizeof(char *));
    if (!wasm_map) {
        ESP_LOGE(TAG, "lv_scale_set_text_src: failed to malloc wasm_map");
        return;
    }

    for (int i = 0; i < txt_size; i++) {
        if (app_txt[i] == NULL) {
            wasm_map[i] = NULL;
        } else {
            wasm_map[i] = (char *)addr_app_to_native((uint32_t)app_txt[i]);
            if (!wasm_map[i]) {
                ESP_LOGW(TAG, "lv_scale_set_text_src: failed to map app_txt[%d]=0x%"PRIx32, i, (uint32_t)app_txt[i]);
                free(wasm_map);
                return;
            }
        }
    }

    wasm_map[txt_size] = NULL;

    wasm_text_map_t *text_map = (wasm_text_map_t *)malloc(sizeof(wasm_text_map_t));
    if (!text_map) {
        ESP_LOGE(TAG, "lv_scale_set_text_src: failed to malloc text_map");
        free(wasm_map);
        return;
    }
    text_map->map = wasm_map;
    text_map->size = txt_size + 1;

    lv_scale_set_text_src(obj, (const char **)wasm_map);
    lv_obj_set_external_data(obj, text_map, lv_scale_set_text_src_destructor);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_scale_set_section_range)
{
    lvgl_native_get_arg(lv_obj_t *, scale);
    lvgl_native_get_arg(lv_scale_section_t *, section);
    lvgl_native_get_arg(int32_t, min);
    lvgl_native_get_arg(int32_t, max);

    lv_scale_set_section_range(scale, section, min, max);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_scale_set_section_style_main)
{
    lvgl_native_get_arg(lv_obj_t *, scale);
    lvgl_native_get_arg(lv_scale_section_t *, section);
    lvgl_native_get_arg(lv_style_t *, style);

    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);

    lv_scale_set_section_style_main(scale, section, (const lv_style_t *)style);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_scale_set_section_style_indicator)
{
    lvgl_native_get_arg(lv_obj_t *, scale);
    lvgl_native_get_arg(lv_scale_section_t *, section);
    lvgl_native_get_arg(lv_style_t *, style);

    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);

    lv_scale_set_section_style_indicator(scale, section, (const lv_style_t *)style);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_scale_set_section_style_items)
{
    lvgl_native_get_arg(lv_obj_t *, scale);
    lvgl_native_get_arg(lv_scale_section_t *, section);
    lvgl_native_get_arg(lv_style_t *, style);

    style = (lv_style_t *)map_ptr(exec_env, (const void *)style);

    lv_scale_set_section_style_items(scale, section, (const lv_style_t *)style);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_scale_set_image_needle_value)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_obj_t *, needle_img);
    lvgl_native_get_arg(int32_t, value);

    lv_scale_set_image_needle_value(obj, needle_img, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_scale_set_post_draw)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(bool, en);

    lv_scale_set_post_draw(obj, en);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_task_get_draw_dsc)
{
    void *res;
    lvgl_native_return_type(void *);
    lvgl_native_get_arg(const lv_draw_task_t *, t);

    res = lv_draw_task_get_draw_dsc(t);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_task_get_area)
{
    lvgl_native_get_arg(const lv_draw_task_t *, t);
    lvgl_native_get_arg(lv_area_t *, area);

    area = map_ptr(exec_env, area);

    lv_draw_task_get_area(t, area);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_task_get_line_dsc)
{
    lv_draw_line_dsc_t *res;
    lvgl_native_return_type(lv_draw_line_dsc_t *);
    lvgl_native_get_arg(lv_draw_task_t *, task);

    res = lv_draw_task_get_line_dsc(task);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_task_get_fill_dsc)
{
    lv_draw_fill_dsc_t *res;
    lvgl_native_return_type(lv_draw_fill_dsc_t *);
    lvgl_native_get_arg(lv_draw_task_t *, task);

    res = lv_draw_task_get_fill_dsc(task);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_task_get_label_dsc)
{
    lv_draw_label_dsc_t *res;
    lvgl_native_return_type(lv_draw_label_dsc_t *);
    lvgl_native_get_arg(lv_draw_task_t *, task);

    res = lv_draw_task_get_label_dsc(task);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_task_get_border_dsc)
{
    lv_draw_border_dsc_t *res;
    lvgl_native_return_type(lv_draw_border_dsc_t *);
    lvgl_native_get_arg(lv_draw_task_t *, task);

    res = lv_draw_task_get_border_dsc(task);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_triangle_dsc_init)
{
    lvgl_native_get_arg(lv_draw_triangle_dsc_t *, dsc);

    dsc = map_ptr(exec_env, dsc);

    lv_draw_triangle_dsc_init(dsc);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_triangle)
{
    lvgl_native_get_arg(lv_layer_t *, layer);
    lvgl_native_get_arg(const lv_draw_triangle_dsc_t *, draw_dsc);

    draw_dsc = map_ptr(exec_env, draw_dsc);

    lv_draw_triangle(layer, draw_dsc);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_pct)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(int32_t, x);

    res = lv_pct(x);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_get_first_point_center_offset)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_chart_get_first_point_center_offset(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_get_series_color)
{
    lv_color_t res;
    lvgl_native_return_type(uint32_t);
    lvgl_native_get_arg(lv_obj_t *, chart);
    lvgl_native_get_arg(const lv_chart_series_t *, series);

    res = lv_chart_get_series_color(chart, series);

    lvgl_native_set_return((uint32_t)((res.red << 16) | (res.green << 8) | res.blue));
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_get_series_y_array)
{
    int32_t *res;
    lvgl_native_return_type(int32_t *);
    lvgl_native_get_arg(const lv_obj_t *, obj);
    lvgl_native_get_arg(lv_chart_series_t *, ser);

    res = lv_chart_get_series_y_array(obj, ser);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_sibling)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(const lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, idx);

    res = lv_obj_get_sibling(obj, idx);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_arc_opa)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_opa_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_arc_opa(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_margin_left)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_margin_left(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_margin_right)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_margin_right(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_margin_top)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_margin_top(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_margin_bottom)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_margin_bottom(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_length)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_length(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_arc_rounded)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(bool, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_arc_rounded(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_coords)
{
    lvgl_native_get_arg(const lv_obj_t *, obj);
    lvgl_native_get_arg(lv_area_t *, coords);

    coords = map_ptr(exec_env, coords);

    lv_obj_get_coords(obj, coords);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_remove_style_all)
{
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_obj_remove_style_all(obj);
}


DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_layout)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, layout);

    lv_obj_set_layout(obj, layout);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_content_height)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(const lv_obj_t *, obj);

    res = lv_obj_get_content_height(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_scroll_bottom)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(const lv_obj_t *, obj);

    res = lv_obj_get_scroll_bottom(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_opa_layered)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_opa_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_opa_layered(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_scroll_to_y)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, y);
    lvgl_native_get_arg(lv_anim_enable_t, anim_en);

    lv_obj_scroll_to_y(obj, y, anim_en);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_translate_y)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_translate_y(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_arc_color)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(uint32_t, color_packed);

    void *orig_style = style;
    style = map_ptr(exec_env, style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_arc_color: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_color_t value = {
        .blue = color_packed & 0xFF,
        .green = (color_packed >> 8) & 0xFF,
        .red = (color_packed >> 16) & 0xFF
    };

    lv_style_set_arc_color(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_line_color)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(uint32_t, color_packed);

    void *orig_style = style;
    style = map_ptr(exec_env, style);
    if (!style) {
        ESP_LOGE(TAG, "lv_style_set_line_color: map_ptr failed for style=%p", orig_style);
        return;
    }

    lv_color_t value = {
        .blue = color_packed & 0xFF,
        .green = (color_packed >> 8) & 0xFF,
        .red = (color_packed >> 16) & 0xFF
    };

    lv_style_set_line_color(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_color_white)
{
    lv_color_t res;
    lvgl_native_return_type(uint32_t);

    res = lv_color_white();

    lvgl_native_set_return((uint32_t)((res.red << 16) | (res.green << 8) | res.blue));
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_color_black)
{
    lv_color_t res;
    lvgl_native_return_type(uint32_t);

    res = lv_color_black();

    lvgl_native_set_return((uint32_t)((res.red << 16) | (res.green << 8) | res.blue));
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_color_hex3)
{
    lv_color_t res;
    lvgl_native_return_type(uint32_t);
    lvgl_native_get_arg(uint32_t, c);

    res = lv_color_hex3(c);

    lvgl_native_set_return((uint32_t)((res.red << 16) | (res.green << 8) | res.blue));
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_image_set_pivot)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, x);
    lvgl_native_get_arg(int32_t, y);

    lv_image_set_pivot(obj, x, y);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_image_set_inner_align)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_image_align_t, align);

    lv_image_set_inner_align(obj, align);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_indev_wait_release)
{
    lvgl_native_get_arg(lv_indev_t *, indev);

    lv_indev_wait_release(indev);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_slider_set_value)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_anim_enable_t, anim);

    lv_slider_set_value(obj, value, anim);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_slider_set_range)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, min);
    lvgl_native_get_arg(int32_t, max);

    lv_slider_set_range(obj, min, max);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_slider_get_value)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(const lv_obj_t *, obj);

    res = lv_slider_get_value(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_area_get_width)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(const lv_area_t *, area_p);

    area_p = map_ptr(exec_env, area_p);
    res = lv_area_get_width(area_p);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_area_get_height)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(const lv_area_t *, area_p);

    area_p = map_ptr(exec_env, area_p);
    res = lv_area_get_height(area_p);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_arc_set_angles)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_value_precise_t, start);
    lvgl_native_get_arg(lv_value_precise_t, end);

    lv_arc_set_angles(obj, start, end);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_event_get_draw_task)
{
    lv_draw_task_t *res;
    lvgl_native_return_type(lv_draw_task_t *);
    lvgl_native_get_arg(lv_event_t *, e);

    res = lv_event_get_draw_task(e);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_event_get_layer)
{
    lv_layer_t *res;
    lvgl_native_return_type(lv_layer_t *);
    lvgl_native_get_arg(lv_event_t *, e);

    res = lv_event_get_layer(e);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_tabview_get_content)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_tabview_get_content(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_speed)
{
    uint32_t res;
    lvgl_native_return_type(uint32_t);
    lvgl_native_get_arg(uint32_t, speed);

    res = lv_anim_speed(speed);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_obs_data)
{
    int res = -1;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(lv_display_t *, disp);
    lvgl_native_get_arg(int, type);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    pdata = map_ptr(exec_env, pdata);

    if (type == LV_SYSMON_BACKEND_DATA && n == sizeof(disp->perf_sysmon_backend)) {
        memcpy(pdata, &disp->perf_sysmon_backend, sizeof(disp->perf_sysmon_backend));
        res = 0;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_task_get_data)
{
    int res = -1;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(lv_draw_task_t *, t);
    lvgl_native_get_arg(int, type);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    pdata = map_ptr(exec_env, pdata);

    if (type == LV_DRAW_TASK_DSC_BASE && n == sizeof(lv_draw_dsc_base_t)) {
        memcpy(pdata, t->draw_dsc, sizeof(lv_draw_dsc_base_t));
        res = 0;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_fill_dsc_set_data)
{
    int res = -1;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(lv_draw_fill_dsc_t *, dsc);
    lvgl_native_get_arg(int, type);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    pdata = map_ptr(exec_env, pdata);

    if (type == LV_DRAW_FILL_DSC_COLOR && n == sizeof(dsc->color)) {
        memcpy(&dsc->color, pdata, sizeof(dsc->color));
        res = 0;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_label_dsc_set_data)
{
    int res = -1;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(lv_draw_label_dsc_t *, dsc);
    lvgl_native_get_arg(int, type);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    pdata = map_ptr(exec_env, pdata);

    if (type == LV_DRAW_LABEL_DSC_COLOR && n == sizeof(dsc->color)) {
        memcpy(&dsc->color, pdata, sizeof(dsc->color));
        res = 0;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_border_dsc_set_data)
{
    int res = -1;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(lv_draw_border_dsc_t *, dsc);
    lvgl_native_get_arg(int, type);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    pdata = map_ptr(exec_env, pdata);

    if (type == LV_DRAW_BORDER_DSC_COLOR && n == sizeof(dsc->color)) {
        memcpy(&dsc->color, pdata, sizeof(dsc->color));
        res = 0;
    } else if (type == LV_DRAW_BORDER_DSC_WIDTH && n == sizeof(dsc->width)) {
        memcpy(&dsc->width, pdata, sizeof(dsc->width));
        res = 0;
    } else if (type == LV_DRAW_BORDER_DSC_SIDE && n == sizeof(lv_border_side_t)) {
        dsc->side = *(lv_border_side_t *)pdata;
        res = 0;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_font_get_line_height)
{
    int32_t res = -1;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(const lv_font_t *, font);

    if (!ptr_is_in_ram_or_rom(font)) {
        ESP_LOGE(TAG, "Wrong font addr %p", font);
        LVGL_TRACE_ABORT();
    }

    res = lv_font_get_line_height(font);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_trigo_cos)
{
    int32_t res = -1;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(int16_t, angle);

    res = lv_trigo_cos(angle);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_get_sys_perf_data)
{
    int res = -1;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(const lv_sysmon_perf_info_t *, info);
    lvgl_native_get_arg(int, type);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    pdata = map_ptr(exec_env, pdata);

    if (type == LV_SYS_PERF_INFO_CALC && n == sizeof(info->calculated)) {
        memcpy(pdata, &info->calculated, sizeof(info->calculated));
        res = 0;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_buttonmatrix_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, parent);

    res = lv_buttonmatrix_create(parent);

    lvgl_native_set_return(res);
}

static void lv_buttonmatrix_set_map_destructor(void *ext_data)
{
    free(ext_data);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_buttonmatrix_set_map)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, app_map);

    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    char **map = (char **)addr_app_to_native(app_map);
    if (!map) {
        ESP_LOGW(TAG, "using APP map=0x%"PRIx32, app_map);
        return;
    }

    int map_size = 1;
    do {
        if (map_size > MAX_BUTTONMAP_ITER) {
            ESP_LOGW(TAG, "lv_buttonmatrix_set_map: maximum iteration limit (%d) exceeded, map_size=%d, map[%d]=%p, breaking to avoid infinite loop",
                     MAX_BUTTONMAP_ITER, map_size, map_size - 1, (void *)map[map_size - 1]);
            break;
        }

        char *str = (char *)addr_app_to_native((uint32_t)map[map_size - 1]);
        if (!str) {
            ESP_LOGW(TAG, "using APP map[%d]=0x%"PRIx32, map_size - 1, (uint32_t)map[map_size - 1]);
            str = map[map_size - 1];
        }

        ESP_LOGD(TAG, "str=%p", (void *)str);

        if (str[0] == '\0') {
            break;
        }

        map_size++;
    } while (1);

    ESP_LOGI(TAG, "map_size=%d", map_size);

    char **wasm_map = (char **)malloc(map_size * sizeof(char *));
    if (!wasm_map) {
        ESP_LOGE(TAG, "failed to malloc wasm_map");
        return;
    }

    for (int i = 0; i < map_size; i++) {
        wasm_map[i] = (char *)addr_app_to_native((uint32_t)map[i]);
        if (!wasm_map[i]) {
            wasm_map[i] = (char *)map[i];
        }
    }

    lv_buttonmatrix_set_map(obj, (const char *const *)wasm_map);

    lv_obj_set_external_data(obj, (void *)wasm_map, lv_buttonmatrix_set_map_destructor);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_buttonmatrix_set_button_width)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, btn_id);
    lvgl_native_get_arg(uint32_t, width);

    lv_buttonmatrix_set_button_width(obj, btn_id, width);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_buttonmatrix_set_button_ctrl)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, btn_id);
    lvgl_native_get_arg(lv_buttonmatrix_ctrl_t, ctrl);

    lv_buttonmatrix_set_button_ctrl(obj, btn_id, ctrl);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_buttonmatrix_get_selected_button)
{
    uint32_t res;
    lvgl_native_return_type(uint32_t);
    lvgl_native_get_arg(const lv_obj_t *, obj);

    res = lv_buttonmatrix_get_selected_button(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_buttonmatrix_get_button_text)
{
    const char *res;
    lvgl_native_return_type(const char *);
    lvgl_native_get_arg(const lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, btn_id);

    res = lv_buttonmatrix_get_button_text(obj, btn_id);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    char *app_text = (char *)(uintptr_t)addr_native_to_app((void *)res);
    if (!app_text) {
        ESP_LOGE(TAG, "addr_native_to_app failed for lv_buttonmatrix_get_button_text res=%p", (void *)res);
        lvgl_native_set_return(NULL);
        return;
    }

    lvgl_native_set_return((const char *)app_text);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_label_cut_text)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, pos);
    lvgl_native_get_arg(uint32_t, cnt);

    lv_label_cut_text(obj, pos, cnt);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_label_ins_text)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, pos);
    lvgl_native_get_arg(const char *, txt);

    txt = (const char *)map_ptr(exec_env, (const void *)txt);

    lv_label_ins_text(obj, pos, txt);
}

/* Returns a heap-allocated pointer in the WASM module (via module_malloc).
 * The caller is responsible for freeing it using the corresponding module_free API.
 * The returned pointer is obtained via addr_native_to_app(app_text) where app_text
 * is allocated by module_malloc. Use lvgl_native_set_return to set the return value. */
DEFINE_LVGL_NATIVE_WRAPPER(lv_label_get_text2)
{
    char *res;
    lvgl_native_return_type(char *);
    lvgl_native_get_arg(const lv_obj_t *, obj);

    char *text = lv_label_get_text(obj);
    if (!text) {
        lvgl_native_set_return(NULL);
        return;
    }

    int size = strlen(text) + 1;
    char *app_text = NULL;

    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    if (!module_malloc(size, (void **)&app_text) || !app_text) {
        ESP_LOGE(TAG, "failed to malloc app_text");
        lvgl_native_set_return(NULL);
        return;
    }

    memcpy(app_text, text, size);
    res = (char *)(uintptr_t)addr_native_to_app((void *)app_text);
    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_textarea_get_text2)
{
    char *res;
    lvgl_native_return_type(const char *);
    lvgl_native_get_arg(const lv_obj_t *, obj);

    const char *text = lv_textarea_get_text(obj);
    if (!text) {
        lvgl_native_set_return(NULL);
        return;
    }

    int size = strlen(text) + 1;
    char *app_text = NULL;

    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    if (!module_malloc(size, (void **)&app_text) || !app_text) {
        ESP_LOGE(TAG, "failed to malloc app_text");
        lvgl_native_set_return(NULL);
        return;
    }

    memcpy(app_text, text, size);
    res = (char *)(uintptr_t)addr_native_to_app((void *)app_text);
    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_subject_get_pointer)
{
    const void *res;
    lvgl_native_return_type(const void *);
    lvgl_native_get_arg(lv_subject_t *, subject);

    subject = (lv_subject_t *)map_ptr(exec_env, (const void *)subject);
    res = lv_subject_get_pointer(subject);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_observer_get_target)
{
    void *res;
    lvgl_native_return_type(void *);
    lvgl_native_get_arg(lv_observer_t *, observer);

    res = lv_observer_get_target(observer);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_x)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(const lv_obj_t *, obj);

    res = lv_obj_get_x(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_y)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(const lv_obj_t *, obj);

    res = lv_obj_get_y(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_grid_column_dsc_array)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const int32_t *, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    value = map_ptr(exec_env, value);

    lv_obj_set_style_grid_column_dsc_array(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_grid_row_dsc_array)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const int32_t *, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    value = map_ptr(exec_env, value);

    lv_obj_set_style_grid_row_dsc_array(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_grid_align)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_grid_align_t, column_align);
    lvgl_native_get_arg(lv_grid_align_t, row_align);

    lv_obj_set_grid_align(obj, column_align, row_align);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_screen_load_anim)
{
    lvgl_native_get_arg(lv_obj_t *, new_scr);
    lvgl_native_get_arg(lv_screen_load_anim_t, anim_type);
    lvgl_native_get_arg(uint32_t, time);
    lvgl_native_get_arg(uint32_t, delay);
    lvgl_native_get_arg(bool, auto_del);

    lv_screen_load_anim(new_scr, anim_type, time, delay, auto_del);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_screen_load)
{
    lvgl_native_get_arg(lv_obj_t *, scr);

    lv_screen_load(scr);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_theme_simple_init)
{
    lv_theme_t *res;
    lvgl_native_return_type(lv_theme_t *);
    lvgl_native_get_arg(lv_display_t *, disp);

    res = lv_theme_simple_init(disp);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_display_set_theme)
{
    lvgl_native_get_arg(lv_display_t *, disp);
    lvgl_native_get_arg(lv_theme_t *, th);

    th = (lv_theme_t *)map_ptr(exec_env, (const void *)th);

    lv_display_set_theme(disp, th);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_align)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_align_t, align);

    lv_obj_set_align(obj, align);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_text_opa)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_opa_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_text_opa(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_send_event)
{
    lv_result_t res;
    lvgl_native_return_type(lv_result_t);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_event_code_t, event_code);
    lvgl_native_get_arg(void *, param);

    res = lv_obj_send_event(obj, event_code, param);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_bg_image_tiled)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(bool, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_bg_image_tiled(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_border_opa)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_opa_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_border_opa(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_shadow_spread)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_shadow_spread(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_image_recolor_opa)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_opa_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_image_recolor_opa(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_y_aligned)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(const lv_obj_t *, obj);

    res = lv_obj_get_y_aligned(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_set_user_data)
{
    lvgl_native_get_arg(lv_anim_t *, a);
    lvgl_native_get_arg(void *, user_data);

    a = (lv_anim_t *)map_ptr(exec_env, (const void *)a);
    lv_anim_set_user_data(a, user_data);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_set_custom_exec_cb)
{
    lvgl_native_get_arg(lv_anim_t *, a);
    lvgl_native_get_arg(lv_anim_custom_exec_cb_t, exec_cb);

    a = (lv_anim_t *)map_ptr(exec_env, (const void *)a);
    lv_anim_set_custom_exec_cb(a, exec_cb);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_set_deleted_cb)
{
    lvgl_native_get_arg(lv_anim_t *, a);
    lvgl_native_get_arg(lv_anim_deleted_cb_t, deleted_cb);

    a = (lv_anim_t *)map_ptr(exec_env, (const void *)a);
    lv_anim_set_deleted_cb(a, deleted_cb);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_set_reverse_delay)
{
    lvgl_native_get_arg(lv_anim_t *, a);
    lvgl_native_get_arg(uint32_t, delay);

    a = (lv_anim_t *)map_ptr(exec_env, (const void *)a);
    lv_anim_set_reverse_delay(a, delay);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_set_repeat_delay)
{
    lvgl_native_get_arg(lv_anim_t *, a);
    lvgl_native_get_arg(uint32_t, delay);

    a = (lv_anim_t *)map_ptr(exec_env, (const void *)a);
    lv_anim_set_repeat_delay(a, delay);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_set_early_apply)
{
    lvgl_native_get_arg(lv_anim_t *, a);
    lvgl_native_get_arg(bool, en);

    a = (lv_anim_t *)map_ptr(exec_env, (const void *)a);
    lv_anim_set_early_apply(a, en);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_set_get_value_cb)
{
    lvgl_native_get_arg(lv_anim_t *, a);
    lvgl_native_get_arg(lv_anim_get_value_cb_t, get_value_cb);

    a = (lv_anim_t *)map_ptr(exec_env, (const void *)a);
    lv_anim_set_get_value_cb(a, get_value_cb);
}


DEFINE_LVGL_NATIVE_WRAPPER(lv_arc_get_value)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(const lv_obj_t *, obj);

    res = lv_arc_get_value(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_transform_scale_x)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_transform_scale_x(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_transform_scale_y)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_transform_scale_y(obj, value, selector);
}

static void lv_async_timer_cb_wrapper(lv_timer_t *timer)
{
    uint32_t argv[1];

    /*Save the info because an lv_async_call_cancel might delete it in the callback*/
    lv_async_info_t *info = (lv_async_info_t *)timer->user_data;
    lv_async_info_t info_save = *info;

    /*Save the wrapper pointer before deleting the timer to avoid use-after-free*/
    lvgl_cb_wrapper_t *wrapper = NULL;
    if (timer && timer->ext_data.data) {
        wrapper = (lvgl_cb_wrapper_t *)timer->ext_data.data;
    }

    if (wrapper) {
        lv_async_cb_t info_cb;
        if (wrapper->module_inst && !esp_addr_executable(wrapper->event_cb)) {
            wasm_module_inst_t module_inst = wrapper->module_inst;
            uint32_t wasm_user_data = addr_native_to_app(info_save.user_data);

            ESP_LOGI(TAG, "lvgl_timer_cb_wrapper: %p -> 0x%"PRIx32"", info_save.user_data, wasm_user_data);

            argv[0] = wasm_user_data ? wasm_user_data : (uint32_t)info_save.user_data;

            lvgl_run_wasm(wrapper->module_inst, wrapper->event_cb, 1, argv);
        } else {
            info_cb = (void *)wrapper->event_cb;
            info_cb(info_save.user_data);
        }
    }

    lv_timer_delete(timer);
    lv_free(info);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_async_call)
{
    lv_result_t res = LV_RESULT_INVALID;
    lvgl_native_return_type(lv_result_t);
    lvgl_native_get_arg(lv_async_cb_t, async_xcb);
    lvgl_native_get_arg(void *, user_data);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    lvgl_cb_wrapper_t *wrapper = calloc(1, sizeof(lvgl_cb_wrapper_t));
    if (wrapper) {
        wrapper->event_cb = (uint32_t)async_xcb;
        wrapper->module_inst = module_inst;

        /*Allocate an info structure*/
        lv_async_info_t *info = lv_malloc(sizeof(lv_async_info_t));

        if (info == NULL) {
            free(wrapper);
            goto Exit;
        }

        /*Create a new timer*/
        lv_timer_t *timer = lv_timer_create(lv_async_timer_cb_wrapper, 0, info);
        if (timer == NULL) {
            lv_free(info);
            free(wrapper);
            goto Exit;
        } else {
            lv_timer_set_external_data(timer, wrapper, lvgl_timer_cb_wrapper_destructor);
        }

        info->cb = async_xcb;
        info->user_data = user_data;

        lv_timer_set_repeat_count(timer, 1);
        res = LV_RESULT_OK;
    }

Exit:
    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_subject)
{
    lv_subject_t *res;
    lvgl_native_return_type(lv_subject_t *);

    lv_display_t *disp = lv_display_get_default();
    if (!disp) {
        lvgl_native_set_return(NULL);
        return;
    }

    res = &disp->perf_sysmon_backend.subject;

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_remove_from_subject)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_subject_t *, subject);

    lv_obj_remove_from_subject(obj, subject);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_refr_pos)
{
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_obj_refr_pos(obj);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_image_set_offset_x)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, x);

    lv_image_set_offset_x(obj, x);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_delete_all)
{
    lv_anim_delete_all();
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_parent)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_obj_t *, parent);

    lv_obj_set_parent(obj, parent);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_tick_get)
{
    uint32_t res;
    lvgl_native_return_type(uint32_t);

    res = lv_tick_get();

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_tick_elaps)
{
    uint32_t res;
    lvgl_native_return_type(uint32_t);
    lvgl_native_get_arg(uint32_t, prev_tick);
    res = lv_tick_elaps(prev_tick);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_get_user_data)
{
    void *res;
    lvgl_native_return_type(void *);
    lvgl_native_get_arg(const lv_anim_t *, a);

    a = (const lv_anim_t *)map_ptr(exec_env, (const void *)a);
    res = lv_anim_get_user_data(a);

    lvgl_native_set_return(res);
}

static const lvgl_func_desc_t lvgl_func_desc_table[] = {
    LVGL_NATIVE_WRAPPER(LV_FONT_GET_FONT, lv_font_get_font, 1),
    LVGL_NATIVE_WRAPPER(LV_DISP_GET_NEXT, lv_display_get_next, 1),
    LVGL_NATIVE_WRAPPER(LV_DISP_GET_DEFAULT, lv_display_get_default, 1),
    LVGL_NATIVE_WRAPPER(LV_DISP_GET_SCR_ACT, lv_display_get_screen_active, 1),
    LVGL_NATIVE_WRAPPER(LV_DISP_GET_HOR_RES, lv_display_get_horizontal_resolution, 1),
    LVGL_NATIVE_WRAPPER(LV_DISP_GET_VER_RES, lv_display_get_vertical_resolution, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_REMOVE_STYLE, lv_obj_remove_style, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_BG_OPA, lv_obj_set_style_bg_opa, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_POS, lv_obj_set_pos, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_ALIGN_TO, lv_obj_align_to, 5),
    LVGL_NATIVE_WRAPPER(LV_OBJ_CREATE, lv_obj_create, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_WIDTH, lv_obj_get_width, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_HEIGHT, lv_obj_get_height, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_SIZE, lv_obj_set_size, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_ALIGN, lv_obj_align, 4),
    LVGL_NATIVE_WRAPPER(LV_OBJ_UPDATE_LAYOUT, lv_obj_update_layout, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_CLEAN, lv_obj_clean, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_FLEX_FLOW, lv_obj_set_flex_flow, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_CONTENT_WIDTH, lv_obj_get_content_width, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_WIDTH, lv_obj_set_width, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_LINE_COLOR, lv_obj_set_style_line_color, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_ARC_COLOR, lv_obj_set_style_arc_color, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_IMG_RECOLOR, lv_obj_set_style_image_recolor, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_TEXT_COLOR, lv_obj_set_style_text_color, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_X, lv_obj_set_x, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_Y, lv_obj_set_y, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_ADD_STYLE, lv_obj_add_style, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_BG_COLOR, lv_obj_set_style_bg_color, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_BORDER_COLOR, lv_obj_set_style_border_color, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_SHADOW_COLOR, lv_obj_set_style_shadow_color, 3),
    LVGL_NATIVE_WRAPPER(LV_LABEL_CREATE, lv_label_create, 1),
    LVGL_NATIVE_WRAPPER(LV_LABEL_SET_TEXT, lv_label_set_text, 2),
    LVGL_NATIVE_WRAPPER(LV_TABLE_CREATE, lv_table_create, 1),
    LVGL_NATIVE_WRAPPER(LV_TABLE_SET_COL_CNT, lv_table_set_column_count, 2),
    LVGL_NATIVE_WRAPPER(LV_TABLE_SET_COL_WIDTH, lv_table_set_column_width, 3),
    LVGL_NATIVE_WRAPPER(LV_TABLE_ADD_CELL_CTRL, lv_table_set_cell_ctrl, 4),
    LVGL_NATIVE_WRAPPER(LV_TABLE_SET_CELL_VALUE, lv_table_set_cell_value, 4),
    LVGL_NATIVE_WRAPPER(LV_TIMER_CREATE, lv_timer_create, 3),
    LVGL_NATIVE_WRAPPER(LV_TIMER_SET_REPEAT_COUNT, lv_timer_set_repeat_count, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_INIT, lv_style_init, 1),
    LVGL_NATIVE_WRAPPER(LV_STYLE_RESET, lv_style_reset, 1),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_BG_OPA, lv_style_set_bg_opa, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_RADIUS, lv_style_set_radius, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_BORDER_WIDTH, lv_style_set_border_width, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_BORDER_OPA, lv_style_set_border_opa, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_BORDER_SIDE, lv_style_set_border_side, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_SHADOW_OPA, lv_style_set_shadow_opa, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_SHADOW_WIDTH, lv_style_set_shadow_width, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_SHADOW_OFS_X, lv_style_set_shadow_offset_x, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_SHADOW_OFS_Y, lv_style_set_shadow_offset_y, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_SHADOW_SPREAD, lv_style_set_shadow_spread, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_IMG_OPA, lv_style_set_image_opa, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_IMG_RECOLOR_OPA, lv_style_set_image_recolor_opa, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_TEXT_FONT, lv_style_set_text_font, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_TEXT_OPA, lv_style_set_text_opa, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_LINE_WIDTH, lv_style_set_line_width, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_LINE_OPA, lv_style_set_line_opa, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_ARC_WIDTH, lv_style_set_arc_width, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_ARC_OPA, lv_style_set_arc_opa, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_BLEND_MODE, lv_style_set_blend_mode, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_TEXT_COLOR, lv_style_set_text_color, 2),
    LVGL_NATIVE_WRAPPER(LV_LINE_CREATE, lv_line_create, 1),
    LVGL_NATIVE_WRAPPER(LV_LINE_SET_POINTS, lv_line_set_points, 3),
    LVGL_NATIVE_WRAPPER(LV_ARC_CREATE, lv_arc_create, 1),
    LVGL_NATIVE_WRAPPER(LV_ARC_SET_START_ANGLE, lv_arc_set_start_angle, 2),
    LVGL_NATIVE_WRAPPER(LV_ARC_SET_END_ANGLE, lv_arc_set_end_angle, 2),
    LVGL_NATIVE_WRAPPER(LV_IMG_CREATE, lv_image_create, 1),
    LVGL_NATIVE_WRAPPER(LV_IMG_SET_SRC, lv_image_set_src, 2),
    LVGL_NATIVE_WRAPPER(LV_IMG_SET_ANGLE, lv_image_set_rotation, 2),
    LVGL_NATIVE_WRAPPER(LV_IMG_SET_ZOOM, lv_image_set_scale, 2),
    LVGL_NATIVE_WRAPPER(LV_IMG_SET_ANTIALIAS, lv_image_set_antialias, 2),
    LVGL_NATIVE_WRAPPER(LV_ANIM_INIT, lv_anim_init, 1),
    LVGL_NATIVE_WRAPPER(LV_ANIM_START, lv_anim_start, 1),
    LVGL_NATIVE_WRAPPER(LV_THEME_GET_FONT_SMALL, lv_theme_get_font_small, 1),
    LVGL_NATIVE_WRAPPER(LV_THEME_GET_FONT_NORMAL, lv_theme_get_font_normal, 1),
    LVGL_NATIVE_WRAPPER(LV_THEME_GET_FONT_LARGE, lv_theme_get_font_large, 1),
    LVGL_NATIVE_WRAPPER(LV_THEME_DEFAULT_INIT, lv_theme_default_init, 5),
    LVGL_NATIVE_WRAPPER(KV_THEME_GET_COLOR_PRIMARY, lv_theme_get_color_primary, 1),
    LVGL_NATIVE_WRAPPER(LV_FONT_GET_BITMAP_FMT_TXT, lv_font_get_bitmap_fmt_txt, 2),
    LVGL_NATIVE_WRAPPER(LV_FONT_GET_GLYPH_DSC_FMT_TXT, lv_font_get_glyph_dsc_fmt_txt, 4),
    LVGL_NATIVE_WRAPPER(LV_PALETTE_MAIN, lv_palette_main, 1),
    LVGL_NATIVE_WRAPPER(LV_TABVIEW_MAIN, lv_tabview_create, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_TEXT_FONT, lv_obj_set_style_text_font, 3),
    LVGL_NATIVE_WRAPPER(LV_TABVIEW_GET_TAB_BTNS, lv_tabview_get_tab_bar, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_PAD_LEFT, lv_obj_set_style_pad_left, 3 ),
    LVGL_NATIVE_WRAPPER(LV_TABVIEW_ADD_TAB, lv_tabview_add_tab, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_HEIGHT, lv_obj_set_height, 2),
    LVGL_NATIVE_WRAPPER(LV_LABEL_SET_LONG_MODE, lv_label_set_long_mode, 2),
    LVGL_NATIVE_WRAPPER(LV_BTN_CREATE, lv_button_create, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_ADD_STATE, lv_obj_add_state, 2),
    LVGL_NATIVE_WRAPPER(LV_KEYBOARD_CREATE, lv_keyboard_create, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_ADD_FLAG, lv_obj_add_flag, 2),
    LVGL_NATIVE_WRAPPER(LV_TEXTAREA_CREATE, lv_textarea_create, 1),
    LVGL_NATIVE_WRAPPER(LV_TEXTAREA_SET_ONE_LINE, lv_textarea_set_one_line, 2),
    LVGL_NATIVE_WRAPPER(LV_TEXTAREA_SET_PLACEHOLDER_TEXT, lv_textarea_set_placeholder_text, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_ADD_EVENT_CB, lv_obj_add_event_cb, 4),
    LVGL_NATIVE_WRAPPER(LV_TEXTAREA_SET_PASSWORD_MODE, lv_textarea_set_password_mode, 2),
    LVGL_NATIVE_WRAPPER(LV_DROPDOWN_CREATE, lv_dropdown_create, 1),
    LVGL_NATIVE_WRAPPER(LV_DROPDOWN_SET_OPTIONS_STATIC, lv_dropdown_set_options_static, 2),
    LVGL_NATIVE_WRAPPER(LV_SLIDER_CREATE, lv_slider_create, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_REFRESH_EXT_DRAW_SIZE, lv_obj_refresh_ext_draw_size, 1),
    LVGL_NATIVE_WRAPPER(LV_SWITCH_CREATE, lv_switch_create, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_GRID_DSC_ARRAY, lv_obj_set_grid_dsc_array, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_GRID_CELL, lv_obj_set_grid_cell, 7),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_TEXT_ALIGN, lv_obj_set_style_text_align, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_FLEX_GROW, lv_obj_set_flex_grow, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_MAX_HEIGHT, lv_obj_set_style_max_height, 3),
    LVGL_NATIVE_WRAPPER(LV_CHART_CREATE, lv_chart_create, 1),
    LVGL_NATIVE_WRAPPER(LV_GROUP_GET_DEFAULT, lv_group_get_default, 1),
    LVGL_NATIVE_WRAPPER(LV_GROUP_ADD_OBJ, lv_group_add_obj, 2),
    LVGL_NATIVE_WRAPPER(LV_CHART_SET_DIV_LINE_COUNT, lv_chart_set_div_line_count, 3),
    LVGL_NATIVE_WRAPPER(LV_CHART_SET_POINT_COUNT, lv_chart_set_point_count, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_BORDER_SIDE, lv_obj_set_style_border_side, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_RADIUS, lv_obj_set_style_radius, 3),
    LVGL_NATIVE_WRAPPER(LV_CHART_ADD_SERIES, lv_chart_add_series, 3),
    LVGL_NATIVE_WRAPPER(LV_RAND, lv_rand, 2),
    LVGL_NATIVE_WRAPPER(LV_CHART_SET_NEXT_VALUE, lv_chart_set_next_value, 3),
    LVGL_NATIVE_WRAPPER(LV_CHART_SET_TYPE, lv_chart_set_type, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_PAD_ROW, lv_obj_set_style_pad_row, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_PAD_COLUMN, lv_obj_set_style_pad_column, 3),
    LVGL_NATIVE_WRAPPER(LV_PALETTE_LIGHTEN, lv_palette_lighten, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_PARENT, lv_obj_get_parent, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_PAD_RIGHT, lv_obj_set_style_pad_right, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_WIDTH, lv_obj_set_style_width, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_HEIGHT, lv_obj_set_style_height, 3),
    LVGL_NATIVE_WRAPPER(LV_PALETTE_DARKEN, lv_palette_darken, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_OUTLINE_COLOR, lv_obj_set_style_outline_color, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_OUTLINE_WIDTH, lv_obj_set_style_outline_width, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_PAD_BOTTOM, lv_obj_set_style_pad_bottom, 3),
    LVGL_NATIVE_WRAPPER(LV_DISP_GET_DPI, lv_display_get_dpi, 1),
    LVGL_NATIVE_WRAPPER(LV_CHECKBOX_CREATE, lv_checkbox_create, 1),
    LVGL_NATIVE_WRAPPER(LV_CHECKBOX_SET_TEXT, lv_checkbox_set_text, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_FLEX_ALIGN, lv_obj_set_flex_align, 4),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_OPA, lv_obj_set_style_opa, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_CLEAR_FLAG, lv_obj_remove_flag, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_PAD_TOP, lv_obj_set_style_pad_top, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_SHADOW_WIDTH, lv_obj_set_style_shadow_width, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_BG_IMG_SRC, lv_obj_set_style_bg_image_src, 3),
    LVGL_NATIVE_WRAPPER(LV_EVENT_GET_CODE, lv_event_get_code, 1),
    LVGL_NATIVE_WRAPPER(LV_EVENT_GET_TARGET, lv_event_get_target, 1),
    LVGL_NATIVE_WRAPPER(LV_EVENT_GET_USER_DATA, lv_event_get_user_data, 1),
    LVGL_NATIVE_WRAPPER(LV_INDEV_GET_ACT, lv_indev_active, 1),
    LVGL_NATIVE_WRAPPER(LV_INDEV_GET_TYPE, lv_indev_get_type, 1),
    LVGL_NATIVE_WRAPPER(LV_KEYBOARD_SET_TEXTAREA, lv_keyboard_set_textarea, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SCROLL_TO_VIEW_RECURSIVE, lv_obj_scroll_to_view_recursive, 2),
    LVGL_NATIVE_WRAPPER(LV_INDEV_RESET, lv_indev_reset, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_CLEAR_STATE, lv_obj_remove_state, 2),
    LVGL_NATIVE_WRAPPER(LV_DISP_GET_LAYER_TOP, lv_display_get_layer_top, 1),
    LVGL_NATIVE_WRAPPER(LV_CALENDAR_CREATE, lv_calendar_create, 1),
    LVGL_NATIVE_WRAPPER(LV_CALENDAR_SET_SHOWED_DATE, lv_calendar_set_month_shown, 3),
    LVGL_NATIVE_WRAPPER(LV_CALENDAR_HEADER_DROPDOWN_CREATE, lv_calendar_add_header_dropdown, 1),
    LVGL_NATIVE_WRAPPER(LV_EVENT_GET_PARAM, lv_event_get_param, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_HAS_STATE, lv_obj_has_state, 2),
    LVGL_NATIVE_WRAPPER(LV_BAR_GET_VALUE, lv_bar_get_value, 1),
    LVGL_NATIVE_WRAPPER(LV_TXT_GET_SIZE, lv_text_get_size, 7),
    LVGL_NATIVE_WRAPPER(LV_DRAW_RECT_DSC_INIT, lv_draw_rect_dsc_init, 1),
    LVGL_NATIVE_WRAPPER(LV_DRAW_RECT, lv_draw_rect, 3),
    LVGL_NATIVE_WRAPPER(LV_DRAW_LABEL_DSC_INIT, lv_draw_label_dsc_init, 1),
    LVGL_NATIVE_WRAPPER(LV_DRAW_LABEL, lv_draw_label, 3),
    LVGL_NATIVE_WRAPPER(LV_EVENT_GET_CURRENT_TARGET, lv_event_get_current_target, 1),
    LVGL_NATIVE_WRAPPER(LV_CALENDAR_GET_PRESSED_DATE, lv_calendar_get_pressed_date, 2),
    LVGL_NATIVE_WRAPPER(LV_TEXTAREA_SET_TEXT, lv_textarea_set_text, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_DEL, lv_obj_delete, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_INVALIDATE, lv_obj_invalidate, 1),
    LVGL_NATIVE_WRAPPER(LV_CHART_GET_TYPE, lv_chart_get_type, 1),
    LVGL_NATIVE_WRAPPER(_LV_AREA_INTERSECT, lv_area_intersect, 3),
    LVGL_NATIVE_WRAPPER(LV_CHART_GET_PRESSED_POINT, lv_chart_get_pressed_point, 1),
    LVGL_NATIVE_WRAPPER(LV_CHART_GET_SERIES_NEXT, lv_chart_get_series_next, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_CHILD, lv_obj_get_child, 2),
    LVGL_NATIVE_WRAPPER(LV_CHART_SET_SERIES_COLOR, lv_chart_set_series_color, 3),
    LVGL_NATIVE_WRAPPER(LV_MAP, lv_map, 5),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_CHILD_CNT, lv_obj_get_child_count, 1),
    LVGL_NATIVE_WRAPPER(LV_MEM_TEST, lv_mem_test, 1),
    LVGL_NATIVE_WRAPPER(LV_MEM_MONITOR, lv_mem_monitor, 1),
    LVGL_NATIVE_WRAPPER(LV_TABVIEW_SET_ACT, lv_tabview_set_active, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_DEL_ASYNC, lv_obj_delete_async, 1),
    LVGL_NATIVE_WRAPPER(LV_BAR_CREATE, lv_bar_create, 1),
    LVGL_NATIVE_WRAPPER(LV_BAR_SET_RANGE, lv_bar_set_range, 3),
    LVGL_NATIVE_WRAPPER(LV_BAR_SET_VALUE, lv_bar_set_value, 3),
    LVGL_NATIVE_WRAPPER(LV_BAR_SET_START_VALUE, lv_bar_set_start_value, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_ANIM_TIME, lv_obj_set_style_anim_duration, 3),
    LVGL_NATIVE_WRAPPER(LV_WIN_CREATE, lv_win_create, 1),
    LVGL_NATIVE_WRAPPER(LV_WIN_ADD_TITLE, lv_win_add_title, 2),
    LVGL_NATIVE_WRAPPER(LV_WIN_ADD_BTN, lv_win_add_button, 3),
    LVGL_NATIVE_WRAPPER(LV_WIN_GET_CONTENT, lv_win_get_content, 1),
    LVGL_NATIVE_WRAPPER(LV_KEYBOARD_SET_MODE, lv_keyboard_set_mode, 2),
    LVGL_NATIVE_WRAPPER(LV_DROPDOWN_SET_OPTIONS, lv_dropdown_set_options, 2),
    LVGL_NATIVE_WRAPPER(LV_DROPDOWN_OPEN, lv_dropdown_open, 1),
    LVGL_NATIVE_WRAPPER(LV_DROPDOWN_SET_SELECTED, lv_dropdown_set_selected, 2),
    LVGL_NATIVE_WRAPPER(LV_ROLLER_CREATE, lv_roller_create, 1),
    LVGL_NATIVE_WRAPPER(LV_ROLLER_SET_OPTIONS, lv_roller_set_options, 3),
    LVGL_NATIVE_WRAPPER(LV_ROLLER_SET_SELECTED, lv_roller_set_selected, 3),
    LVGL_NATIVE_WRAPPER(LV_MSGBOX_CREATE, lv_msgbox_create, 1),
    LVGL_NATIVE_WRAPPER(LV_TILEVIEW_CREATE, lv_tileview_create, 1),
    LVGL_NATIVE_WRAPPER(LV_TILEVIEW_ADD_TILE, lv_tileview_add_tile, 4),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_TILE_ID, lv_tileview_set_tile_by_index, 4),
    LVGL_NATIVE_WRAPPER(LV_LIST_CREATE, lv_list_create, 1),
    LVGL_NATIVE_WRAPPER(LV_LIST_ADD_BTN, lv_list_add_button, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SCROLL_TO_VIEW, lv_obj_scroll_to_view, 2),
    LVGL_NATIVE_WRAPPER(LV_TEXTAREA_SET_CURSOR_POS, lv_textarea_set_cursor_pos, 2),
    LVGL_NATIVE_WRAPPER(LV_TEXTAREA_ADD_CHAR, lv_textarea_add_char, 2),
    LVGL_NATIVE_WRAPPER(LV_TEXTAREA_ADD_TEXT, lv_textarea_add_text, 2),
    LVGL_NATIVE_WRAPPER(LV_SPINBOX_CREATE, lv_spinbox_create, 1),
    LVGL_NATIVE_WRAPPER(LV_SPINBOX_SET_DIGIT_FORMAT, lv_spinbox_set_digit_format, 3),
    LVGL_NATIVE_WRAPPER(LV_SPINBOX_SET_VALUE, lv_spinbox_set_value, 2),
    LVGL_NATIVE_WRAPPER(LV_SPINBOX_SET_STEP, lv_spinbox_set_step, 2),
    LVGL_NATIVE_WRAPPER(LV_SPINBOX_INCREMENT, lv_spinbox_increment, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SCROLL_BY, lv_obj_scroll_by, 4),
    LVGL_NATIVE_WRAPPER(LV_MSGBOX_CLOSE, lv_msgbox_close, 1),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_WIDTH, lv_style_set_width, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_BG_COLOR, lv_style_set_bg_color, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_PAD_RIGHT, lv_style_set_pad_right, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_GRID_COLUMN_DSC_ARRAY, lv_style_set_grid_column_dsc_array, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_GRID_ROW_DSC_ARRAY, lv_style_set_grid_row_dsc_array, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_GRID_ROW_ALIGN, lv_style_set_grid_row_align, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_LAYOUT, lv_style_set_layout, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_INDEX, lv_obj_get_index, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_SCROLL_SNAP_Y, lv_obj_set_scroll_snap_y, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_BORDER_WIDTH, lv_obj_set_style_border_width, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_SCROLL_DIR, lv_obj_set_scroll_dir, 2),
    LVGL_NATIVE_WRAPPER(LV_IMGBTN_CREATE, lv_imagebutton_create, 1),
    LVGL_NATIVE_WRAPPER(LV_IMGBTN_SET_SRC, lv_imagebutton_set_src, 5),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_BG_GRAD_DIR, lv_obj_set_style_bg_grad_dir, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_BG_GRAD_COLOR, lv_obj_set_style_bg_grad_color, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_GRID_ROW_ALIGN, lv_obj_set_style_grid_row_align, 3),
    LVGL_NATIVE_WRAPPER(LV_TIMER_PAUSE, lv_timer_pause, 1),
    LVGL_NATIVE_WRAPPER(LV_ANIM_PATH_BOUNCE, lv_anim_path_bounce, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_FADE_IN, lv_obj_fade_in, 3),
    LVGL_NATIVE_WRAPPER(LV_ANIM_PATH_EASE_OUT, lv_anim_path_ease_out, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_MOVE_TO_INDEX, lv_obj_move_to_index, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_TEXT_LINE_SPACE, lv_obj_set_style_text_line_space, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_FADE_OUT, lv_obj_fade_out, 3),
    LVGL_NATIVE_WRAPPER(LV_TIMER_RESUME, lv_timer_resume, 1),
    LVGL_NATIVE_WRAPPER(LV_ANIM_PATH_LINEAR, lv_anim_path_linear, 1),
    LVGL_NATIVE_WRAPPER(LV_ANIM_PATH_OVERSHOOT, lv_anim_path_overshoot, 1),
    LVGL_NATIVE_WRAPPER(LV_ANIM_DEL, lv_anim_delete, 2),
    LVGL_NATIVE_WRAPPER(LV_EVENT_SET_EXT_DRAW_SIZE, lv_event_set_ext_draw_size, 2),
    LVGL_NATIVE_WRAPPER(LV_EVENT_SET_COVER_RES, lv_event_set_cover_res, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_STYLE_PROP, lv_obj_get_style_prop, 3),
    LVGL_NATIVE_WRAPPER(LV_IMG_GET_ZOOM, lv_image_get_scale, 1),
    LVGL_NATIVE_WRAPPER(LV_TRIGO_SIN, lv_trigo_sin, 1),
    LVGL_NATIVE_WRAPPER(LV_INDEV_GET_GESTURE_DIR, lv_indev_get_gesture_dir, 1),
    LVGL_NATIVE_WRAPPER(LV_ANIM_PATH_EASE_IN, lv_anim_path_ease_in, 1),
    LVGL_NATIVE_WRAPPER(LV_TIMER_GET_USER_DATA, lv_timer_get_user_data, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_DRAW_PART_DSC_GET_DATA, lv_draw_rect_dsc_get_data, 4),
    LVGL_NATIVE_WRAPPER(LV_OBJ_DRAW_PART_DSC_SET_DATA, lv_draw_rect_dsc_set_data, 4),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_DATA, lv_obj_get_data, 4),
    LVGL_NATIVE_WRAPPER(LV_FONT_GET_DATA, lv_font_get_data, 4),
    LVGL_NATIVE_WRAPPER(LV_LABEL_SET_TEXT_STATIC, lv_label_set_text_static, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_BORDER_COLOR, lv_style_set_border_color, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_SHADOW_COLOR, lv_style_set_shadow_color, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_OUTLINE_COLOR, lv_style_set_outline_color, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_OUTLINE_WIDTH, lv_style_set_outline_width, 2),
    LVGL_NATIVE_WRAPPER(LV_INDEV_GET_NEXT, lv_indev_get_next, 1),
    LVGL_NATIVE_WRAPPER(LV_GROUP_CREATE, lv_group_create, 1),
    LVGL_NATIVE_WRAPPER(LV_INDEV_SET_GROUP, lv_indev_set_group, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_SHADOW_OPA, lv_obj_set_style_shadow_opa, 3),
    LVGL_NATIVE_WRAPPER(LV_INDEV_ENABLE, lv_indev_enable, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_HAS_FLAG, lv_obj_has_flag, 2),
    LVGL_NATIVE_WRAPPER(LV_ARC_SET_BG_ANGLES, lv_arc_set_bg_angles, 3),
    LVGL_NATIVE_WRAPPER(LV_ARC_SET_VALUE, lv_arc_set_value, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_ARC_WIDTH, lv_obj_set_style_arc_width, 3),
    LVGL_NATIVE_WRAPPER(LV_ARC_SET_ROTATION, lv_arc_set_rotation, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_IMG_OPA, lv_obj_set_style_image_opa, 3),
    LVGL_NATIVE_WRAPPER(LV_TIMER_DEL, lv_timer_delete, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_USER_DATA, lv_obj_get_user_data, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_USER_DATA, lv_obj_set_user_data, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_SCROLLBAR_MODE, lv_obj_set_scrollbar_mode, 2),
    LVGL_NATIVE_WRAPPER(LV_GROUP_REMOVE_ALL_OBJS, lv_group_remove_all_objs, 1),
    LVGL_NATIVE_WRAPPER(LV_LABEL_SET_RECOLOR, lv_label_set_recolor, 2),
    LVGL_NATIVE_WRAPPER(LV_TABVIEW_GET_TAB_ACT, lv_tabview_get_tab_active, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_SHADOW_OFS_X, lv_obj_set_style_shadow_offset_x, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_SHADOW_OFS_Y, lv_obj_set_style_shadow_offset_y, 3),
    LVGL_NATIVE_WRAPPER(LV_LED_CREATE, lv_led_create, 1),
    LVGL_NATIVE_WRAPPER(LV_LED_OFF, lv_led_off, 1),
    LVGL_NATIVE_WRAPPER(LV_LED_ON, lv_led_on, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_CLICK_AREA, lv_obj_get_click_area, 2),
    LVGL_NATIVE_WRAPPER(LV_INDEV_SET_BUTTON_POINTS, lv_indev_set_button_points, 2),
    LVGL_NATIVE_WRAPPER(LV_QRCODE_CREATE, lv_qrcode_create, 1),
    LVGL_NATIVE_WRAPPER(LV_QRCODE_UPDATE, lv_qrcode_update, 3),
    LVGL_NATIVE_WRAPPER(LV_GROUP_FOCUS_OBJ, lv_group_focus_obj, 1),
    LVGL_NATIVE_WRAPPER(LV_GROUP_FOCUS_FREEZE, lv_group_focus_freeze, 2),
    LVGL_NATIVE_WRAPPER(LV_DISP_GET_REFR_TIMER, lv_display_get_refr_timer, 1),
    LVGL_NATIVE_WRAPPER(LV_TIMER_SET_PERIOD, lv_timer_set_period, 2),
    LVGL_NATIVE_WRAPPER(LV_ANIM_GET_TIMER, lv_anim_get_timer, 1),
    LVGL_NATIVE_WRAPPER(LV_ANIM_TIMER_GET_DATA, lv_anim_timer_get_data, 3),
    LVGL_NATIVE_WRAPPER(LV_TABLE_SET_ROW_CNT, lv_table_set_row_count, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_STYLE_OPA_RECURSIVE, lv_obj_get_style_opa_recursive, 2),
    LVGL_NATIVE_WRAPPER(LV_TIMER_CTX_GET_DATA, lv_timer_ctx_get_data, 4),
    LVGL_NATIVE_WRAPPER(LV_TIMER_CTX_SET_DATA, lv_timer_ctx_set_data, 4),
    LVGL_NATIVE_WRAPPER(LV_TIMER_READY, lv_timer_ready, 1),
    LVGL_NATIVE_WRAPPER(LV_SUBJECT_ADD_OBSERVER_OBJ, lv_subject_add_observer_obj, 4),
    LVGL_NATIVE_WRAPPER(LV_SCREEN_ACTIVE, lv_screen_active, 1),
    LVGL_NATIVE_WRAPPER(LV_TABVIEW_SET_TAB_BAR_SIZE, lv_tabview_set_tab_bar_size, 2),
    LVGL_NATIVE_WRAPPER(LV_ANIM_SET_VAR, lv_anim_set_var, 2),
    LVGL_NATIVE_WRAPPER(LV_ANIM_SET_DURATION, lv_anim_set_duration, 2),
    LVGL_NATIVE_WRAPPER(LV_ANIM_SET_DELAY, lv_anim_set_delay, 2),
    LVGL_NATIVE_WRAPPER(LV_ANIM_SET_COMPLETED_CB, lv_anim_set_completed_cb, 2),
    LVGL_NATIVE_WRAPPER(LV_ANIM_SET_EXEC_CB, lv_anim_set_exec_cb, 2),
    LVGL_NATIVE_WRAPPER(LV_ANIM_SET_PATH_CB, lv_anim_set_path_cb, 2),
    LVGL_NATIVE_WRAPPER(LV_ANIM_SET_VALUES, lv_anim_set_values, 3),
    LVGL_NATIVE_WRAPPER(LV_ANIM_SET_REVERSE_DURATION, lv_anim_set_reverse_duration, 2),
    LVGL_NATIVE_WRAPPER(LV_ANIM_SET_REPEAT_COUNT, lv_anim_set_repeat_count, 2),
    LVGL_NATIVE_WRAPPER(LV_SCALE_CREATE, lv_scale_create, 1),
    LVGL_NATIVE_WRAPPER(LV_SCALE_SET_MODE, lv_scale_set_mode, 2),
    LVGL_NATIVE_WRAPPER(LV_SCALE_SET_ANGLE_RANGE, lv_scale_set_angle_range, 2),
    LVGL_NATIVE_WRAPPER(LV_SCALE_SET_TEXT_SRC, lv_scale_set_text_src, 2),
    LVGL_NATIVE_WRAPPER(LV_SCALE_SET_TOTAL_TICK_COUNT, lv_scale_set_total_tick_count, 2),
    LVGL_NATIVE_WRAPPER(LV_SCALE_SET_MAJOR_TICK_EVERY, lv_scale_set_major_tick_every, 2),
    LVGL_NATIVE_WRAPPER(LV_SCALE_SET_RANGE, lv_scale_set_range, 3),
    LVGL_NATIVE_WRAPPER(LV_SCALE_SET_ROTATION, lv_scale_set_rotation, 2),
    LVGL_NATIVE_WRAPPER(LV_SCALE_ADD_SECTION, lv_scale_add_section, 1),
    LVGL_NATIVE_WRAPPER(LV_SCALE_SET_SECTION_RANGE, lv_scale_set_section_range, 4),
    LVGL_NATIVE_WRAPPER(LV_SCALE_SET_SECTION_STYLE_MAIN, lv_scale_set_section_style_main, 3),
    LVGL_NATIVE_WRAPPER(LV_SCALE_SET_SECTION_STYLE_INDICATOR, lv_scale_set_section_style_indicator, 3),
    LVGL_NATIVE_WRAPPER(LV_SCALE_SET_SECTION_STYLE_ITEMS, lv_scale_set_section_style_items, 3),
    LVGL_NATIVE_WRAPPER(LV_SCALE_SET_IMAGE_NEEDLE_VALUE, lv_scale_set_image_needle_value, 3),
    LVGL_NATIVE_WRAPPER(LV_SCALE_SET_POST_DRAW, lv_scale_set_post_draw, 2),
    LVGL_NATIVE_WRAPPER(LV_MSGBOX_ADD_TITLE, lv_msgbox_add_title, 2),
    LVGL_NATIVE_WRAPPER(LV_MSGBOX_ADD_HEADER_BUTTON, lv_msgbox_add_header_button, 2),
    LVGL_NATIVE_WRAPPER(LV_MSGBOX_ADD_TEXT, lv_msgbox_add_text, 2),
    LVGL_NATIVE_WRAPPER(LV_MSGBOX_ADD_FOOTER_BUTTON, lv_msgbox_add_footer_button, 2),
    LVGL_NATIVE_WRAPPER(LV_TEXTAREA_DELETE_CHAR_FORWARD, lv_textarea_delete_char_forward, 1),
    LVGL_NATIVE_WRAPPER(LV_LAYER_TOP, lv_layer_top, 1),
    LVGL_NATIVE_WRAPPER(LV_DRAW_TASK_GET_TYPE, lv_draw_task_get_type, 1),
    LVGL_NATIVE_WRAPPER(LV_DRAW_DSC_BASE_GET_DATA, lv_draw_dsc_base_get_data, 4),
    LVGL_NATIVE_WRAPPER(LV_DRAW_DSC_BASE_SET_DATA, lv_draw_dsc_base_set_data, 4),
    LVGL_NATIVE_WRAPPER(LV_DRAW_LINE_DSC_GET_DATA, lv_draw_line_dsc_get_data, 4),
    LVGL_NATIVE_WRAPPER(LV_DRAW_LINE_DSC_SET_DATA, lv_draw_line_dsc_set_data, 4),
    LVGL_NATIVE_WRAPPER(LV_DRAW_FILL_DSC_GET_DATA, lv_draw_fill_dsc_get_data, 4),
    LVGL_NATIVE_WRAPPER(LV_DRAW_TASK_GET_DRAW_DSC, lv_draw_task_get_draw_dsc, 1),
    LVGL_NATIVE_WRAPPER(LV_DRAW_TASK_GET_AREA, lv_draw_task_get_area, 2),
    LVGL_NATIVE_WRAPPER(LV_DRAW_TASK_GET_LINE_DSC, lv_draw_task_get_line_dsc, 1),
    LVGL_NATIVE_WRAPPER(LV_DRAW_TASK_GET_FILL_DSC, lv_draw_task_get_fill_dsc, 1),
    LVGL_NATIVE_WRAPPER(LV_DRAW_TASK_GET_LABEL_DSC, lv_draw_task_get_label_dsc, 1),
    LVGL_NATIVE_WRAPPER(LV_DRAW_TASK_GET_BORDER_DSC, lv_draw_task_get_border_dsc, 1),
    LVGL_NATIVE_WRAPPER(LV_DRAW_TRIANGLE_DSC_INIT, lv_draw_triangle_dsc_init, 1),
    LVGL_NATIVE_WRAPPER(LV_DRAW_TRIANGLE, lv_draw_triangle, 2),
    LVGL_NATIVE_WRAPPER(LV_AREA_PCT, lv_pct, 1),
    LVGL_NATIVE_WRAPPER(LV_CHART_GET_FIRST_POINT_CENTER_OFFSET, lv_chart_get_first_point_center_offset, 1),
    LVGL_NATIVE_WRAPPER(LV_CHART_GET_SERIES_COLOR, lv_chart_get_series_color, 2),
    LVGL_NATIVE_WRAPPER(LV_CHART_GET_SERIES_Y_ARRAY, lv_chart_get_series_y_array, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_CENTER, lv_obj_center, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_DELETE_ANIM_COMPLETED_CB, lv_obj_delete_anim_completed_cb, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_SIBLING, lv_obj_get_sibling, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_ARC_OPA, lv_obj_set_style_arc_opa, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_MARGIN_LEFT, lv_obj_set_style_margin_left, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_MARGIN_RIGHT, lv_obj_set_style_margin_right, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_MARGIN_TOP, lv_obj_set_style_margin_top, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_MARGIN_BOTTOM, lv_obj_set_style_margin_bottom, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_LENGTH, lv_obj_set_style_length, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_ARC_ROUNDED, lv_obj_set_style_arc_rounded, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_COORDS, lv_obj_get_coords, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_REMOVE_STYLE_ALL, lv_obj_remove_style_all, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_LAYOUT, lv_obj_set_layout, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_CONTENT_HEIGHT, lv_obj_get_content_height, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_SCROLL_BOTTOM, lv_obj_get_scroll_bottom, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_OPA_LAYERED, lv_obj_set_style_opa_layered, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SCROLL_TO_Y, lv_obj_scroll_to_y, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_TRANSLATE_Y, lv_obj_set_style_translate_y, 3),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_ARC_COLOR, lv_style_set_arc_color, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_LINE_COLOR, lv_style_set_line_color, 2),
    LVGL_NATIVE_WRAPPER(LV_COLOR_WHITE, lv_color_white, 1),
    LVGL_NATIVE_WRAPPER(LV_COLOR_BLACK, lv_color_black, 1),
    LVGL_NATIVE_WRAPPER(LV_COLOR_HEX3, lv_color_hex3, 1),
    LVGL_NATIVE_WRAPPER(LV_IMAGE_SET_PIVOT, lv_image_set_pivot, 3),
    LVGL_NATIVE_WRAPPER(LV_IMAGE_SET_INNER_ALIGN, lv_image_set_inner_align, 2),
    LVGL_NATIVE_WRAPPER(LV_INDEV_WAIT_RELEASE, lv_indev_wait_release, 1),
    LVGL_NATIVE_WRAPPER(LV_SLIDER_SET_VALUE, lv_slider_set_value, 3),
    LVGL_NATIVE_WRAPPER(LV_SLIDER_SET_RANGE, lv_slider_set_range, 3),
    LVGL_NATIVE_WRAPPER(LV_SLIDER_GET_VALUE, lv_slider_get_value, 1),
    LVGL_NATIVE_WRAPPER(LV_AREA_GET_WIDTH, lv_area_get_width, 1),
    LVGL_NATIVE_WRAPPER(LV_AREA_GET_HEIGHT, lv_area_get_height, 1),
    LVGL_NATIVE_WRAPPER(LV_ARC_SET_ANGLES, lv_arc_set_angles, 3),
    LVGL_NATIVE_WRAPPER(LV_EVENT_GET_DRAW_TASK, lv_event_get_draw_task, 1),
    LVGL_NATIVE_WRAPPER(LV_EVENT_GET_LAYER, lv_event_get_layer, 1),
    LVGL_NATIVE_WRAPPER(LV_TABVIEW_GET_CONTENT, lv_tabview_get_content, 1),
    LVGL_NATIVE_WRAPPER(LV_ANIM_SPEED, lv_anim_speed, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_OBS_DATA, lv_obj_get_obs_data, 4),
    LVGL_NATIVE_WRAPPER(LV_DRAW_TASK_GET_DATA, lv_draw_task_get_data, 4),
    LVGL_NATIVE_WRAPPER(LV_DRAW_FILL_DSC_SET_DATA, lv_draw_fill_dsc_set_data, 4),
    LVGL_NATIVE_WRAPPER(LV_DRAW_LABEL_DSC_SET_DATA, lv_draw_label_dsc_set_data, 4),
    LVGL_NATIVE_WRAPPER(LV_DRAW_BORDER_DSC_SET_DATA, lv_draw_border_dsc_set_data, 4),
    LVGL_NATIVE_WRAPPER(LV_FONT_GET_LINE_HEIGHT, lv_font_get_line_height, 1),
    LVGL_NATIVE_WRAPPER(LV_TRIGO_COS, lv_trigo_cos, 1),
    LVGL_NATIVE_WRAPPER(LV_GET_SYS_PERF_DATA, lv_get_sys_perf_data, 4),
    LVGL_NATIVE_WRAPPER(LV_BUTTONMATRIX_CREATE, lv_buttonmatrix_create, 1),
    LVGL_NATIVE_WRAPPER(LV_BUTTONMATRIX_SET_MAP, lv_buttonmatrix_set_map, 2),
    LVGL_NATIVE_WRAPPER(LV_BUTTONMATRIX_SET_BUTTON_WIDTH, lv_buttonmatrix_set_button_width, 3),
    LVGL_NATIVE_WRAPPER(LV_BUTTONMATRIX_SET_BUTTON_CTRL, lv_buttonmatrix_set_button_ctrl, 3),
    LVGL_NATIVE_WRAPPER(LV_BUTTONMATRIX_GET_SELECTED_BUTTON, lv_buttonmatrix_get_selected_button, 1),
    LVGL_NATIVE_WRAPPER(LV_BUTTONMATRIX_GET_BUTTON_TEXT, lv_buttonmatrix_get_button_text, 2),
    LVGL_NATIVE_WRAPPER(LV_LABEL_CUT_TEXT, lv_label_cut_text, 3),
    LVGL_NATIVE_WRAPPER(LV_LABEL_INS_TEXT, lv_label_ins_text, 3),
    LVGL_NATIVE_WRAPPER(LV_LABEL_GET_TEXT, lv_label_get_text2, 1),
    LVGL_NATIVE_WRAPPER(LV_TEXTAREA_GET_TEXT, lv_textarea_get_text2, 1),
    LVGL_NATIVE_WRAPPER(LV_SUBJECT_GET_POINTER, lv_subject_get_pointer, 1),
    LVGL_NATIVE_WRAPPER(LV_OBSERVER_GET_TARGET, lv_observer_get_target, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_X, lv_obj_get_x, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_Y, lv_obj_get_y, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_GRID_COLUMN_DSC_ARRAY, lv_obj_set_style_grid_column_dsc_array, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_GRID_ROW_DSC_ARRAY, lv_obj_set_style_grid_row_dsc_array, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_GRID_ALIGN, lv_obj_set_grid_align, 3),
    LVGL_NATIVE_WRAPPER(LV_SCREEN_LOAD_ANIM, lv_screen_load_anim, 5),
    LVGL_NATIVE_WRAPPER(LV_SCREEN_LOAD, lv_screen_load, 1),
    LVGL_NATIVE_WRAPPER(LV_THEME_SIMPLE_INIT, lv_theme_simple_init, 1),
    LVGL_NATIVE_WRAPPER(LV_DISPLAY_SET_THEME, lv_display_set_theme, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_ALIGN, lv_obj_set_align, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_TEXT_OPA, lv_obj_set_style_text_opa, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SEND_EVENT, lv_obj_send_event, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_BG_IMAGE_TILED, lv_obj_set_style_bg_image_tiled, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_BORDER_OPA, lv_obj_set_style_border_opa, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_SHADOW_SPREAD, lv_obj_set_style_shadow_spread, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_IMAGE_RECOLOR_OPA, lv_obj_set_style_image_recolor_opa, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_Y_ALIGNED, lv_obj_get_y_aligned, 1),
    LVGL_NATIVE_WRAPPER(LV_ANIM_SET_USER_DATA, lv_anim_set_user_data, 2),
    LVGL_NATIVE_WRAPPER(LV_ANIM_SET_CUSTOM_EXEC_CB, lv_anim_set_custom_exec_cb, 2),
    LVGL_NATIVE_WRAPPER(LV_ANIM_SET_DELETED_CB, lv_anim_set_deleted_cb, 2),
    LVGL_NATIVE_WRAPPER(LV_ANIM_SET_REVERSE_DELAY, lv_anim_set_reverse_delay, 2),
    LVGL_NATIVE_WRAPPER(LV_ANIM_SET_REPEAT_DELAY, lv_anim_set_repeat_delay, 2),
    LVGL_NATIVE_WRAPPER(LV_ANIM_SET_EARLY_APPLY, lv_anim_set_early_apply, 2),
    LVGL_NATIVE_WRAPPER(LV_ANIM_SET_GET_VALUE_CB, lv_anim_set_get_value_cb, 2),
    LVGL_NATIVE_WRAPPER(LV_ARC_GET_VALUE, lv_arc_get_value, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_TRANSFORM_SCALE_X, lv_obj_set_style_transform_scale_x, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_TRANSFORM_SCALE_Y, lv_obj_set_style_transform_scale_y, 3),
    LVGL_NATIVE_WRAPPER(LV_ASYNC_CALL, lv_async_call, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_SUBJECT, lv_obj_get_subject, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_REMOVE_FROM_SUBJECT, lv_obj_remove_from_subject, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_REFR_POS, lv_obj_refr_pos, 1),
    LVGL_NATIVE_WRAPPER(LV_IMAGE_SET_OFFSET_X, lv_image_set_offset_x, 2),
    LVGL_NATIVE_WRAPPER(LV_ANIM_DELETE_ALL, lv_anim_delete_all, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_PARENT, lv_obj_set_parent, 2),
    LVGL_NATIVE_WRAPPER(LV_TICK_GET, lv_tick_get, 1),
    LVGL_NATIVE_WRAPPER(LV_TICK_ELAPS, lv_tick_elaps, 1),
    LVGL_NATIVE_WRAPPER(LV_ANIM_GET_USER_DATA, lv_anim_get_user_data, 1),
};

static void esp_lvgl_call_native_func_wrapper(wasm_exec_env_t exec_env,
        int32_t func_id,
        uint32_t argc,
        uint32_t *argv)
{
    int func_num = sizeof(lvgl_func_desc_table) / sizeof(lvgl_func_desc_table[0]);
    wasm_module_inst_t module_inst = get_module_inst(exec_env);
    const lvgl_func_desc_t *func_desc = &lvgl_func_desc_table[func_id];

    if (func_id >= func_num) {
        ESP_LOGE(TAG, "func_id=%"PRIi32" is out of range", func_id);
        return;
    }

    if (!wasm_runtime_validate_native_addr(module_inst,
                                           argv,
                                           argc * sizeof(uint32_t))) {
        ESP_LOGE(TAG, "argv=%p argc=%"PRIu32" is out of range", argv, argc);
        return;
    }

    if (func_desc->argc == argc) {
        uint32_t size;
        uint32_t argv_copy_buf[LVGL_ARG_BUF_NUM];
        uint32_t *argv_copy = argv_copy_buf;

        if (argc > LVGL_ARG_BUF_NUM) {
            if (argc > LVGL_ARG_NUM_MAX) {
                ESP_LOGE(TAG, "argc=%"PRIu32" is out of range", argc);
                return;
            }

            size = sizeof(uint32_t) * argc;
            argv_copy = wasm_runtime_malloc(size);
            if (!argv_copy) {
                ESP_LOGE(TAG, "failed to malloc for argv_copy");
                return;
            }

            memset(argv_copy, 0, (uint32_t)size);
        }

        for (int i = 0; i < argc; i++) {
            argv_copy[i] = argv[i];
        }

        ESP_LOGD(TAG, "func_id=%"PRIi32" start", func_id);

        func_desc->func(exec_env, argv_copy, argv);

        if (argv_copy != argv_copy_buf) {
            wasm_runtime_free(argv_copy);
        }

        // ESP_LOGD(TAG, "func_id=%"PRIi32" done", func_id);
    } else {
        ESP_LOGE(TAG, "func_id=%"PRIi32" is not found", func_id);
    }
}

static NativeSymbol wm_lvgl_wrapper_native_symbol[] = {
    REG_NATIVE_FUNC(esp_lvgl_init, "(i)i"),
    REG_NATIVE_FUNC(esp_lvgl_deinit, "()i"),
    REG_NATIVE_FUNC(lvgl_lock, "()"),
    REG_NATIVE_FUNC(lvgl_unlock, "()"),
    REG_NATIVE_FUNC(esp_lvgl_call_native_func, "(ii*)"),
};

int wm_ext_wasm_native_lvgl_export(void)
{
    NativeSymbol *sym = (NativeSymbol *)wm_lvgl_wrapper_native_symbol;
    int num = sizeof(wm_lvgl_wrapper_native_symbol) / sizeof(wm_lvgl_wrapper_native_symbol[0]);

    if (!wasm_native_register_natives("env", sym,  num)) {
        return -1;
    }

    return 0;
}

esp_err_t wm_ext_wasm_native_lvgl_register_ops(wm_ext_wasm_native_lvgl_ops_t *ops)
{
    if (!ops) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_lvgl_ops, ops, sizeof(wm_ext_wasm_native_lvgl_ops_t));

    return ESP_OK;
}

WM_EXT_WASM_NATIVE_EXPORT_FN(wm_ext_wasm_native_lvgl_export)
{
    return wm_ext_wasm_native_lvgl_export();
}

#if CONFIG_WASMACHINE_WASM_EXT_NATIVE_LVGL_USE_WASM_HEAP
void __wrap_lv_mem_init(void)
{
}

void __wrap_lv_mem_deinit(void)
{
}

lv_mem_pool_t __wrap_lv_mem_add_pool(void *mem, size_t bytes)
{
    return NULL;
}

void __wrap_lv_mem_remove_pool(lv_mem_pool_t pool)
{
}

void *__wrap_lv_malloc_core(size_t size)
{
    // add a pointer to the beginning of the memory to store the module instance
    // and a size_t to store the original allocation size
    size_t size_with_header = size + sizeof(uintptr_t) + sizeof(size_t);
    uintptr_t *p = NULL;
    wasm_module_inst_t module_inst = (wasm_module_inst_t)pvTaskGetThreadLocalStoragePointer(NULL, LVGL_WASM_TASK_LOCAL_STORAGE_INDEX);
    if (module_inst) {
        if (module_malloc(size_with_header, (void **)&p)) {
            // Check if allocation succeeded
            if (!p) {
                return NULL;
            }

            // store the module instance pointer at the beginning of the memory
            p[0] = (uintptr_t)module_inst;
            // store the original allocation size
            ((size_t *)&p[1])[0] = size;
        } else {
            // module_malloc failed
            return NULL;
        }
    } else {
        p = heap_caps_malloc(size_with_header, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        if (!p) {
            return NULL;
        }

        p[0] = 0;
        // store the original allocation size
        ((size_t *)&p[1])[0] = size;
    }

    return (void *) & ((size_t *)&p[1])[1];
}

void __wrap_lv_free_core(void *p)
{
    if (!p) {
        return;
    }

    void *p_native = (void *)((char *)p - sizeof(size_t) - sizeof(uintptr_t));
    wasm_module_inst_t module_inst = *(void **)p_native;
    if (module_inst) {
        module_free(addr_native_to_app(p_native));
    } else {
        heap_caps_free(p_native);
    }
}

void *__wrap_lv_realloc_core(void *p, size_t new_size)
{
    // if new_size is 0, free the memory
    if (!new_size) {
        if (p) {
            __wrap_lv_free_core(p);
        }
        return NULL;
    }

    size_t old_size = 0;
    wasm_module_inst_t module_inst = NULL;

    // if p is not NULL, retrieve the old size and module instance from the header
    if (p) {
        void *p_native = (void *)((char *)p - sizeof(size_t) - sizeof(uintptr_t));
        module_inst = *(wasm_module_inst_t *)p_native;
        old_size = ((size_t *)((uintptr_t *)p_native + 1))[0];
    } else {
        // if p is NULL, get module instance from thread local storage
        module_inst = (wasm_module_inst_t)pvTaskGetThreadLocalStoragePointer(NULL, LVGL_WASM_TASK_LOCAL_STORAGE_INDEX);
    }

    // add a pointer to the beginning of the memory to store the module instance
    // and a size_t to store the original allocation size
    size_t new_size_with_header = new_size + sizeof(uintptr_t) + sizeof(size_t);
    uintptr_t *new_p = NULL;
    if (module_inst) {
        if (module_malloc(new_size_with_header, (void **)&new_p)) {
            // store the module instance pointer at the beginning of the memory
            new_p[0] = (uintptr_t)module_inst;
            // store the new allocation size
            ((size_t *)&new_p[1])[0] = new_size;
        }
    } else {
        new_p = heap_caps_malloc(new_size_with_header, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        if (new_p) {
            new_p[0] = 0;
            // store the new allocation size
            ((size_t *)&new_p[1])[0] = new_size;
        }
    }

    // Check if allocation failed
    if (!new_p) {
        // Allocation failed, return NULL (original pointer p remains unchanged)
        return NULL;
    }

    if (new_p && p && new_size) {
        void *p_native = (void *)((char *)p - sizeof(size_t) - sizeof(uintptr_t));
        // Only copy and free old memory if the allocation actually moved
        if (new_p != p_native) {
            // copy only min(old_size, new_size) bytes to prevent OOB reads
            size_t copy_size = (old_size < new_size) ? old_size : new_size;
            memcpy(&((size_t *)&new_p[1])[1], p, copy_size);
            __wrap_lv_free_core(p);
        }
    }

    return (void *) & ((size_t *)&new_p[1])[1];
}

void __wrap_lv_mem_monitor_core(lv_mem_monitor_t *mon_p)
{
}

lv_result_t __wrap_lv_mem_test_core(void)
{
    return LV_RESULT_OK;
}
#endif
