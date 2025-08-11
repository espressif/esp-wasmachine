/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#define LV_FONT_GET_FONT                0
#define LV_DISP_GET_NEXT                1
#define LV_DISP_GET_DEFAULT             2
#define LV_DISP_GET_SCR_ACT             3
#define LV_DISP_GET_HOR_RES             4
#define LV_DISP_GET_VER_RES             5
#define LV_DISP_SET_MONITOR_CB          6
#define LV_OBJ_REMOVE_STYLE             7
#define LV_OBJ_SET_STYLE_BG_OPA         8
#define LV_OBJ_SET_POS                  9
#define LV_OBJ_ALIGN_TO                 10
#define LV_OBJ_CREATE                   11
#define LV_OBJ_GET_WIDTH                12
#define LV_OBJ_GET_HEIGHT               13
#define LV_OBJ_SET_SIZE                 14
#define LV_OBJ_ALIGN                    15
#define LV_OBJ_UPDATE_LAYOUT            16
#define LV_OBJ_CLEAN                    17
#define LV_OBJ_SET_FLEX_FLOW            18
#define LV_OBJ_GET_CONTENT_WIDTH        19
#define LV_OBJ_SET_WIDTH                20
#define LV_OBJ_SET_STYLE_LINE_COLOR     21
#define LV_OBJ_SET_STYLE_ARC_COLOR      22
#define LV_OBJ_SET_STYLE_IMG_RECOLOR    23
#define LV_OBJ_SET_STYLE_TEXT_COLOR     24
#define LV_OBJ_SET_X                    25
#define LV_OBJ_SET_Y                    26
#define LV_OBJ_ADD_STYLE                27
#define LV_OBJ_SET_STYLE_BG_COLOR       28
#define LV_OBJ_SET_STYLE_BORDER_COLOR   29
#define LV_OBJ_SET_STYLE_SHADOW_COLOR   30
#define LV_LABEL_CREATE                 31
#define LV_LABEL_SET_TEXT               32
#define LV_TABLE_CREATE                 33
#define LV_TABLE_SET_COL_CNT            34
#define LV_TABLE_SET_COL_WIDTH          35
#define LV_TABLE_ADD_CELL_CTRL          36
#define LV_TABLE_SET_CELL_VALUE         37
#define LV_TIMER_CREATE                 38
#define LV_TIMER_SET_REPEAT_COUNT       39
#define LV_STYLE_INIT                   40
#define LV_STYLE_RESET                  41
#define LV_STYLE_SET_BG_OPA             42
#define LV_STYLE_SET_RADIUS             43
#define LV_STYLE_SET_BORDER_WIDTH       44
#define LV_STYLE_SET_BORDER_OPA         45
#define LV_STYLE_SET_BORDER_SIDE        46
#define LV_STYLE_SET_SHADOW_OPA         47
#define LV_STYLE_SET_SHADOW_WIDTH       48
#define LV_STYLE_SET_SHADOW_OFS_X       49
#define LV_STYLE_SET_SHADOW_OFS_Y       50
#define LV_STYLE_SET_SHADOW_SPREAD      51
#define LV_STYLE_SET_IMG_OPA            52
#define LV_STYLE_SET_IMG_RECOLOR_OPA    53
#define LV_STYLE_SET_TEXT_FONT          54
#define LV_STYLE_SET_TEXT_OPA           55
#define LV_STYLE_SET_LINE_WIDTH         56
#define LV_STYLE_SET_LINE_OPA           57
#define LV_STYLE_SET_ARC_WIDTH          58
#define LV_STYLE_SET_ARC_OPA            59
#define LV_STYLE_SET_BLEND_MODE         60
#define LV_STYLE_SET_TEXT_COLOR         61
#define LV_LINE_CREATE                  62
#define LV_LINE_SET_POINTS              63
#define LV_ARC_CREATE                   64
#define LV_ARC_SET_START_ANGLE          65
#define LV_ARC_SET_END_ANGLE            66
#define LV_IMG_CREATE                   67
#define LV_IMG_SET_SRC                  68
#define LV_IMG_SET_ANGLE                69
#define LV_IMG_SET_ZOOM                 70
#define LV_IMG_SET_ANTIALIAS            71
#define LV_ANIM_INIT                    72
#define LV_ANIM_START                   73
#define LV_THEME_GET_FONT_SMALL         74
#define LV_THEME_GET_FONT_NORMAL        75
#define LV_THEME_GET_FONT_LARGE         76
#define LV_THEME_DEFAULT_INIT           77
#define KV_THEME_GET_COLOR_PRIMARY      78
#define LV_FONT_GET_BITMAP_FMT_TXT      79
#define LV_FONT_GET_GLYPH_DSC_FMT_TXT   80
#define LV_PALETTE_MAIN                 81
#define LV_TABVIEW_MAIN                 82
#define LV_OBJ_SET_STYLE_TEXT_FONT      83
#define LV_TABVIEW_GET_TAB_BTNS         84
#define LV_OBJ_SET_STYLE_PAD_LEFT       85
#define LV_TABVIEW_ADD_TAB              86
#define LV_OBJ_SET_HEIGHT               87
#define LV_LABEL_SET_LONG_MODE          88
#define LV_BTN_CREATE                   89
#define LV_OBJ_ADD_STATE                90
#define LV_KEYBOARD_CREATE              91
#define LV_OBJ_ADD_FLAG                 92
#define LV_TEXTAREA_CREATE              93
#define LV_TEXTAREA_SET_ONE_LINE        94
#define LV_TEXTAREA_SET_PLACEHOLDER_TEXT  95
#define LV_OBJ_ADD_EVENT_CB             96
#define LV_TEXTAREA_SET_PASSWORD_MODE   97
#define LV_DROPDOWN_CREATE              98
#define LV_DROPDOWN_SET_OPTIONS_STATIC  99
#define LV_SLIDER_CREATE                100
#define LV_OBJ_REFRESH_EXT_DRAW_SIZE    101
#define LV_SWITCH_CREATE                102
#define LV_OBJ_SET_GRID_DSC_ARRAY       103
#define LV_OBJ_SET_GRID_CELL            104
#define LV_OBJ_SET_STYLE_TEXT_ALIGN     105
#define LV_OBJ_SET_FLEX_GROW            106
#define LV_OBJ_SET_STYLE_MAX_HEIGHT     107
#define LV_CHART_CREATE                 108
#define LV_GROUP_GET_DEFAULT            109
#define LV_GROUP_ADD_OBJ                110
#define LV_CHART_SET_AXIS_TICK          111
#define LV_CHART_SET_DIV_LINE_COUNT     112
#define LV_CHART_SET_POINT_COUNT        113
#define LV_CHART_SET_ZOOM_X             114
#define LV_OBJ_SET_STYLE_BORDER_SIDE    115
#define LV_OBJ_SET_STYLE_RADIUS         116
#define LV_CHART_ADD_SERIES             117
#define LV_RAND                         118
#define LV_CHART_SET_NEXT_VALUE         119
#define LV_CHART_SET_TYPE               120
#define LV_OBJ_SET_STYLE_PAD_ROW        121
#define LV_OBJ_SET_STYLE_PAD_COLUMN     122
#define LV_PALETTE_LIGHTEN              123
#define LV_OBJ_GET_PARENT               124
#define LV_METER_ADD_SCALE              125
#define LV_METER_SET_SCALE_RANGE        126
#define LV_METER_SET_SCALE_TICKS        127
#define LV_METER_ADD_ARC                128
#define LV_METER_SET_INDICATOR_START_VALUE  129
#define LV_METER_SET_INDICATOR_END_VALUE    130
#define LV_OBJ_SET_STYLE_PAD_RIGHT      131
#define LV_OBJ_SET_STYLE_WIDTH          132
#define LV_OBJ_SET_STYLE_HEIGHT         133
#define LV_PALETTE_DARKEN               134
#define LV_OBJ_SET_STYLE_OUTLINE_COLOR  135
#define LV_OBJ_SET_STYLE_OUTLINE_WIDTH  136
#define LV_METER_SET_SCALE_MAJOR_TICKS  137
#define LV_METER_ADD_SCALE_LINES        138
#define LV_OBJ_SET_STYLE_PAD_BOTTOM     139
#define LV_DISP_GET_DPI                 140
#define LV_CHECKBOX_CREATE              141
#define LV_CHECKBOX_SET_TEXT            142
#define LV_OBJ_SET_FLEX_ALIGN           143
#define LV_OBJ_SET_STYLE_OPA            144
#define LV_OBJ_CLEAR_FLAG               145
#define LV_OBJ_SET_STYLE_PAD_TOP        146
#define LV_OBJ_SET_STYLE_SHADOW_WIDTH   147
#define LV_OBJ_SET_STYLE_BG_IMG_SRC     148
#define LV_EVENT_GET_CODE               149
#define LV_EVENT_GET_TARGET             150
#define LV_EVENT_GET_USER_DATA          151
#define LV_INDEV_GET_ACT                152
#define LV_INDEV_GET_TYPE               153
#define LV_KEYBOARD_SET_TEXTAREA        154
#define LV_OBJ_SCROLL_TO_VIEW_RECURSIVE 155
#define LV_INDEV_RESET                  156
#define LV_OBJ_CLEAR_STATE              157
#define LV_DISP_GET_LAYER_TOP           158
#define LV_CALENDAR_CREATE              159
#define LV_CALENDAR_SET_SHOWED_DATE     160
#define LV_CALENDAR_HEADER_DROPDOWN_CREATE  161
#define LV_EVENT_GET_PARAM              162
#define LV_OBJ_HAS_STATE                163
#define LV_BAR_GET_VALUE                164
#define LV_TXT_GET_SIZE                 165
#define LV_DRAW_RECT_DSC_INIT           166
#define LV_DRAW_RECT                    167
#define LV_DRAW_LABEL_DSC_INIT          168
#define LV_DRAW_LABEL                   169
#define LV_EVENT_GET_CURRENT_TARGET     170
#define LV_CALENDAR_GET_PRESSED_DATE    171
#define LV_TEXTAREA_SET_TEXT            172
#define LV_OBJ_DEL                      173
#define LV_OBJ_INVALIDATE               174
#define LV_CHART_GET_TYPE               175
#define LV_DRAW_MASK_LINE_POINTS_INIT   176
#define LV_DRAW_MASK_ADD                177
#define LV_DRAW_MASK_FADE_INIT          178
#define _LV_AREA_INTERSECT              179
#define LV_DRAW_MASK_REMOVE_ID          180
#define LV_CHART_GET_PRESSED_POINT      181
#define LV_CHART_GET_SERIES_NEXT        182
#define LV_METER_CREATE                 183
#define LV_OBJ_GET_CHILD                184
#define LV_METER_SET_INDICATOR_VALUE    185
#define LV_CHART_SET_SERIES_COLOR       186
#define LV_MAP                          187
#define LV_OBJ_GET_CHILD_CNT            188
#define LV_METER_ADD_NEEDLE_LINE        189
#define LV_MEM_TEST                     190
#define LV_MEM_MONITOR                  191
#define LV_COLORWHEEL_CREATE            192
#define LV_TABVIEW_SET_ACT              193
#define LV_OBJ_DEL_ANIM_READY_CB        194
#define LV_OBJ_DEL_ASYNC                195
#define LV_BAR_CREATE                   196
#define LV_BAR_SET_RANGE                197
#define LV_BAR_SET_VALUE                198
#define LV_BAR_SET_START_VALUE          199
#define LV_OBJ_SET_STYLE_ANIM_TIME      200
#define LV_WIN_CREATE                   201
#define LV_WIN_ADD_TITLE                202
#define LV_WIN_ADD_BTN                  203
#define LV_WIN_GET_CONTENT              204
#define LV_KEYBOARD_SET_MODE            205
#define LV_DROPDOWN_SET_OPTIONS         206
#define LV_DROPDOWN_OPEN                207
#define LV_DROPDOWN_SET_SELECTED        208
#define LV_ROLLER_CREATE                209
#define LV_ROLLER_SET_OPTIONS           210
#define LV_ROLLER_SET_SELECTED          211
#define LV_MSGBOX_CREATE                212
#define LV_TILEVIEW_CREATE              213
#define LV_TILEVIEW_ADD_TILE            214
#define LV_OBJ_SET_TILE_ID              215
#define LV_LIST_CREATE                  216
#define LV_LIST_ADD_BTN                 217
#define LV_OBJ_SCROLL_TO_VIEW           218
#define LV_TEXTAREA_SET_CURSOR_POS      219
#define LV_TEXTAREA_ADD_CHAR            220
#define LV_TEXTAREA_ADD_TEXT            221
#define LV_SPINBOX_CREATE               222
#define LV_SPINBOX_SET_DIGIT_FORMAT     223
#define LV_SPINBOX_SET_VALUE            224
#define LV_SPINBOX_SET_STEP             225
#define LV_SPINBOX_INCREMENT            226
#define LV_OBJ_SCROLL_BY                227
#define LV_TEXTAREA_DEL_CHAR_FORWARD    228
#define LV_MSGBOX_CLOSE                 229
#define LV_STYLE_SET_WIDTH              230
#define LV_STYLE_SET_BG_COLOR           231
#define LV_STYLE_SET_PAD_RIGHT          232
#define LV_STYLE_SET_GRID_COLUMN_DSC_ARRAY  233
#define LV_STYLE_SET_GRID_ROW_DSC_ARRAY 234
#define LV_STYLE_SET_GRID_ROW_ALIGN     235
#define LV_STYLE_SET_LAYOUT             236
#define LV_OBJ_GET_INDEX                237
#define LV_OBJ_SET_SCROLL_SNAP_Y        238
#define LV_OBJ_SET_STYLE_BORDER_WIDTH   239
#define LV_OBJ_SET_SCROLL_DIR           240
#define LV_IMGBTN_CREATE                241
#define LV_IMGBTN_SET_SRC               242
#define LV_OBJ_SET_STYLE_BG_GRAD_DIR    243
#define LV_OBJ_SET_STYLE_BG_GRAD_COLOR  244
#define LV_OBJ_SET_STYLE_GRID_ROW_ALIGN 245
#define LV_TIMER_PAUSE                  246
#define LV_ANIM_PATH_BOUNCE             247
#define LV_OBJ_FADE_IN                  248
#define LV_ANIM_PATH_EASE_OUT           249
#define LV_OBJ_MOVE_TO_INDEX            250
#define LV_OBJ_SET_STYLE_TEXT_LINE_SPACE  251
#define LV_OBJ_FADE_OUT                 252
#define LV_TIMER_RESUME                 253
#define LV_ANIM_PATH_LINEAR             254
#define LV_ANIM_PATH_OVERSHOOT          255
#define LV_ANIM_DEL                     256
#define LV_EVENT_SET_EXT_DRAW_SIZE      257
#define LV_EVENT_SET_COVER_RES          258
#define LV_OBJ_GET_STYLE_PROP           259
#define LV_IMG_GET_ZOOM                 260
#define LV_TRIGO_SIN                    261
#define LV_DRAW_POLYGON                 262
#define LV_INDEV_GET_GESTURE_DIR        263
#define LV_ANIM_PATH_EASE_IN            264
#define LV_OBJ_GET_DATA                 265
#define LV_TIMER_GET_USER_DATA          266
#define LV_OBJ_DRAW_PART_DSC_GET_DATA   267
#define LV_OBJ_DRAW_PART_DSC_SET_DATA   268
#define LV_CHART_SERIES_GET_DATA        269
#define LV_FONT_GET_DATA                270
#define LV_LABEL_SET_TEXT_STATIC        271
#define LV_STYLE_SET_BORDER_COLOR       272
#define LV_STYLE_SET_SHADOW_COLOR       273
#define LV_STYLE_SET_OUTLINE_COLOR      274
#define LV_STYLE_SET_OUTLINE_WIDTH      275
#define LV_INDEV_GET_NEXT               276
#define LV_GROUP_CREATE                 277
#define LV_INDEV_SET_GROUP              278
#define LV_OBJ_SET_STYLE_SHADOW_OPA     279
#define LV_INDEV_ENABLE                 280
#define LV_OBJ_HAS_FLAG                 281
#define LV_ARC_SET_BG_ANGLES            282
#define LV_ARC_SET_VALUE                283
#define LV_OBJ_SET_STYLE_ARC_WIDTH      284
#define LV_ARC_SET_ROTATION             285
#define LV_OBJ_SET_STYLE_IMG_OPA        286
#define LV_TIMER_DEL                    287
#define LV_OBJ_GET_USER_DATA            288
#define LV_OBJ_SET_USER_DATA            289
#define LV_OBJ_SET_SCROLLBAR_MODE       290
#define LV_GROUP_REMOVE_ALL_OBJS        291
#define LV_LABEL_SET_RECOLOR            292
#define LV_TABVIEW_GET_TAB_ACT          293
#define LV_OBJ_SET_STYLE_SHADOW_OFS_X   294
#define LV_OBJ_SET_STYLE_SHADOW_OFS_Y   295
#define LV_LED_CREATE                   296
#define LV_LED_OFF                      297
#define LV_LED_ON                       298
#define LV_OBJ_GET_CLICK_AREA           299
#define LV_INDEV_SET_BUTTON_POINTS      300
#define LV_QRCODE_CREATE                301
#define LV_QRCODE_UPDATE                302
#define LV_GROUP_FOCUS_OBJ              303
#define LV_GROUP_FOCUS_FREEZE           304
#define LV_DISP_GET_REFR_TIMER          305
#define LV_TIMER_SET_PERIOD             306
#define LV_ANIM_GET_TIMER               307
#define LV_DISP_GET_DATA                308
#define LV_ANIM_TIMER_GET_DATA          309
#define LV_TABLE_SET_ROW_CNT            310
#define LV_OBJ_GET_STYLE_OPA_RECURSIVE  311
#define LV_TIMER_CTX_GET_DATA           312
#define LV_TIMER_CTX_SET_DATA           313
#define LV_TIMER_READY                  314
#define LV_SUBJECT_ADD_OBSERVER_OBJ     315
#define LV_SCREEN_ACTIVE                316
#define LV_TABVIEW_SET_TAB_BAR_SIZE     317
#define LV_ANIM_SET_VAR                 318
#define LV_ANIM_SET_DURATION            319
#define LV_ANIM_SET_DELAY               320
#define LV_ANIM_SET_COMPLETED_CB        321
#define LV_ANIM_SET_EXEC_CB             322
#define LV_ANIM_SET_PATH_CB             323
#define LV_ANIM_SET_VALUES              324
#define LV_ANIM_SET_REVERSE_DURATION    325
#define LV_ANIM_SET_REPEAT_COUNT        326
#define LV_SCALE_CREATE                 327
#define LV_SCALE_SET_MODE               328
#define LV_SCALE_SET_ANGLE_RANGE        329
#define LV_SCALE_SET_TEXT_SRC           330
#define LV_SCALE_SET_TOTAL_TICK_COUNT   331
#define LV_SCALE_SET_MAJOR_TICK_EVERY   332
#define LV_SCALE_SET_RANGE              333
#define LV_SCALE_SET_ROTATION           334
#define LV_SCALE_ADD_SECTION            335
#define LV_SCALE_SET_SECTION_RANGE      336
#define LV_SCALE_SET_SECTION_STYLE_MAIN 337
#define LV_SCALE_SET_SECTION_STYLE_INDICATOR 338
#define LV_SCALE_SET_SECTION_STYLE_ITEMS     339
#define LV_SCALE_SET_IMAGE_NEEDLE_VALUE 340
#define LV_SCALE_SET_POST_DRAW          341
#define LV_MSGBOX_ADD_TITLE             342
#define LV_MSGBOX_ADD_HEADER_BUTTON     343
#define LV_MSGBOX_ADD_TEXT              344
#define LV_MSGBOX_ADD_FOOTER_BUTTON     345
#define LV_TEXTAREA_DELETE_CHAR_FORWARD 346
#define LV_LAYER_TOP                    347
#define LV_DRAW_TASK_GET_TYPE           348
#define LV_DRAW_DSC_BASE_GET_DATA       349
#define LV_DRAW_DSC_BASE_SET_DATA       350
#define LV_DRAW_LINE_DSC_GET_DATA       351
#define LV_DRAW_LINE_DSC_SET_DATA       352
#define LV_DRAW_FILL_DSC_GET_DATA       353
#define LV_DRAW_TASK_GET_DRAW_DSC       354
#define LV_DRAW_TASK_GET_AREA           355
#define LV_DRAW_TASK_GET_LINE_DSC       356
#define LV_DRAW_TASK_GET_FILL_DSC       357
#define LV_DRAW_TASK_GET_LABEL_DSC      358
#define LV_DRAW_TASK_GET_BORDER_DSC     359
#define LV_DRAW_TRIANGLE_DSC_INIT       360
#define LV_DRAW_TRIANGLE                361
#define LV_AREA_PCT                     362
#define LV_CHART_GET_FIRST_POINT_CENTER_OFFSET 363
#define LV_CHART_GET_SERIES_COLOR       364
#define LV_CHART_GET_SERIES_Y_ARRAY     365
#define LV_OBJ_CENTER                   366
#define LV_OBJ_DELETE_ANIM_COMPLETED_CB 367
#define LV_OBJ_GET_SIBLING              368
#define LV_OBJ_SET_STYLE_ARC_OPA        369
#define LV_OBJ_SET_STYLE_MARGIN_LEFT    370
#define LV_OBJ_SET_STYLE_MARGIN_RIGHT   371
#define LV_OBJ_SET_STYLE_MARGIN_TOP     372
#define LV_OBJ_SET_STYLE_MARGIN_BOTTOM  373
#define LV_OBJ_SET_STYLE_LENGTH         374
#define LV_OBJ_SET_STYLE_ARC_ROUNDED    375
#define LV_OBJ_GET_COORDS               376
#define LV_OBJ_REMOVE_STYLE_ALL         377
#define LV_OBJ_SET_LAYOUT               378
#define LV_OBJ_GET_CONTENT_HEIGHT       379
#define LV_OBJ_GET_SCROLL_BOTTOM        380
#define LV_OBJ_SET_STYLE_OPA_LAYERED    381
#define LV_OBJ_SCROLL_TO_Y              382
#define LV_OBJ_SET_STYLE_TRANSLATE_Y    383
#define LV_STYLE_SET_ARC_COLOR          384
#define LV_STYLE_SET_LINE_COLOR         385
#define LV_COLOR_WHITE                  386
#define LV_COLOR_BLACK                  387
#define LV_COLOR_HEX3                   388
#define LV_IMAGE_SET_PIVOT              389
#define LV_IMAGE_SET_INNER_ALIGN        390
#define LV_INDEV_WAIT_RELEASE           391
#define LV_SLIDER_SET_VALUE             392
#define LV_SLIDER_SET_RANGE             393
#define LV_SLIDER_GET_VALUE             394
#define LV_AREA_GET_WIDTH               395
#define LV_AREA_GET_HEIGHT              396
#define LV_ARC_SET_ANGLES               397
#define LV_EVENT_GET_DRAW_TASK          398
#define LV_EVENT_GET_LAYER              399
#define LV_TABVIEW_GET_CONTENT          400
#define LV_ANIM_SPEED                   401
#define LV_OBJ_GET_OBS_DATA             402
#define LV_DRAW_TASK_GET_DATA           403
#define LV_DRAW_FILL_DSC_SET_DATA       404
#define LV_DRAW_LABEL_DSC_SET_DATA      405
#define LV_DRAW_BORDER_DSC_SET_DATA     406
#define LV_FONT_GET_LINE_HEIGHT         407
#define LV_TRIGO_COS                    408
#define LV_GET_SYS_PERF_DATA            409
#define LV_BUTTONMATRIX_CREATE          410
#define LV_BUTTONMATRIX_SET_MAP         411
#define LV_BUTTONMATRIX_SET_BUTTON_WIDTH    412
#define LV_BUTTONMATRIX_SET_BUTTON_CTRL     413
#define LV_BUTTONMATRIX_GET_SELECTED_BUTTON 414
#define LV_BUTTONMATRIX_GET_BUTTON_TEXT 415
#define LV_LABEL_CUT_TEXT               416
#define LV_LABEL_INS_TEXT               417
#define LV_LABEL_GET_TEXT               418
#define LV_TEXTAREA_GET_TEXT            419
#define LV_SUBJECT_GET_POINTER          420
#define LV_OBSERVER_GET_TARGET          421
#define LV_OBJ_GET_X                    422
#define LV_OBJ_GET_Y                    423
#define LV_OBJ_SET_STYLE_GRID_COLUMN_DSC_ARRAY  424
#define LV_OBJ_SET_STYLE_GRID_ROW_DSC_ARRAY     425
#define LV_OBJ_SET_GRID_ALIGN           426
#define LV_SCREEN_LOAD_ANIM             427
#define LV_SCREEN_LOAD                  428
#define LV_THEME_SIMPLE_INIT            429
#define LV_DISPLAY_SET_THEME            430
#define LV_OBJ_SET_ALIGN                431
#define LV_OBJ_SET_STYLE_TEXT_OPA       432
#define LV_OBJ_SEND_EVENT               433
#define LV_OBJ_SET_STYLE_BG_IMAGE_TILED 434
#define LV_OBJ_SET_STYLE_BORDER_OPA     435
#define LV_OBJ_SET_STYLE_SHADOW_SPREAD  436
#define LV_OBJ_SET_STYLE_IMAGE_RECOLOR_OPA 437
#define LV_OBJ_GET_Y_ALIGNED            438
#define LV_MALLOC                       439
#define LV_FREE                         440
#define LV_ANIM_SET_USER_DATA           441
#define LV_ANIM_SET_CUSTOM_EXEC_CB      442
#define LV_ANIM_SET_DELETED_CB          443
#define LV_ANIM_SET_REVERSE_DELAY       444
#define LV_ANIM_SET_REPEAT_DELAY        445
#define LV_ANIM_SET_EARLY_APPLY         446
#define LV_ANIM_SET_GET_VALUE_CB        447
#define LV_ARC_GET_VALUE                448
#define LV_OBJ_SET_STYLE_TRANSFORM_SCALE_X 449
#define LV_OBJ_SET_STYLE_TRANSFORM_SCALE_Y 450
#define LV_ASYNC_CALL                   451
#define LV_OBJ_GET_SUBJECT              452
#define LV_OBJ_REMOVE_FROM_SUBJECT      453

