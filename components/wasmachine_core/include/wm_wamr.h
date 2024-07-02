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

#include "sdkconfig.h"
#include "wm_config.h"
#ifdef CONFIG_WASMACHINE_APP_MGR
#include "bi-inc/shared_utils.h"
#include "wa-inc/request.h"
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