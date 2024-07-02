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

#include <string.h>
#include <stdlib.h>
#include "unity.h"
#include "data_seq.h"

#define TEST_COUNT 1024

#define TLV_TS_A 0
#define TLV_TS_B 1
#define TLV_TS_C 2
#define TLV_TS_D 3
#define TLV_TS_E 4

struct test_ds {
    int32_t a;
    int8_t b;
    int16_t c;
    void *d;
    char e[32];
};

static void test_decode_ts0(struct test_ds *ts, data_seq_t *ds)
{
    struct test_ds ts0;

    TEST_ASSERT_EQUAL_INT(0, DATA_SEQ_POP(ds, TLV_TS_A, ts0.a));
    TEST_ASSERT_EQUAL_INT(0, DATA_SEQ_POP(ds, TLV_TS_B, ts0.b));
    TEST_ASSERT_EQUAL_INT(0, DATA_SEQ_POP(ds, TLV_TS_C, ts0.c));
    TEST_ASSERT_EQUAL_INT(0, DATA_SEQ_POP(ds, TLV_TS_D, ts0.d));
    TEST_ASSERT_EQUAL_INT(0, DATA_SEQ_POP(ds, TLV_TS_E, ts0.e));

    TEST_ASSERT_EQUAL_INT(ts->a, ts0.a);
    TEST_ASSERT_EQUAL_INT(ts->b, ts0.b);
    TEST_ASSERT_EQUAL_INT(ts->c, ts0.c);
    TEST_ASSERT_EQUAL_INT(ts->d, ts0.d);
    TEST_ASSERT_EQUAL_STRING(ts->e, ts0.e);
}

TEST_CASE("Data Sequence Encode", "[data_sequence]")
{
    struct test_ds ts0 = {0};
    data_seq_t *ds;

    ds = data_seq_alloc(1);
    TEST_ASSERT_NOT_NULL(ds);

    TEST_ASSERT_EQUAL_INT(0, DATA_SEQ_PUSH(ds, TLV_TS_B, ts0.b));
    TEST_ASSERT_LESS_THAN(0, DATA_SEQ_PUSH(ds, TLV_TS_E, ts0.e));

    data_seq_free(ds);
}

TEST_CASE("Data Sequence Decode", "[data_sequence]")
{
    struct test_ds ts0 = { 0 };
    struct test_ds ts1;

    data_seq_t *ds;

    ds = data_seq_alloc(2);
    TEST_ASSERT_NOT_NULL(ds);

    TEST_ASSERT_EQUAL_INT(0, DATA_SEQ_PUSH(ds, TLV_TS_D, ts0.d));
    TEST_ASSERT_EQUAL_INT(0, DATA_SEQ_PUSH(ds, TLV_TS_E, ts0.e));

    TEST_ASSERT_LESS_THAN(0, DATA_SEQ_POP(ds, TLV_TS_A, ts1.a));
    TEST_ASSERT_LESS_THAN(0, DATA_SEQ_POP(ds, TLV_TS_B, ts1.b));
    TEST_ASSERT_LESS_THAN(0, DATA_SEQ_POP(ds, TLV_TS_C, ts1.c));

    data_seq_free(ds);
}

TEST_CASE("Data Sequence Codec", "[data_sequence]")
{
    data_seq_t *ds;

    ds = data_seq_alloc(5);
    TEST_ASSERT_NOT_NULL(ds);

    for (int i = 0; i < TEST_COUNT; i++) {
        struct test_ds ts0;

        ts0.a = (int32_t)random();
        ts0.b = (int8_t)random();
        ts0.c = (int16_t)random();
        ts0.d = (void *)random();
        sprintf(ts0.e, "hello-%d", i);

        data_seq_reset(ds);

        TEST_ASSERT_EQUAL_INT(0, DATA_SEQ_PUSH(ds, TLV_TS_A, ts0.a));
        TEST_ASSERT_EQUAL_INT(0, DATA_SEQ_PUSH(ds, TLV_TS_B, ts0.b));
        TEST_ASSERT_EQUAL_INT(0, DATA_SEQ_PUSH(ds, TLV_TS_C, ts0.c));
        TEST_ASSERT_EQUAL_INT(0, DATA_SEQ_PUSH(ds, TLV_TS_D, ts0.d));
        TEST_ASSERT_EQUAL_INT(0, DATA_SEQ_PUSH(ds, TLV_TS_E, ts0.e));

        test_decode_ts0(&ts0, ds);
    }

    data_seq_free(ds);
}