#define LV_SYS_PERF_INFO_CALC             0

#define LV_DRAW_BORDER_DSC_COLOR          0
#define LV_DRAW_BORDER_DSC_WIDTH          1
#define LV_DRAW_BORDER_DSC_SIDE           2

#define LV_DRAW_LABEL_DSC_COLOR           0

#define LV_DRAW_FILL_DSC_COLOR            0

#define LV_DRAW_TASK_DSC_BASE             0 /*!< type of set/get lv_obj_t->coords */

#define LV_SYSMON_BACKEND_DATA            0 /*!< type of set/get lv_obj_t->coords */

#define LV_OBJ_COORDS                     0 /*!< type of set/get lv_obj_t->coords */

#define LV_OBJ_DRAW_PART_DSC_TYPE         0 /*!< type of set/get lv_obj_draw_part_dsc_t->type */
#define LV_OBJ_DRAW_PART_DSC_PART         1 /*!< type of set/get lv_obj_draw_part_dsc_t->part */
#define LV_OBJ_DRAW_PART_DSC_ID           2 /*!< type of set/get lv_obj_draw_part_dsc_t->id */
#define LV_OBJ_DRAW_PART_DSC_TEXT         3 /*!< type of set/get lv_obj_draw_part_dsc_t->text */
#define LV_OBJ_DRAW_PART_DSC_VALUE        4 /*!< type of set/get lv_obj_draw_part_dsc_t->value */
#define LV_OBJ_DRAW_PART_DSC_P1           5 /*!< type of set/get lv_obj_draw_part_dsc_t->p1 */
#define LV_OBJ_DRAW_PART_DSC_P2           6 /*!< type of set/get lv_obj_draw_part_dsc_t->p2 */
#define LV_OBJ_DRAW_PART_DSC_CLIP_AREA    7 /*!< type of set/get lv_draw_ctx_t->clip_area */
#define LV_OBJ_DRAW_PART_DSC_DRAW_AREA    8 /*!< type of set/get lv_obj_draw_part_dsc_t->draw_area */
#define LV_OBJ_DRAW_PART_DSC_RECT_DSC     9 /*!< type of set/get lv_obj_draw_part_dsc_t->rect_dsc */
#define LV_OBJ_DRAW_PART_DSC_LINE_DSC     10 /*!< type of set/get lv_obj_draw_part_dsc_t->line_dsc */
#define LV_OBJ_DRAW_PART_DSC_SUB_PART_PTR 11 /*!< type of set/get lv_obj_draw_part_dsc_t->sub_part_ptr */
#define LV_OBJ_DRAW_PART_DSC_DRAW_CTX     12 /*!< type of set/get lv_obj_draw_part_dsc_t->draw_ctx */

