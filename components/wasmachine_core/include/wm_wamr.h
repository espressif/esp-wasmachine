/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "sdkconfig.h"
#include "wm_config.h"
#ifdef CONFIG_WASMACHINE_APP_MGR
#include "bi-inc/shared_utils.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_WASMACHINE_APP_MGR
void wm_wamr_app_mgr_init(void);
void wm_wamr_app_mgr_lock(void);
void wm_wamr_app_mgr_unlock(void);
int wm_wamr_app_send_request(request_t *request, uint16_t msg_type);
#endif

void wm_wamr_init(void);

#ifdef __cplusplus
}
#endif
