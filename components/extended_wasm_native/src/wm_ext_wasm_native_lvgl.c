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

#include <sys/lock.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bh_platform.h"
#include "wasm_export.h"
#include "wasm_native.h"
#include "wasm_runtime_common.h"

#include "wm_ext_wasm_native_macro.h"
#include "wm_ext_wasm_native_export.h"
#include "wm_ext_wasm_native_lvgl.h"

#include "bsp_board.h"
#include "lvgl.h"

#define LVGL_ARG_BUF_NUM        16
#define LVGL_ARG_NUM_MAX        64

#define LVGL_WASM_CALLBACK_STACK_SIZE   (8192)

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

typedef void (*lvgl_func_t)(wasm_exec_env_t exec_env, uint32_t *args, uint32_t *args_ret);

typedef struct lvgl_func_desc {
    lvgl_func_t     func;
    uint32_t        argc;
} lvgl_func_desc_t;

static const char *TAG = "wm_lvgl_wrapper";

static bool lvgl_inited;
static _lock_t lvgl_lock;

static int lvgl_init_wrapper(wasm_exec_env_t exec_env)
{
    _lock_acquire_recursive(&lvgl_lock);
    if (!lvgl_inited) {
        bsp_i2c_init();
        bsp_display_start();
        bsp_display_backlight_on();
        lvgl_inited = true;
    }
    _lock_release_recursive(&lvgl_lock);

    return 0;
}

static void lvgl_lock_wrapper(void)
{
    bsp_display_lock(0);
}

static void lvgl_unlock_wrapper(void)
{
    bsp_display_unlock();
}

static bool ptr_is_in_ram_or_rom(const void *ptr)
{
    bool ret = false;

    if (esp_ptr_in_dram(ptr)) {
        ret = true;
    } else if (esp_ptr_in_drom(ptr)) {
        ret = true;
    } else if (esp_ptr_external_ram(ptr)) {
        ret = true;
    }

    return ret;
}

static lv_style_t *map_style(wasm_exec_env_t exec_env, lv_style_t *style)
{
    void *ptr;
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    ptr = addr_app_to_native((uint32_t)style);
    if (!ptr) {
        ESP_LOGE(TAG, "failed to map style=%p", style);
        LVGL_TRACE_ABORT();
    } else {
        style = ptr;
    }

    return style;
}

static lv_anim_t *map_anim(wasm_exec_env_t exec_env, lv_anim_t *anim)
{
    void *ptr;
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    ptr = addr_app_to_native((uint32_t)anim);
    if (!ptr) {
        ESP_LOGE(TAG, "failed to map anim=%p", anim);
        LVGL_TRACE_ABORT();
    } else {
        anim = ptr;
    }

    return anim;
}

static const lv_font_t *map_font(wasm_exec_env_t exec_env, const lv_font_t *_font)
{
    void *ptr;
    lv_font_t *font = (lv_font_t *)_font;
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (ptr_is_in_ram_or_rom(font)) {
        return font;
    }

    ptr = addr_app_to_native((uint32_t)font);
    if (!ptr) {
        ESP_LOGE(TAG, "failed to map font=%p", font);
        LVGL_TRACE_ABORT();
    } else {
        font = ptr;
    }

    font->env = exec_env;

    return font;
}

static lv_img_dsc_t *map_img_dsc(wasm_exec_env_t exec_env, lv_img_dsc_t *img_dsc)
{
    void *ptr;
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (ptr_is_in_ram_or_rom(img_dsc)) {
        return img_dsc;
    }

    ptr = addr_app_to_native((uint32_t)img_dsc);
    if (!ptr) {
        ESP_LOGE(TAG, "failed to map img_dsc=%p", img_dsc);
        LVGL_TRACE_ABORT();
    } else {
        img_dsc = ptr;
    }

    img_dsc->env = exec_env;

    return img_dsc;
}

static lv_point_t *map_points(wasm_exec_env_t exec_env, lv_point_t *point)
{
    void *ptr;
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (ptr_is_in_ram_or_rom(point)) {
        return point;
    }

    ptr = (lv_point_t *)addr_app_to_native((uint32_t)point);
    if (!ptr) {
        ESP_LOGE(TAG, "failed to map point=%p", point);
        LVGL_TRACE_ABORT();
    } else {
        point = ptr;
    }

    return point;
}

static const char *map_string(wasm_exec_env_t exec_env, const char *s)
{
    void *ptr;
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (ptr_is_in_ram_or_rom(s)) {
        return s;
    }

    ptr = (char *)addr_app_to_native((uint32_t)s);
    if (!ptr) {
        ESP_LOGE(TAG, "failed to map s=%p", s);
        LVGL_TRACE_ABORT();
    } else {
        s = ptr;
    }

    return s;
}

static void *map_ptr(wasm_exec_env_t exec_env, const void *app_addr)
{
    void *ptr;
    wasm_module_inst_t module_inst = get_module_inst(exec_env);

    if (ptr_is_in_ram_or_rom(app_addr)) {
        return (void *)app_addr;
    }

    ptr = (char *)addr_app_to_native((uint32_t)app_addr);
    if (!ptr) {
        ESP_LOGE(TAG, "failed to map app_addr=%p", app_addr);
        LVGL_TRACE_ABORT();
    } else {
        app_addr = ptr;
    }

    return (void *)app_addr; 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_disp_get_next)
{
    lv_disp_t *res;
    lvgl_native_return_type(lv_disp_t *);
    lvgl_native_get_arg(lv_disp_t *, disp);

    res = lv_disp_get_next(disp);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_disp_get_default)
{
    lv_disp_t *res;
    lvgl_native_return_type(lv_disp_t *);

    res = lv_disp_get_default();

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_disp_get_scr_act)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_disp_t *, disp);

    res = lv_disp_get_scr_act(disp);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_disp_get_hor_res)
{
    lv_coord_t res;
    lvgl_native_return_type(lv_coord_t);
    lvgl_native_get_arg(lv_disp_t *, disp);

    res = lv_disp_get_hor_res(disp);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_disp_get_ver_res)
{
    lv_coord_t res;
    lvgl_native_return_type(lv_coord_t);
    lvgl_native_get_arg(lv_disp_t *, disp);

    res = lv_disp_get_ver_res(disp);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_disp_set_monitor_cb)
{
    lvgl_native_get_arg(lv_disp_t *, disp);
    lvgl_native_get_arg(void *, cb);

    disp->driver->monitor_cb = cb;
    disp->driver->env = exec_env;
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_remove_style)
{
    lvgl_native_get_arg(lv_obj_t   *, obj);
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
    lvgl_native_get_arg(lv_coord_t, x);
    lvgl_native_get_arg(lv_coord_t, y);

    lv_obj_set_pos(obj, x, y);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_align_to)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_obj_t *, base);
    lvgl_native_get_arg(lv_align_t, align);
    lvgl_native_get_arg(lv_coord_t, x_ofs);
    lvgl_native_get_arg(lv_coord_t, y_ofs);

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
    lv_coord_t res;
    lvgl_native_return_type(lv_coord_t);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_obj_get_width(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_height)
{
    lv_coord_t res;
    lvgl_native_return_type(lv_coord_t);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_obj_get_height(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_size)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_coord_t, w);
    lvgl_native_get_arg(lv_coord_t, h);

    lv_obj_set_size(obj, w, h);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_align)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_align_t, align);
    lvgl_native_get_arg(lv_coord_t, x_ofs);
    lvgl_native_get_arg(lv_coord_t, y_ofs);

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
    lv_coord_t res;
    lvgl_native_return_type(lv_coord_t);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_obj_get_content_width(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_width)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_coord_t, w);

    lv_obj_set_width(obj, w);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_line_color)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_color_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_line_color(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_arc_color)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_color_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_arc_color(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_img_recolor)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_color_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_img_recolor(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_text_color)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_color_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_text_color(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_x)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_coord_t, x);

    lv_obj_set_x(obj, x);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_y)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_coord_t, y);

    lv_obj_set_y(obj, y);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_add_style)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    style = map_style(exec_env, style);

    lv_obj_add_style(obj, style, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_bg_color)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_color_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_bg_color(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_border_color)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_color_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_border_color(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_shadow_color)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_color_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

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

    txt = map_string(exec_env, txt);

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