#define LV_DRAW_RECT_DSC_BASE         0 /*!< type of set/get lv_obj_draw_part_dsc_t->type */
#define LV_DRAW_RECT_DSC_RADIUS       1 /*!< type of set/get lv_obj_draw_part_dsc_t->part */
#define LV_DRAW_RECT_DSC_BG_OPA           2 /*!< type of set/get lv_obj_draw_part_dsc_t->id */
#define LV_DRAW_RECT_DSC_BG_COLOR         3 /*!< type of set/get lv_obj_draw_part_dsc_t->text */
#define LV_DRAW_RECT_DSC_BG_GRAD        4 /*!< type of set/get lv_obj_draw_part_dsc_t->value */
#define LV_DRAW_RECT_DSC_BG_IMAGE_RECOLOR           5 /*!< type of set/get lv_obj_draw_part_dsc_t->p1 */
#define LV_DRAW_RECT_DSC_BG_IMAGE_OPA           6 /*!< type of set/get lv_obj_draw_part_dsc_t->p2 */
#define LV_DRAW_RECT_DSC_BG_IMAGE_RECOLOR_OPA    7 /*!< type of set/get lv_draw_ctx_t->clip_area */
#define LV_DRAW_RECT_DSC_BG_IMAGE_TILED    8 /*!< type of set/get lv_obj_draw_part_dsc_t->draw_area */

