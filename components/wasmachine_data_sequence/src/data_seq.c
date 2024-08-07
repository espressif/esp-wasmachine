/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include "data_seq.h"

#define DATA_SEQ_MIN        (1)
#define DATA_SEQ_MAX        (1024)

#define TYPE_LEN            sizeof(data_seq_type_t)
#define SIZE_LEN            sizeof(data_seq_size_t)

data_seq_t *data_seq_alloc(uint32_t n)
{
    uint32_t size;
    data_seq_t *ds;

    if (n < DATA_SEQ_MIN || n >= DATA_SEQ_MAX) {
        return NULL;
    }

    size = sizeof(data_seq_frame_t) * n;
    ds = malloc(sizeof(data_seq_t) + size);
    if (ds) {
        ds->version = DATA_SEQ_V_1;
        ds->num     = n;
        ds->index   = 0;
    }

    return ds;
}

void data_seq_free(data_seq_t *ds)
{
    if (ds) {
        free(ds);
    }
}

void data_seq_reset(data_seq_t *ds)
{
    if (ds) {
        ds->index = 0;
    }
}

int data_seq_push(data_seq_t *ds, data_seq_type_t type, data_seq_size_t size, const void *data)
{
    if (!ds || !size || !data) {
        return -EINVAL;
    }

    if (ds->version == DATA_SEQ_V_1) {
        if (ds->num <= ds->index) {
            return -ENOSPC;
        }

        ds->frame[ds->index].type = type;
        ds->frame[ds->index].size = size;
        ds->frame[ds->index].ptr  = (uintptr_t)data;

        ds->index = ds->index + 1;
    }

    return 0;
}

int data_seq_pop(data_seq_t *ds, data_seq_type_t type, data_seq_size_t size, void *data)
{
    int ret = -ENOENT;

    if (!ds || !size || !data) {
        return -EINVAL;
    }

    if (ds->version == DATA_SEQ_V_1) {
        for (uint32_t i  = 0; i < ds->index; i++) {
            if ((ds->frame[i].type == type) && (ds->frame[i].size == size)) {
                void *buffer = (void *)ds->frame[i].ptr;
                memcpy(data, buffer, size);
                ret = 0;
                break;
            }
        }
    }

    return ret;
}

int data_seq_update_frame_data(data_seq_t *ds, data_seq_type_t type, data_seq_size_t size, void *data)
{
    if (!ds || !size || !data) {
        return -EINVAL;
    }

    if (ds->version == DATA_SEQ_V_1) {
        for (uint32_t i = 0; i < ds->index; i++) {
            if (ds->frame[i].type == type &&
                    ds->frame[i].size == size) {
                void *s = (void *)ds->frame[i].ptr;

                memcpy(s, data, size);
                return 0;
            }
        }
    }

    return -EINVAL;
}