DEFINE_LVGL_NATIVE_WRAPPER(lv_table_set_col_cnt)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint16_t, col_cnt);

    lv_table_set_col_cnt(obj, col_cnt);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_table_set_col_width)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint16_t, col_id);
    lvgl_native_get_arg(lv_coord_t, w);

    lv_table_set_col_width(obj, col_id, w);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_table_add_cell_ctrl)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint16_t, row);
    lvgl_native_get_arg(uint16_t, col);
    lvgl_native_get_arg(lv_table_cell_ctrl_t, ctrl);

    lv_table_add_cell_ctrl(obj, row, col, ctrl);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_table_set_cell_value)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint16_t, row);
    lvgl_native_get_arg(uint16_t, col);
    lvgl_native_get_arg(const char *, txt);

    txt = map_string(exec_env, txt);   

    lv_table_set_cell_value(obj, row, col, txt);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_timer_create)
{
    lv_timer_t *res;
    lvgl_native_return_type(lv_timer_t *);
    lvgl_native_get_arg(lv_timer_cb_t, timer_xcb);
    lvgl_native_get_arg(uint32_t, period);
    lvgl_native_get_arg(void *, user_data);

    _lock_acquire_recursive(&lvgl_lock);
    res = lv_timer_create(timer_xcb, period, user_data);
    res->env = exec_env;
    _lock_release_recursive(&lvgl_lock);

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

    style = map_style(exec_env, style);

    lv_style_init(style);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_reset)
{
    lvgl_native_get_arg(lv_style_t *, style);

    style = map_style(exec_env, style);

    lv_style_reset(style);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_bg_opa)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_opa_t, value);

    style = map_style(exec_env, style);

    lv_style_set_bg_opa(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_radius)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_coord_t, value);

    style = map_style(exec_env, style);

    lv_style_set_radius(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_border_width)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_coord_t, value);

    style = map_style(exec_env, style);

    lv_style_set_border_width(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_border_opa)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_opa_t, value);

    style = map_style(exec_env, style);

    lv_style_set_border_opa(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_border_side)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_border_side_t, value);

    style = map_style(exec_env, style);

    lv_style_set_border_side(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_shadow_opa)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_opa_t, value);

    style = map_style(exec_env, style);

    lv_style_set_shadow_opa(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_shadow_width)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_coord_t, value);

    style = map_style(exec_env, style);

    lv_style_set_shadow_width(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_shadow_ofs_x)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_coord_t, value);

    style = map_style(exec_env, style);

    lv_style_set_shadow_ofs_x(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_shadow_ofs_y)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_coord_t, value);

    style = map_style(exec_env, style);

    lv_style_set_shadow_ofs_y(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_shadow_spread)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_coord_t, value);

    style = map_style(exec_env, style);

    lv_style_set_shadow_spread(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_img_opa)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_opa_t, value);

    style = map_style(exec_env, style);

    lv_style_set_img_opa(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_img_recolor_opa)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_opa_t, value);

    style = map_style(exec_env, style);

    lv_style_set_img_recolor_opa(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_text_font)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(const lv_font_t *, font);

    style = map_style(exec_env, style);

    font = map_font(exec_env, font);

    lv_style_set_text_font(style, font);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_text_opa)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_opa_t, value);

    style = map_style(exec_env, style);

    lv_style_set_text_opa(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_line_width)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_coord_t, value);

    style = map_style(exec_env, style);

    lv_style_set_line_width(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_line_opa)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_opa_t, value);

    style = map_style(exec_env, style);

    lv_style_set_line_opa(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_arc_width)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_coord_t, value);

    style = map_style(exec_env, style);

    lv_style_set_arc_width(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_arc_opa)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_opa_t, value);

    style = map_style(exec_env, style);

    lv_style_set_arc_opa(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_blend_mode)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_blend_mode_t, value);

    style = map_style(exec_env, style);

    lv_style_set_blend_mode(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_text_color)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_color_t, value);

    style = map_style(exec_env, style);

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
    lvgl_native_get_arg(lv_point_t *, points);
    lvgl_native_get_arg(uint16_t, point_num);

    points = map_points(exec_env, points);

    lv_line_set_points(obj, points, point_num);
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
    lvgl_native_get_arg(uint16_t, start);

    lv_arc_set_start_angle(arc, start);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_arc_set_end_angle)
{
    lvgl_native_get_arg(lv_obj_t *, arc);
    lvgl_native_get_arg(uint16_t, end);

    lv_arc_set_start_angle(arc, end);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_img_create)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, parent);

    res = lv_img_create(parent);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_img_set_src)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_img_dsc_t *, src);

    src = map_img_dsc(exec_env, src);

    lv_img_set_src(obj, src);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_img_set_angle)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int16_t, angle);

    lv_img_set_angle(obj, angle);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_img_set_zoom)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int16_t, zoom);

    lv_img_set_zoom(obj, zoom);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_img_set_antialias)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(bool, antialias);

    lv_img_set_antialias(obj, antialias);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_init)
{
    lvgl_native_get_arg(lv_anim_t *, a);

    a = map_anim(exec_env, a);

    lv_anim_init(a);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_start)
{
    lv_anim_t *res;
    lvgl_native_return_type(lv_anim_t *);
    lvgl_native_get_arg(lv_anim_t *, a);

    a = map_anim(exec_env, a);

    a->env = exec_env;
    res = lv_anim_start(a);

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
    lvgl_native_get_arg(lv_disp_t *, disp);
    lvgl_native_get_arg(lv_color_t, color_primary);
    lvgl_native_get_arg(lv_color_t, color_secondary);
    lvgl_native_get_arg(bool, dark);
    lvgl_native_get_arg(const lv_font_t *, font);

    font = map_font(exec_env, font);

    res = lv_theme_default_init(disp, color_primary, color_secondary, dark, font);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_theme_get_color_primary)
{
    lv_color_t res;
    lvgl_native_return_type(lv_color_t);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_theme_get_color_primary(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_font_get_bitmap_fmt_txt)
{
    const uint8_t *res;
    lvgl_native_return_type(const uint8_t *);
    lvgl_native_get_arg(const lv_font_t *, font);
    lvgl_native_get_arg(uint32_t, letter);

    res = lv_font_get_bitmap_fmt_txt(font, letter);

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

    res = lv_font_get_glyph_dsc_fmt_txt(font, dsc_out, unicode_letter, unicode_letter_next);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_palette_main)
{
    lv_color_t res;
    lvgl_native_return_type(lv_color_t);
    lvgl_native_get_arg(lv_palette_t, p);

    res = lv_palette_main(p);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_tabview_create)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, parent);
    lvgl_native_get_arg(lv_dir_t, tab_pos);
    lvgl_native_get_arg(lv_coord_t, tab_size);

    res = lv_tabview_create(parent, tab_pos, tab_size);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_text_font)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const lv_font_t *, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    value = map_font(exec_env, value);

    lv_obj_set_style_text_font(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_tabview_get_tab_btns)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_tabview_get_tab_btns(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_pad_left)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_coord_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_pad_left(obj, value, selector);  
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_tabview_add_tab)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, tv);
    lvgl_native_get_arg(const char *, name);

    name = map_string(exec_env, name);

    res = lv_tabview_add_tab(tv, name);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_height)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_coord_t, value);

    lv_obj_set_height(obj, value);  
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_label_set_long_mode)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_label_long_mode_t, value);

    lv_label_set_long_mode(obj, value); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_btn_create)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_btn_create(obj);

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
    lv_obj_t * res;
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
    lv_obj_t * res;
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

    value = map_string(exec_env, value);

    lv_textarea_set_placeholder_text(obj, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_add_event_cb)
{
    struct _lv_event_dsc_t * res;
    lvgl_native_return_type(struct _lv_event_dsc_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_event_cb_t, event_cb);
    lvgl_native_get_arg(lv_event_code_t, filter);
    lvgl_native_get_arg(void *, user_data);

    res = lv_obj_add_event_cb(obj, event_cb, filter, user_data);
    res->env = exec_env; 

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
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_dropdown_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_dropdown_set_options_static)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const char *, value);

    value = map_string(exec_env, value);

    lv_dropdown_set_options_static(obj, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_slider_create)
{
    lv_obj_t * res;
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
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_switch_create(obj);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_grid_dsc_array)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const lv_coord_t *, col_dsc);
    lvgl_native_get_arg(const lv_coord_t *, row_dsc);

    col_dsc = map_ptr(exec_env, col_dsc);
    row_dsc = map_ptr(exec_env, row_dsc);

    lv_obj_set_grid_dsc_array(obj, col_dsc, row_dsc);  
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_grid_cell)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_grid_align_t, column_align);
    lvgl_native_get_arg(uint8_t, col_pos);
    lvgl_native_get_arg(uint8_t, col_span);
    lvgl_native_get_arg(lv_grid_align_t, row_align);
    lvgl_native_get_arg(uint8_t, row_pos);
    lvgl_native_get_arg(uint8_t, row_span);

    lv_obj_set_grid_cell(obj, column_align, col_pos, col_span, row_align, row_pos, row_span);  
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
    lvgl_native_get_arg(lv_coord_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_max_height(obj, value, selector);   
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_create)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_chart_create(obj);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_group_get_default)
{
    lv_group_t * res;
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

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_set_axis_tick)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_chart_axis_t, axis);
    lvgl_native_get_arg(lv_coord_t, major_len);
    lvgl_native_get_arg(lv_coord_t, minor_len);
    lvgl_native_get_arg(lv_coord_t, major_cnt);
    lvgl_native_get_arg(lv_coord_t, minor_cnt);
    lvgl_native_get_arg(bool, label_en);
    lvgl_native_get_arg(lv_coord_t, draw_size);

    lv_chart_set_axis_tick(obj, axis, major_len, minor_len, major_cnt, minor_cnt, label_en, draw_size); 
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
    lvgl_native_get_arg(uint16_t, value);

    lv_chart_set_point_count(obj, value); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_set_zoom_x)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint16_t, value);

    lv_chart_set_zoom_x(obj, value); 
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
    lv_chart_series_t * res;
    lvgl_native_return_type(lv_chart_series_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_color_t, color);
    lvgl_native_get_arg(lv_chart_axis_t, axis);

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
    lvgl_native_get_arg(lv_coord_t, value);

    lv_chart_set_next_value(obj, ser, value); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_set_type)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_chart_type_t , type);

    lv_chart_set_type(obj, type);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_pad_row)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_coord_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_pad_row(obj, value, selector); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_pad_column)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_coord_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_pad_column(obj, value, selector); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_palette_lighten)
{
    lv_color_t res;
    lvgl_native_return_type(lv_color_t);
    lvgl_native_get_arg(lv_palette_t, p);
    lvgl_native_get_arg(uint8_t, lvl);

    res = lv_palette_lighten(p, lvl);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_parent)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_obj_get_parent(obj);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_meter_add_scale)
{
    lv_meter_scale_t * res;
    lvgl_native_return_type(lv_meter_scale_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_meter_add_scale(obj);

    lvgl_native_set_return(res);  
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_meter_set_scale_range)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_meter_scale_t *, scale);
    lvgl_native_get_arg(int32_t, min);
    lvgl_native_get_arg(int32_t, max);
    lvgl_native_get_arg(uint32_t, angle_range);
    lvgl_native_get_arg(uint32_t, rotation);

    lv_meter_set_scale_range(obj, scale, min, max, angle_range, rotation);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_meter_set_scale_ticks)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_meter_scale_t *, scale);
    lvgl_native_get_arg(uint16_t, cnt);
    lvgl_native_get_arg(uint16_t, width);
    lvgl_native_get_arg(uint16_t, len);
    lvgl_native_get_arg(lv_color_t, color);

    lv_meter_set_scale_ticks(obj, scale, cnt, width, len, color);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_meter_add_arc)
{
    lv_meter_indicator_t * res;
    lvgl_native_return_type(lv_meter_indicator_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_meter_scale_t *, scale);
    lvgl_native_get_arg(uint16_t, width);
    lvgl_native_get_arg(lv_color_t, color);
    lvgl_native_get_arg(uint16_t, r_mod);

    res = lv_meter_add_arc(obj, scale, width, color, r_mod);

    lvgl_native_set_return(res);  
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_meter_set_indicator_start_value)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_meter_indicator_t *, indic);
    lvgl_native_get_arg(int32_t, value);

    lv_meter_set_indicator_start_value(obj, indic, value); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_meter_set_indicator_end_value)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_meter_indicator_t *, indic);
    lvgl_native_get_arg(int32_t, value);

    lv_meter_set_indicator_end_value(obj, indic, value);   
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_pad_right)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_coord_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_pad_right(obj, value, selector); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_width)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_coord_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_width(obj, value, selector);   
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_height)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_coord_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_height(obj, value, selector);   
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_palette_darken)
{
    lv_color_t res;
    lvgl_native_return_type(lv_color_t);
    lvgl_native_get_arg(lv_palette_t, p);
    lvgl_native_get_arg(uint8_t, lvl);

    res = lv_palette_darken(p, lvl);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_outline_color)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_color_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_outline_color(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_outline_width)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_coord_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_outline_width(obj, value, selector);   
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_meter_set_scale_major_ticks)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_meter_scale_t *, scale);
    lvgl_native_get_arg(uint16_t, cnt);
    lvgl_native_get_arg(uint16_t, width);
    lvgl_native_get_arg(uint16_t, len);
    lvgl_native_get_arg(lv_color_t, color);
    lvgl_native_get_arg(int16_t, label_gap);

    lv_meter_set_scale_major_ticks(obj, scale, cnt, width, len, color, label_gap);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_meter_add_scale_lines)
{
    lv_meter_indicator_t * res;
    lvgl_native_return_type(lv_meter_indicator_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_meter_scale_t *, scale);
    lvgl_native_get_arg(lv_color_t, color_start);
    lvgl_native_get_arg(lv_color_t, color_end);
    lvgl_native_get_arg(bool, local);
    lvgl_native_get_arg(int16_t, width_mod);

    res = lv_meter_add_scale_lines(obj, scale, color_start, color_end, local, width_mod);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_pad_bottom)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_coord_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_pad_bottom(obj, value, selector); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_disp_get_dpi)
{
    lv_coord_t res;
    lvgl_native_return_type(lv_coord_t);
    lvgl_native_get_arg(const lv_disp_t *, disp);

    res = lv_disp_get_dpi(disp);

    lvgl_native_set_return(res);  
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_checkbox_create)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_checkbox_create(obj);

    lvgl_native_set_return(res);  
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_checkbox_set_text)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const char *, value);

    value = map_string(exec_env, value);

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

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_clear_flag)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_obj_flag_t , f);

    lv_obj_clear_flag(obj, f);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_pad_top)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_coord_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_pad_top(obj, value, selector); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_shadow_width)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_coord_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_shadow_width(obj, value, selector); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_bg_img_src)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_img_dsc_t *, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    value = map_ptr(exec_env, value);
    value->env = exec_env;

    lv_obj_set_style_bg_img_src(obj, value, selector); 
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
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_event_t *, e);

    res = lv_event_get_target(e);

    lvgl_native_set_return(res);   
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_event_get_user_data)
{
    void *res;
    lvgl_native_return_type(void *);
    lvgl_native_get_arg(lv_event_t *, e);

    res = lv_event_get_user_data(e);

    lvgl_native_set_return(res);    
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_indev_get_act)
{
    lv_indev_t * res;
    lvgl_native_return_type(lv_indev_t *);

    res = lv_indev_get_act();

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_indev_get_type)
{
    lv_indev_type_t res;
    lvgl_native_return_type(lv_indev_type_t);
    lvgl_native_get_arg(const lv_indev_t * , indev);

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

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_clear_state)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_state_t, state);

    lv_obj_clear_state(obj, state);    
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_disp_get_layer_top)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_disp_t *, disp);

    res = lv_disp_get_layer_top(disp);

    lvgl_native_set_return(res);    
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_calendar_create)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_calendar_create(obj);

    lvgl_native_set_return(res);     
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_calendar_set_showed_date)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, year);
    lvgl_native_get_arg(uint32_t, month);

    lv_calendar_set_showed_date(obj, year, month);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_calendar_header_dropdown_create)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_calendar_header_dropdown_create(obj);

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
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_state_t , state);

    lv_obj_has_state(obj, state);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_bar_get_value)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_bar_get_value(obj);

    lvgl_native_set_return(res);  
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_txt_get_size)
{
    lvgl_native_get_arg(lv_point_t *, size_res);
    lvgl_native_get_arg(const char *, text);
    lvgl_native_get_arg(const lv_font_t *, font);
    lvgl_native_get_arg(lv_coord_t, letter_space);
    lvgl_native_get_arg(lv_coord_t, line_space);
    lvgl_native_get_arg(lv_coord_t, max_width);
    lvgl_native_get_arg(lv_text_flag_t, flag);

    size_res = map_ptr(exec_env, size_res);
    text = map_string(exec_env, text);
    font = map_font(exec_env, font);

    lv_txt_get_size(size_res, text, font, letter_space, line_space, max_width, flag); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_rect_dsc_init)
{
    lvgl_native_get_arg(lv_draw_rect_dsc_t *, dsc);

    dsc = map_ptr(exec_env, dsc);

    lv_draw_rect_dsc_init(dsc);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_rect)
{
    lvgl_native_get_arg(lv_area_t *, coords);
    lvgl_native_get_arg(lv_area_t *, clip);
    lvgl_native_get_arg(lv_draw_rect_dsc_t *, dsc);

    coords = map_ptr(exec_env, coords);
    clip = map_ptr(exec_env, clip);
    dsc = map_ptr(exec_env, dsc);

    lv_draw_rect(coords, clip, dsc);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_label_dsc_init)
{
    lvgl_native_get_arg(lv_draw_label_dsc_t *, dsc);

    dsc = map_ptr(exec_env, dsc);

    lv_draw_label_dsc_init(dsc);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_label)
{
    lvgl_native_get_arg(lv_area_t *, coords);
    lvgl_native_get_arg(lv_area_t *, mask);
    lvgl_native_get_arg(lv_draw_label_dsc_t *, dsc);
    lvgl_native_get_arg(const char *, txt);
    lvgl_native_get_arg(lv_draw_label_hint_t *, hint);

    coords = map_ptr(exec_env, coords);
    mask = map_ptr(exec_env, mask);
    dsc = map_ptr(exec_env, dsc);
    txt = map_ptr(exec_env, txt);
    hint = map_ptr(exec_env, hint);

    if (!dsc->font) {
        dsc->font = &lv_font_montserrat_14;
    }

    lv_draw_label(coords, mask, dsc, txt, hint);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_event_get_current_target)
{
    lv_obj_t *res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_event_t *, e);

    res = lv_event_get_current_target(e);

    lvgl_native_set_return(res);  
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_calendar_get_pressed_date)
{
    lv_res_t res;
    lvgl_native_return_type(lv_res_t);
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

    value = map_string(exec_env, value);

    lv_textarea_set_text(obj, value); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_del)
{
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_obj_del(obj); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_invalidate)
{
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_obj_invalidate(obj); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_get_type)
{
    lv_chart_type_t res;
    lvgl_native_return_type(lv_chart_type_t);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_chart_get_type(obj);

    lvgl_native_set_return(res);   
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_mask_line_points_init)
{
    lvgl_native_get_arg(lv_draw_mask_line_param_t *, param);
    lvgl_native_get_arg(lv_coord_t, p1x);
    lvgl_native_get_arg(lv_coord_t, p1y);
    lvgl_native_get_arg(lv_coord_t, p2x);
    lvgl_native_get_arg(lv_coord_t, p2y);
    lvgl_native_get_arg(lv_draw_mask_line_side_t, side);

    param = map_ptr(exec_env, param);

    lv_draw_mask_line_points_init(param, p1x, p1y, p2x, p2y, side);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_mask_add)
{
    int16_t res;
    lvgl_native_return_type(int16_t);
    lvgl_native_get_arg(void *, param);
    lvgl_native_get_arg(void *, custom_id);

    param = map_ptr(exec_env, param);
    custom_id = map_ptr(exec_env, custom_id);

    res = lv_draw_mask_add(param, custom_id);

    lvgl_native_set_return(res);  
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_mask_fade_init)
{
    lvgl_native_get_arg(lv_draw_mask_fade_param_t *, param);
    lvgl_native_get_arg(lv_area_t *, coords);
    lvgl_native_get_arg(lv_opa_t, opa_top);
    lvgl_native_get_arg(lv_coord_t, y_top);
    lvgl_native_get_arg(lv_opa_t, opa_bottom);
    lvgl_native_get_arg(lv_coord_t, y_bottom);

    param = map_ptr(exec_env, param);
    coords = map_ptr(exec_env, coords);

    lv_draw_mask_fade_init(param, coords, opa_top, y_top, opa_bottom, y_bottom);
}