#define LV_DRAW_DSC_BASE_OBJ         0 /*!< type of set/get lv_obj_draw_part_dsc_t->type */
#define LV_DRAW_DSC_BASE_PART       1 /*!< type of set/get lv_obj_draw_part_dsc_t->part */
#define LV_DRAW_DSC_BASE_ID1           2 /*!< type of set/get lv_obj_draw_part_dsc_t->id */
#define LV_DRAW_DSC_BASE_ID2         3 /*!< type of set/get lv_obj_draw_part_dsc_t->text */
#define LV_DRAW_DSC_BASE_LAYER        4 /*!< type of set/get lv_obj_draw_part_dsc_t->value */
#define LV_DRAW_DSC_BASE_DSC_SIZE           5 /*!< type of set/get lv_obj_draw_part_dsc_t->p1 */

#define LV_DRAW_LINE_DSC_BASE         0 /*!< type of set/get lv_obj_draw_part_dsc_t->type */
#define LV_DRAW_LINE_DSC_P1       1 /*!< type of set/get lv_obj_draw_part_dsc_t->part */
#define LV_DRAW_LINE_DSC_P2           2 /*!< type of set/get lv_obj_draw_part_dsc_t->id */
#define LV_DRAW_LINE_DSC_COLOR         3 /*!< type of set/get lv_obj_draw_part_dsc_t->text */
#define LV_DRAW_LINE_DSC_WIDTH        4 /*!< type of set/get lv_obj_draw_part_dsc_t->value */
#define LV_DRAW_LINE_DSC_DASH_WIDTH           5 /*!< type of set/get lv_obj_draw_part_dsc_t->p1 */

#define LV_CHART_SERIES_COLOR             0 /*!< type of set/get lv_chart_series_t->color */

#define LV_DRAW_FILL_DSC_RADIUS             0 /*!< type of set/get lv_chart_series_t->color */

#define LV_FONT_LINE_HEIGHT               0 /*!< type of set/get lv_font_t->line_height */

#define LV_FONT_MONTSERRAT_8_FONT 0
#define LV_FONT_MONTSERRAT_10_FONT 1
#define LV_FONT_MONTSERRAT_12_FONT 2
#define LV_FONT_MONTSERRAT_14_FONT 3
#define LV_FONT_MONTSERRAT_16_FONT 4
#define LV_FONT_MONTSERRAT_18_FONT 5
#define LV_FONT_MONTSERRAT_20_FONT 6
#define LV_FONT_MONTSERRAT_22_FONT 7
#define LV_FONT_MONTSERRAT_24_FONT 8
#define LV_FONT_MONTSERRAT_26_FONT 9
#define LV_FONT_MONTSERRAT_28_FONT 10
#define LV_FONT_MONTSERRAT_30_FONT 11
#define LV_FONT_MONTSERRAT_32_FONT 12
#define LV_FONT_MONTSERRAT_34_FONT 13
#define LV_FONT_MONTSERRAT_36_FONT 14
#define LV_FONT_MONTSERRAT_38_FONT 15
#define LV_FONT_MONTSERRAT_40_FONT 16
#define LV_FONT_MONTSERRAT_42_FONT 17
#define LV_FONT_MONTSERRAT_44_FONT 18
#define LV_FONT_MONTSERRAT_46_FONT 19
#define LV_FONT_MONTSERRAT_48_FONT 20
#define LV_FONT_MONTSERRAT_28_COMPRESSED_FONT 21
#define LV_FONT_MONTSERRAT_12_SUBPX_FONT 22
#define LV_FONT_UNSCII_8_FONT 23
#define LV_FONT_UNSCII_16_FONT 24
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW_FONT 25
#define LV_FONT_SIMSUN_16_CJK_FONT 26
#define LV_FONT_BENCHMARK_MONTSERRAT_12_COMPR_AZ_FONT 27
#define LV_FONT_BENCHMARK_MONTSERRAT_14_COMPR_AZ_FONT 28
#define LV_FONT_BENCHMARK_MONTSERRAT_16_COMPR_AZ_FONT 29
#define LV_FONT_BENCHMARK_MONTSERRAT_18_COMPR_AZ_FONT 30
#define LV_FONT_BENCHMARK_MONTSERRAT_20_COMPR_AZ_FONT 31
#define LV_FONT_BENCHMARK_MONTSERRAT_24_COMPR_AZ_FONT 32
#define LV_FONT_BENCHMARK_MONTSERRAT_26_COMPR_AZ_FONT 33

#define LV_TIMER_CTX_COUNT_VAL 0

/* ESP-Wasmachine LVGL version: 0.1.0 */
#define WM_LV_VERSION_MAJOR 0
#define WM_LV_VERSION_MINOR 1
#define WM_LV_VERSION_PATCH 0