DEFINE_LVGL_NATIVE_WRAPPER(_lv_area_intersect)
{
    bool res;
    lvgl_native_return_type(bool);
    lvgl_native_get_arg(lv_area_t *, res_p);
    lvgl_native_get_arg(lv_area_t *, a1_p);
    lvgl_native_get_arg(lv_area_t *, a2_p);

    res_p = map_ptr(exec_env, res_p);
    a1_p = map_ptr(exec_env, a1_p);
    a2_p = map_ptr(exec_env, a2_p);

    res = _lv_area_intersect(res_p, a1_p, a2_p);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_mask_remove_id)
{
    void * res;
    lvgl_native_return_type(void *);
    lvgl_native_get_arg(int16_t, id);

    res = lv_draw_mask_remove_id(id);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_get_pressed_point)
{
    uint32_t res;
    lvgl_native_return_type(uint32_t);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_chart_get_pressed_point(obj);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_get_series_next)
{
    lv_chart_series_t * res;
    lvgl_native_return_type(lv_chart_series_t *);
    lvgl_native_get_arg(lv_obj_t *, chart);
    lvgl_native_get_arg(lv_chart_series_t *, ser);

    res = lv_chart_get_series_next(chart, ser);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_meter_create)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_meter_create(obj);

    lvgl_native_set_return(res);     
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_child)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, id);

    res = lv_obj_get_child(obj, id);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_meter_set_indicator_value)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_meter_indicator_t *, indic);
    lvgl_native_get_arg(int32_t, value);

    lv_meter_set_indicator_value(obj, indic, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_set_series_color)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_chart_series_t *, series);
    lvgl_native_get_arg(lv_color_t, color);

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

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_get_child_cnt)
{
    uint32_t res;
    lvgl_native_return_type(uint32_t);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_obj_get_child_cnt(obj);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_meter_add_needle_line)
{
    lv_meter_indicator_t * res;
    lvgl_native_return_type(lv_meter_indicator_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_meter_scale_t *, scale);
    lvgl_native_get_arg(uint16_t, width);
    lvgl_native_get_arg(lv_color_t, color);
    lvgl_native_get_arg(int16_t, r_mod);

    res = lv_meter_add_needle_line(obj, scale, width, color, r_mod);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_mem_test)
{
    lv_res_t res;
    lvgl_native_return_type(lv_res_t);

    res = lv_mem_test();

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_mem_monitor)
{
    lvgl_native_get_arg(lv_mem_monitor_t *, mon_p);

    mon_p = map_ptr(exec_env, mon_p);

    lv_mem_monitor(mon_p);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_colorwheel_create)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(bool, knob_recolor);

    res = lv_colorwheel_create(obj, knob_recolor);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_tabview_set_act)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, id);
    lvgl_native_get_arg(lv_anim_enable_t, anim_en);

    lv_tabview_set_act(obj, id, anim_en);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_del_anim_ready_cb)
{
    lvgl_native_get_arg(lv_anim_t *, a);

    a = map_ptr(exec_env, a);

    lv_obj_del_anim_ready_cb(a);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_del_async)
{
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_obj_del_async(obj);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_bar_create)
{
    lv_obj_t * res;
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

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_style_anim_time)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_anim_time(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_win_create)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_coord_t, header_height);

    res = lv_win_create(obj, header_height);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_win_add_title)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const char *, txt);

    txt = map_ptr(exec_env, txt);

    res = lv_win_add_title(obj, txt);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_win_add_btn)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const void *, icon);
    lvgl_native_get_arg(lv_coord_t, btn_w);

    icon = map_ptr(exec_env, icon);

    res = lv_win_add_btn(obj, icon, btn_w);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_win_get_content)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_win_get_content(obj);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_keyboard_set_mode)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(int32_t, mode);

    lv_keyboard_set_mode(obj, mode);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_dropdown_set_options)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const char *, options);

    options = map_ptr(exec_env, options);

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
    lvgl_native_get_arg(uint16_t, sel_opt);

    lv_dropdown_set_selected(obj, sel_opt);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_roller_create)
{
    lv_obj_t * res;
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

    options = map_ptr(exec_env, options);

    lv_roller_set_options(obj, options, mode);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_roller_set_selected)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint16_t, sel_opt);
    lvgl_native_get_arg(lv_anim_enable_t, anim);

    lv_roller_set_selected(obj, sel_opt, anim);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_msgbox_create)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const char *, title);
    lvgl_native_get_arg(const char *, txt);
    lvgl_native_get_arg(const char **, origin_btn_txts);
    lvgl_native_get_arg(bool, add_close_btn);

    int btn_txts_num = 0;
    const char **btn_txts;

    title = map_ptr(exec_env, title);
    txt = map_ptr(exec_env, txt);
    origin_btn_txts = map_ptr(exec_env, origin_btn_txts);

    while (1) {
        const char *t = map_ptr(exec_env, origin_btn_txts[btn_txts_num++]);
        if (!t[0]) {
            break;
        }

        if (btn_txts_num > UINT8_MAX) {
            break;
        }
    }

    btn_txts = wasm_runtime_malloc(sizeof(char *) * btn_txts_num);
    for (int i = 0; i < btn_txts_num - 1; i++) {
        btn_txts[i] = map_ptr(exec_env, origin_btn_txts[i]);
    }
    btn_txts[btn_txts_num - 1] = NULL;

    res = lv_msgbox_create(obj, title, txt, btn_txts, add_close_btn);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_tileview_create)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_tileview_create(obj);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_tileview_add_tile)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint8_t, row_id);
    lvgl_native_get_arg(uint8_t, col_id);
    lvgl_native_get_arg(lv_dir_t, dir);

    res = lv_tileview_add_tile(obj, row_id, col_id, dir);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_tile_id)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint32_t, col_id);
    lvgl_native_get_arg(uint32_t, row_id);
    lvgl_native_get_arg(lv_anim_enable_t, anim_en);

    lv_obj_set_tile_id(obj, col_id, row_id, anim_en);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_list_create)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_list_create(obj);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_list_add_btn)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(const char *, icon);
    lvgl_native_get_arg(const char *, txt);

    icon = map_ptr(exec_env, icon);
    txt = map_ptr(exec_env, txt);

    res = lv_list_add_btn(obj, icon, txt);

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

    txt = map_ptr(exec_env, txt);

    lv_textarea_add_text(obj, txt);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_spinbox_create)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_spinbox_create(obj);

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_spinbox_set_digit_format)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(uint8_t, digit_count);
    lvgl_native_get_arg(uint8_t, separator_position);

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
    lvgl_native_get_arg(lv_coord_t, x);
    lvgl_native_get_arg(lv_coord_t, y);
    lvgl_native_get_arg(lv_anim_enable_t, anim_en);

    lv_obj_scroll_by(obj, x, y, anim_en);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_textarea_del_char_forward)
{
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_textarea_del_char_forward(obj);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_msgbox_close)
{
    lvgl_native_get_arg(lv_obj_t *, obj);

    lv_msgbox_t *msgbox = (lv_msgbox_t *)obj;
    lv_btnmatrix_t *btns = (lv_btnmatrix_t *)msgbox->btns;
    char *btn_txts = (char *)btns->map_p;

    lv_msgbox_close(obj);

    if (btn_txts) {
        wasm_runtime_free(btn_txts);
    }
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_width)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_coord_t, value);

    style = map_ptr(exec_env, style);

    lv_style_set_width(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_bg_color)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_color_t, value);

    style = map_ptr(exec_env, style);

    lv_style_set_bg_color(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_pad_right)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_coord_t, value);

    style = map_ptr(exec_env, style);

    lv_style_set_pad_right(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_grid_column_dsc_array)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_coord_t *, value);

    style = map_ptr(exec_env, style);
    value = map_ptr(exec_env, value);

    lv_style_set_grid_column_dsc_array(style, value);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_style_set_grid_row_dsc_array)
{
    lvgl_native_get_arg(lv_style_t *, style);
    lvgl_native_get_arg(lv_coord_t *, value);

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
    uint32_t res;
    lvgl_native_return_type(uint32_t);
    lvgl_native_get_arg(lv_obj_t *, obj);

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
    lvgl_native_get_arg(lv_coord_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

    lv_obj_set_style_border_width(obj, value, selector);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_set_scroll_dir)
{
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_dir_t, dir);

    lv_obj_set_scroll_dir(obj, dir);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_imgbtn_create)
{
    lv_obj_t * res;
    lvgl_native_return_type(lv_obj_t *);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_imgbtn_create(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_imgbtn_set_src)
{
    lvgl_native_get_arg(lv_obj_t *, imgbtn);
    lvgl_native_get_arg(lv_imgbtn_state_t, state);
    lvgl_native_get_arg(const void *, src_left);
    lvgl_native_get_arg(const void *, src_mid);
    lvgl_native_get_arg(const void *, src_right);

    if (src_left) {
        src_left = map_ptr(exec_env, src_left);
        lv_img_dsc_t *dsc = (lv_img_dsc_t *)src_left;
        dsc->env = exec_env;
    }

    if (src_mid) {
        src_mid = map_ptr(exec_env, src_mid);
        lv_img_dsc_t *dsc = (lv_img_dsc_t *)src_mid;
        dsc->env = exec_env;
    }

    if (src_right) {
        src_right = map_ptr(exec_env, src_right);
        lv_img_dsc_t *dsc = (lv_img_dsc_t *)src_right;
        dsc->env = exec_env;
    }

    lv_imgbtn_set_src(imgbtn, state, src_left, src_mid, src_right);
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
    lvgl_native_get_arg(lv_color_t, value);
    lvgl_native_get_arg(lv_style_selector_t, selector);

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
    lvgl_native_get_arg(lv_anim_t *, a);

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
    lvgl_native_get_arg(lv_anim_t *, a);

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
    lvgl_native_get_arg(lv_coord_t, value);
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
    lvgl_native_get_arg(lv_anim_t *, a);

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

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_del)
{
    bool res;
    lvgl_native_return_type(bool);
    lvgl_native_get_arg(void *, var);
    lvgl_native_get_arg(lv_anim_exec_xcb_t, exec_cb);

    res = lv_anim_del(var, exec_cb);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_event_set_ext_draw_size)
{
    lvgl_native_get_arg(lv_event_t *, e);
    lvgl_native_get_arg(lv_coord_t, size);

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
    lvgl_native_get_arg(lv_obj_t *, obj);
    lvgl_native_get_arg(lv_part_t, part);
    lvgl_native_get_arg(lv_style_prop_t, prop);

    res = lv_obj_get_style_prop(obj, part, prop);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_img_get_zoom)
{
    uint16_t res;
    lvgl_native_return_type(uint16_t);
    lvgl_native_get_arg(lv_obj_t *, obj);

    res = lv_img_get_zoom(obj);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_trigo_sin)
{
    int16_t res;
    lvgl_native_return_type(int16_t);
    lvgl_native_get_arg(int16_t, angle);

    res = lv_trigo_sin(angle);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_draw_polygon)
{
    lvgl_native_get_arg(lv_point_t *, points);
    lvgl_native_get_arg(uint16_t, point_cnt);
    lvgl_native_get_arg(lv_area_t *, mask);
    lvgl_native_get_arg(lv_draw_rect_dsc_t *, draw_dsc);

    points = map_ptr(exec_env, points);
    mask = map_ptr(exec_env, mask);
    draw_dsc = map_ptr(exec_env, draw_dsc);

    lv_draw_polygon(points, point_cnt, mask, draw_dsc);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_indev_get_gesture_dir)
{
    lv_dir_t res;
    lvgl_native_return_type(lv_dir_t);
    lvgl_native_get_arg(lv_indev_t *, indev);

    res = lv_indev_get_gesture_dir(indev);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_anim_path_ease_in)
{
    int32_t res;
    lvgl_native_return_type(int32_t);
    lvgl_native_get_arg(lv_anim_t *, a);

    a = map_ptr(exec_env, a);

    res = lv_anim_path_ease_in(a);

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_timer_get_user_data)
{
    void * res;
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

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_draw_part_dsc_get_data)
{
    int res = -1;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(lv_obj_draw_part_dsc_t *, dsc);
    lvgl_native_get_arg(int, type);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    pdata = map_ptr(exec_env, pdata);

    if (type == LV_OBJ_DRAW_PART_DSC_TYPE && n == sizeof(dsc->type)) {
        memcpy(pdata, &dsc->type, sizeof(dsc->type));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_PART && n == sizeof(dsc->part)) {
        memcpy(pdata, &dsc->part, sizeof(dsc->part));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_ID && n == sizeof(dsc->id)) {
        memcpy(pdata, &dsc->id, sizeof(dsc->id));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_VALUE && n == sizeof(dsc->value)) {
        memcpy(pdata, &dsc->value, sizeof(dsc->value));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_P1 && n == sizeof(*dsc->p1) && dsc->p1) {
        memcpy(pdata, dsc->p1, sizeof(*dsc->p1));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_P2 && n == sizeof(*dsc->p2) && dsc->p2) {
        memcpy(pdata, dsc->p2, sizeof(*dsc->p2));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_CLIP_AREA && n == sizeof(*dsc->clip_area) && dsc->clip_area) {
        memcpy(pdata, dsc->clip_area, sizeof(*dsc->clip_area));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_DRAW_AREA && n == sizeof(*dsc->draw_area) && dsc->draw_area) {
        memcpy(pdata, dsc->draw_area, sizeof(*dsc->draw_area));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_RECT_DSC && n == sizeof(*dsc->rect_dsc) && dsc->rect_dsc) {
        memcpy(pdata, dsc->rect_dsc, sizeof(*dsc->rect_dsc));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_LINE_DSC && n == sizeof(*dsc->line_dsc) && dsc->line_dsc) {
        memcpy(pdata, dsc->line_dsc, sizeof(*dsc->line_dsc));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_SUB_PART_PTR && n == sizeof(dsc->sub_part_ptr)) {
        memcpy(pdata, &dsc->sub_part_ptr, sizeof(dsc->sub_part_ptr));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_TEXT && n == sizeof(dsc->text) && dsc->text) {
        strncpy((char *)pdata, dsc->text, dsc->text_length);
        res = 0;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_obj_draw_part_dsc_set_data)
{
    int res = 0;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(lv_obj_draw_part_dsc_t *, dsc);
    lvgl_native_get_arg(int, type);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    pdata = map_ptr(exec_env, pdata);

    if (type == LV_OBJ_DRAW_PART_DSC_TYPE && n == sizeof(dsc->type)) {
        memcpy(&dsc->type, pdata, sizeof(dsc->type));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_PART && n == sizeof(dsc->part)) {
        memcpy(&dsc->part, pdata, sizeof(dsc->part));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_ID && n == sizeof(dsc->id)) {
        memcpy(&dsc->id, pdata, sizeof(dsc->id));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_VALUE && n == sizeof(dsc->value)) {
        memcpy(&dsc->value, pdata, sizeof(dsc->value));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_P1 && n == sizeof(*dsc->p1) && dsc->p1) {
        memcpy((void *)dsc->p1, pdata, sizeof(*dsc->p1));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_P2 && n == sizeof(*dsc->p2) && dsc->p2) {
        memcpy((void *)dsc->p2, pdata, sizeof(*dsc->p2));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_CLIP_AREA && n == sizeof(*dsc->clip_area) && dsc->clip_area) {
        memcpy((void *)dsc->clip_area, pdata, sizeof(*dsc->clip_area));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_DRAW_AREA && n == sizeof(*dsc->draw_area) && dsc->draw_area) {
        memcpy(dsc->draw_area, pdata, sizeof(*dsc->draw_area));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_RECT_DSC && n == sizeof(*dsc->rect_dsc) && dsc->rect_dsc) {
        memcpy(dsc->rect_dsc, pdata, sizeof(*dsc->rect_dsc));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_LINE_DSC && n == sizeof(*dsc->line_dsc) && dsc->line_dsc) {
        memcpy(dsc->line_dsc, pdata, sizeof(*dsc->line_dsc));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_SUB_PART_PTR && n == sizeof(dsc->sub_part_ptr)) {
        memcpy(&dsc->sub_part_ptr, pdata, sizeof(dsc->sub_part_ptr));
        res = 0;
    } else if (type == LV_OBJ_DRAW_PART_DSC_TEXT && n == sizeof(dsc->text) && dsc->text) {
        strncpy(dsc->text, (char *)pdata, dsc->text_length);
        res = 0;
    }

    lvgl_native_set_return(res);
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_chart_series_get_data)
{
    int res = -1;
    lvgl_native_return_type(int);
    lvgl_native_get_arg(lv_chart_series_t *, ser);
    lvgl_native_get_arg(int, type);
    lvgl_native_get_arg(void *, pdata);
    lvgl_native_get_arg(int, n);

    pdata = map_ptr(exec_env, pdata);

    if (type == LV_CHART_SERIES_COLOR && n == sizeof(ser->color)) {
        memcpy(pdata, &ser->color, sizeof(ser->color));
        res = 0;
    }

    lvgl_native_set_return(res); 
}

DEFINE_LVGL_NATIVE_WRAPPER(lv_font_get_font)
{
    const lv_font_t * res;
    lvgl_native_return_type(const lv_font_t *);
    lvgl_native_get_arg(int, type);

    switch(type) {
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

    font = map_font(exec_env, font);
    pdata = map_ptr(exec_env, pdata);

    if (type == LV_FONT_LINE_HEIGHT && n == sizeof(font->line_height)) {
        memcpy(pdata, &font->line_height, sizeof(font->line_height));
        res = 0;
    }

    lvgl_native_set_return(res); 
}

static const lvgl_func_desc_t lvgl_func_desc_table[] = {
    LVGL_NATIVE_WRAPPER(LV_FONT_GET_FONT, lv_font_get_font, 1),
    LVGL_NATIVE_WRAPPER(LV_DISP_GET_NEXT, lv_disp_get_next, 1),
    LVGL_NATIVE_WRAPPER(LV_DISP_GET_DEFAULT, lv_disp_get_default, 1),
    LVGL_NATIVE_WRAPPER(LV_DISP_GET_SCR_ACT, lv_disp_get_scr_act, 1),
    LVGL_NATIVE_WRAPPER(LV_DISP_GET_HOR_RES, lv_disp_get_hor_res, 1),
    LVGL_NATIVE_WRAPPER(LV_DISP_GET_VER_RES, lv_disp_get_ver_res, 1),
    LVGL_NATIVE_WRAPPER(LV_DISP_SET_MONITOR_CB, lv_disp_set_monitor_cb, 2),
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
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_IMG_RECOLOR, lv_obj_set_style_img_recolor, 3),
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
    LVGL_NATIVE_WRAPPER(LV_TABLE_SET_COL_CNT, lv_table_set_col_cnt, 2),
    LVGL_NATIVE_WRAPPER(LV_TABLE_SET_COL_WIDTH, lv_table_set_col_width, 3),
    LVGL_NATIVE_WRAPPER(LV_TABLE_ADD_CELL_CTRL, lv_table_add_cell_ctrl, 4),
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
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_SHADOW_OFS_X, lv_style_set_shadow_ofs_x, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_SHADOW_OFS_Y, lv_style_set_shadow_ofs_y, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_SHADOW_SPREAD, lv_style_set_shadow_spread, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_IMG_OPA, lv_style_set_img_opa, 2),
    LVGL_NATIVE_WRAPPER(LV_STYLE_SET_IMG_RECOLOR_OPA, lv_style_set_img_recolor_opa, 2),
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
    LVGL_NATIVE_WRAPPER(LV_IMG_CREATE, lv_img_create, 1),
    LVGL_NATIVE_WRAPPER(LV_IMG_SET_SRC, lv_img_set_src, 2),
    LVGL_NATIVE_WRAPPER(LV_IMG_SET_ANGLE, lv_img_set_angle, 2),
    LVGL_NATIVE_WRAPPER(LV_IMG_SET_ZOOM, lv_img_set_zoom, 2),
    LVGL_NATIVE_WRAPPER(LV_IMG_SET_ANTIALIAS, lv_img_set_antialias, 2),
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
    LVGL_NATIVE_WRAPPER(LV_TABVIEW_MAIN, lv_tabview_create, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_TEXT_FONT, lv_obj_set_style_text_font, 3),
    LVGL_NATIVE_WRAPPER(LV_TABVIEW_GET_TAB_BTNS, lv_tabview_get_tab_btns, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_PAD_LEFT, lv_obj_set_style_pad_left,3 ),
    LVGL_NATIVE_WRAPPER(LV_TABVIEW_ADD_TAB, lv_tabview_add_tab, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_HEIGHT, lv_obj_set_height, 2),
    LVGL_NATIVE_WRAPPER(LV_LABEL_SET_LONG_MODE, lv_label_set_long_mode, 2),
    LVGL_NATIVE_WRAPPER(LV_BTN_CREATE, lv_btn_create, 1),
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
    LVGL_NATIVE_WRAPPER(LV_CHART_SET_AXIS_TICK, lv_chart_set_axis_tick, 8),
    LVGL_NATIVE_WRAPPER(LV_CHART_SET_DIV_LINE_COUNT, lv_chart_set_div_line_count, 3),
    LVGL_NATIVE_WRAPPER(LV_CHART_SET_POINT_COUNT, lv_chart_set_point_count, 2),
    LVGL_NATIVE_WRAPPER(LV_CHART_SET_ZOOM_X, lv_chart_set_zoom_x, 2),
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
    LVGL_NATIVE_WRAPPER(LV_METER_ADD_SCALE, lv_meter_add_scale, 1),
    LVGL_NATIVE_WRAPPER(LV_METER_SET_SCALE_RANGE, lv_meter_set_scale_range, 6),
    LVGL_NATIVE_WRAPPER(LV_METER_SET_SCALE_TICKS, lv_meter_set_scale_ticks, 6),
    LVGL_NATIVE_WRAPPER(LV_METER_ADD_ARC, lv_meter_add_arc, 5),
    LVGL_NATIVE_WRAPPER(LV_METER_SET_INDICATOR_START_VALUE, lv_meter_set_indicator_start_value, 3),
    LVGL_NATIVE_WRAPPER(LV_METER_SET_INDICATOR_END_VALUE, lv_meter_set_indicator_end_value, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_PAD_RIGHT, lv_obj_set_style_pad_right, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_WIDTH, lv_obj_set_style_width, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_HEIGHT, lv_obj_set_style_height, 3),
    LVGL_NATIVE_WRAPPER(LV_PALETTE_DARKEN, lv_palette_darken, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_OUTLINE_COLOR, lv_obj_set_style_outline_color, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_OUTLINE_WIDTH, lv_obj_set_style_outline_width, 3),
    LVGL_NATIVE_WRAPPER(LV_METER_SET_SCALE_MAJOR_TICKS, lv_meter_set_scale_major_ticks, 7),
    LVGL_NATIVE_WRAPPER(LV_METER_ADD_SCALE_LINES, lv_meter_add_scale_lines, 6),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_PAD_BOTTOM, lv_obj_set_style_pad_bottom, 3),
    LVGL_NATIVE_WRAPPER(LV_DISP_GET_DPI, lv_disp_get_dpi, 1),
    LVGL_NATIVE_WRAPPER(LV_CHECKBOX_CREATE, lv_checkbox_create, 1),
    LVGL_NATIVE_WRAPPER(LV_CHECKBOX_SET_TEXT, lv_checkbox_set_text, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_FLEX_ALIGN, lv_obj_set_flex_align, 4),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_OPA, lv_obj_set_style_opa, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_CLEAR_FLAG, lv_obj_clear_flag, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_PAD_TOP, lv_obj_set_style_pad_top, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_SHADOW_WIDTH, lv_obj_set_style_shadow_width, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_BG_IMG_SRC, lv_obj_set_style_bg_img_src, 3),
    LVGL_NATIVE_WRAPPER(LV_EVENT_GET_CODE, lv_event_get_code, 1),
    LVGL_NATIVE_WRAPPER(LV_EVENT_GET_TARGET, lv_event_get_target, 1),
    LVGL_NATIVE_WRAPPER(LV_EVENT_GET_USER_DATA, lv_event_get_user_data, 1),
    LVGL_NATIVE_WRAPPER(LV_INDEV_GET_ACT, lv_indev_get_act, 1),
    LVGL_NATIVE_WRAPPER(LV_INDEV_GET_TYPE, lv_indev_get_type, 1),
    LVGL_NATIVE_WRAPPER(LV_KEYBOARD_SET_TEXTAREA, lv_keyboard_set_textarea, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SCROLL_TO_VIEW_RECURSIVE, lv_obj_scroll_to_view_recursive, 2),
    LVGL_NATIVE_WRAPPER(LV_INDEV_RESET, lv_indev_reset, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_CLEAR_STATE, lv_obj_clear_state, 2),
    LVGL_NATIVE_WRAPPER(LV_DISP_GET_LAYER_TOP, lv_disp_get_layer_top, 1),
    LVGL_NATIVE_WRAPPER(LV_CALENDAR_CREATE, lv_calendar_create, 1),
    LVGL_NATIVE_WRAPPER(LV_CALENDAR_SET_SHOWED_DATE, lv_calendar_set_showed_date, 3),
    LVGL_NATIVE_WRAPPER(LV_CALENDAR_HEADER_DROPDOWN_CREATE, lv_calendar_header_dropdown_create, 1),
    LVGL_NATIVE_WRAPPER(LV_EVENT_GET_PARAM, lv_event_get_param, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_HAS_STATE, lv_obj_has_state, 2),
    LVGL_NATIVE_WRAPPER(LV_BAR_GET_VALUE, lv_bar_get_value, 1),
    LVGL_NATIVE_WRAPPER(LV_TXT_GET_SIZE, lv_txt_get_size, 7),
    LVGL_NATIVE_WRAPPER(LV_DRAW_RECT_DSC_INIT, lv_draw_rect_dsc_init, 1),
    LVGL_NATIVE_WRAPPER(LV_DRAW_RECT, lv_draw_rect, 3),
    LVGL_NATIVE_WRAPPER(LV_DRAW_LABEL_DSC_INIT, lv_draw_label_dsc_init, 1),
    LVGL_NATIVE_WRAPPER(LV_DRAW_LABEL, lv_draw_label, 5),
    LVGL_NATIVE_WRAPPER(LV_EVENT_GET_CURRENT_TARGET, lv_event_get_current_target, 1),
    LVGL_NATIVE_WRAPPER(LV_CALENDAR_GET_PRESSED_DATE, lv_calendar_get_pressed_date, 2),
    LVGL_NATIVE_WRAPPER(LV_TEXTAREA_SET_TEXT, lv_textarea_set_text, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_DEL, lv_obj_del, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_INVALIDATE, lv_obj_invalidate, 1),
    LVGL_NATIVE_WRAPPER(LV_CHART_GET_TYPE, lv_chart_get_type, 1),
    LVGL_NATIVE_WRAPPER(LV_DRAW_MASK_LINE_POINTS_INIT, lv_draw_mask_line_points_init, 6),
    LVGL_NATIVE_WRAPPER(LV_DRAW_MASK_ADD, lv_draw_mask_add, 2),
    LVGL_NATIVE_WRAPPER(LV_DRAW_MASK_FADE_INIT, lv_draw_mask_fade_init, 6),
    LVGL_NATIVE_WRAPPER(_LV_AREA_INTERSECT, _lv_area_intersect, 3),
    LVGL_NATIVE_WRAPPER(LV_DRAW_MASK_REMOVE_ID, lv_draw_mask_remove_id, 1),
    LVGL_NATIVE_WRAPPER(LV_CHART_GET_PRESSED_POINT, lv_chart_get_pressed_point, 1),
    LVGL_NATIVE_WRAPPER(LV_CHART_GET_SERIES_NEXT, lv_chart_get_series_next, 2),
    LVGL_NATIVE_WRAPPER(LV_METER_CREATE, lv_meter_create, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_CHILD, lv_obj_get_child, 2),
    LVGL_NATIVE_WRAPPER(LV_METER_SET_INDICATOR_VALUE, lv_meter_set_indicator_value, 3),
    LVGL_NATIVE_WRAPPER(LV_CHART_SET_SERIES_COLOR, lv_chart_set_series_color, 3),
    LVGL_NATIVE_WRAPPER(LV_MAP, lv_map, 5),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_CHILD_CNT, lv_obj_get_child_cnt, 1),
    LVGL_NATIVE_WRAPPER(LV_METER_ADD_NEEDLE_LINE, lv_meter_add_needle_line, 5),
    LVGL_NATIVE_WRAPPER(LV_MEM_TEST, lv_mem_test, 1),
    LVGL_NATIVE_WRAPPER(LV_MEM_MONITOR, lv_mem_monitor, 1),
    LVGL_NATIVE_WRAPPER(LV_COLORWHEEL_CREATE, lv_colorwheel_create, 2),
    LVGL_NATIVE_WRAPPER(LV_TABVIEW_SET_ACT, lv_tabview_set_act, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_DEL_ANIM_READY_CB, lv_obj_del_anim_ready_cb, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_DEL_ASYNC, lv_obj_del_async, 1),
    LVGL_NATIVE_WRAPPER(LV_BAR_CREATE, lv_bar_create, 1),
    LVGL_NATIVE_WRAPPER(LV_BAR_SET_RANGE, lv_bar_set_range, 3),
    LVGL_NATIVE_WRAPPER(LV_BAR_SET_VALUE, lv_bar_set_value, 3),
    LVGL_NATIVE_WRAPPER(LV_BAR_SET_START_VALUE, lv_bar_set_start_value, 3),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_STYLE_ANIM_TIME, lv_obj_set_style_anim_time, 3),
    LVGL_NATIVE_WRAPPER(LV_WIN_CREATE, lv_win_create, 2),
    LVGL_NATIVE_WRAPPER(LV_WIN_ADD_TITLE, lv_win_add_title, 2),
    LVGL_NATIVE_WRAPPER(LV_WIN_ADD_BTN, lv_win_add_btn, 3),
    LVGL_NATIVE_WRAPPER(LV_WIN_GET_CONTENT, lv_win_get_content, 1),
    LVGL_NATIVE_WRAPPER(LV_KEYBOARD_SET_MODE, lv_keyboard_set_mode, 2),
    LVGL_NATIVE_WRAPPER(LV_DROPDOWN_SET_OPTIONS, lv_dropdown_set_options, 2),
    LVGL_NATIVE_WRAPPER(LV_DROPDOWN_OPEN, lv_dropdown_open, 1),
    LVGL_NATIVE_WRAPPER(LV_DROPDOWN_SET_SELECTED, lv_dropdown_set_selected, 2),
    LVGL_NATIVE_WRAPPER(LV_ROLLER_CREATE, lv_roller_create, 1),
    LVGL_NATIVE_WRAPPER(LV_ROLLER_SET_OPTIONS, lv_roller_set_options, 3),
    LVGL_NATIVE_WRAPPER(LV_ROLLER_SET_SELECTED, lv_roller_set_selected, 3),
    LVGL_NATIVE_WRAPPER(LV_MSGBOX_CREATE, lv_msgbox_create, 5),
    LVGL_NATIVE_WRAPPER(LV_TILEVIEW_CREATE, lv_tileview_create, 1),
    LVGL_NATIVE_WRAPPER(LV_TILEVIEW_ADD_TILE, lv_tileview_add_tile, 4),
    LVGL_NATIVE_WRAPPER(LV_OBJ_SET_TILE_ID, lv_obj_set_tile_id, 4),
    LVGL_NATIVE_WRAPPER(LV_LIST_CREATE, lv_list_create, 1),
    LVGL_NATIVE_WRAPPER(LV_LIST_ADD_BTN, lv_list_add_btn, 3),
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
    LVGL_NATIVE_WRAPPER(LV_TEXTAREA_DEL_CHAR_FORWARD, lv_textarea_del_char_forward, 1),
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
    LVGL_NATIVE_WRAPPER(LV_IMGBTN_CREATE, lv_imgbtn_create, 1),
    LVGL_NATIVE_WRAPPER(LV_IMGBTN_SET_SRC, lv_imgbtn_set_src, 5),
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
    LVGL_NATIVE_WRAPPER(LV_ANIM_DEL, lv_anim_del, 2),
    LVGL_NATIVE_WRAPPER(LV_EVENT_SET_EXT_DRAW_SIZE, lv_event_set_ext_draw_size, 2),
    LVGL_NATIVE_WRAPPER(LV_EVENT_SET_COVER_RES, lv_event_set_cover_res, 2),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_STYLE_PROP, lv_obj_get_style_prop, 3),
    LVGL_NATIVE_WRAPPER(LV_IMG_GET_ZOOM, lv_img_get_zoom, 1),
    LVGL_NATIVE_WRAPPER(LV_TRIGO_SIN, lv_trigo_sin, 1),
    LVGL_NATIVE_WRAPPER(LV_DRAW_POLYGON, lv_draw_polygon, 4),
    LVGL_NATIVE_WRAPPER(LV_INDEV_GET_GESTURE_DIR, lv_indev_get_gesture_dir, 1),
    LVGL_NATIVE_WRAPPER(LV_ANIM_PATH_EASE_IN, lv_anim_path_ease_in, 1),
    LVGL_NATIVE_WRAPPER(LV_TIMER_GET_USER_DATA, lv_timer_get_user_data, 1),
    LVGL_NATIVE_WRAPPER(LV_OBJ_DRAW_PART_DSC_GET_DATA, lv_obj_draw_part_dsc_get_data, 4),
    LVGL_NATIVE_WRAPPER(LV_OBJ_DRAW_PART_DSC_SET_DATA, lv_obj_draw_part_dsc_set_data, 4),
    LVGL_NATIVE_WRAPPER(LV_OBJ_GET_DATA, lv_obj_get_data, 4),
    LVGL_NATIVE_WRAPPER(LV_CHART_SERIES_GET_DATA, lv_chart_series_get_data, 4),
    LVGL_NATIVE_WRAPPER(LV_FONT_GET_DATA, lv_font_get_data, 4),
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
        ESP_LOGE(TAG, "func_id=%d is out of range", func_id);
        return;
    }

    if (!wasm_runtime_validate_native_addr(module_inst,
                                           argv,
                                           argc * sizeof(uint32_t))) {
        ESP_LOGE(TAG, "argv=%p argc=%d is out of range", argv, argc);
        return;
    }

    if (func_desc->argc == argc) {
        uint32_t size;
        uint32_t argv_copy_buf[LVGL_ARG_BUF_NUM];
        uint32_t *argv_copy = argv_copy_buf;

        if (argc > LVGL_ARG_BUF_NUM) {
            if (argc > LVGL_ARG_NUM_MAX) {
                ESP_LOGE(TAG, "argc=%d is out of range", argc);
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

        ESP_LOGD(TAG, "func_id=%d start", func_id);

        func_desc->func(exec_env, argv_copy, argv);

        if (argv_copy != argv_copy_buf)
            wasm_runtime_free(argv_copy);

        ESP_LOGD(TAG, "func_id=%d done", func_id);
    } else {
        ESP_LOGE(TAG, "func_id=%d is not found", func_id);
    }    
}

void lv_run_wasm(void *env, void *cb, int argc, uint32_t *argv)
{
    bool ret;
    wasm_exec_env_t origin_exec_env = env;
    wasm_module_inst_t module_inst = get_module_inst(origin_exec_env);
    wasm_exec_env_t exec_env = wasm_runtime_create_exec_env(module_inst, LVGL_WASM_CALLBACK_STACK_SIZE);

    ret = wasm_runtime_call_indirect(exec_env,
                                     (uint32_t)cb,
                                     argc,
                                     argv);
    if (!ret) {
        ESP_LOGE(TAG, "failed to run WASM callback cb=%p argc=%d argv=%p", cb, argc, argv);
    }
    
    wasm_runtime_destroy_exec_env(exec_env);
}

static NativeSymbol wm_lvgl_wrapper_native_symbol[] = {
    REG_NATIVE_FUNC(lvgl_init, "()i"),
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

