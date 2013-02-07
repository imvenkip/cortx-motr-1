/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 10/19/2010
 * Revision       : Anup Barve <Anup_Barve@xyratex.com>
 * Revision date  : 06/21/2012
 */

#include <stdlib.h>
#include "lib/types.h"
#include "lib/adt.h"
#include "lib/assert.h"
#include "lib/memory.h"

#include "lib/ub.h"
#include "lib/ut.h"
#include "sns/parity_math.h"

enum {
	DATA_UNIT_COUNT_MAX      = 30,
	PRTY_UNIT_COUNT_MAX      = 12,
	DATA_TO_PRTY_RATIO_MAX   = DATA_UNIT_COUNT_MAX / PRTY_UNIT_COUNT_MAX,
	UNIT_BUFF_SIZE_MAX       = 1048576,
	DATA_UNIT_COUNT          = 15,
	PARITY_UNIT_COUNT        = 1,
	RS_MAX_PARITY_UNIT_COUNT = DATA_UNIT_COUNT - 1,
};

static uint8_t expected[DATA_UNIT_COUNT_MAX][UNIT_BUFF_SIZE_MAX];
static uint8_t data    [DATA_UNIT_COUNT_MAX][UNIT_BUFF_SIZE_MAX];
static uint8_t parity  [DATA_UNIT_COUNT_MAX][UNIT_BUFF_SIZE_MAX];
static uint8_t fail    [DATA_UNIT_COUNT_MAX+PRTY_UNIT_COUNT_MAX];
static int32_t duc = DATA_UNIT_COUNT_MAX;
static int32_t puc = PRTY_UNIT_COUNT_MAX;
static int32_t fuc = PRTY_UNIT_COUNT_MAX;
static uint32_t UNIT_BUFF_SIZE = 256;
static int32_t fail_index_xor;

enum recovery_type {
	FAIL_VECTOR,
	FAIL_INDEX,
};

static void unit_spoil(const uint32_t buff_size,
		       const uint32_t fail_count,
		       const uint32_t data_count)
{
	uint32_t i;

	for (i = 0; i < fail_count; ++i)
		if (fail[i]) {
			if (i < data_count)
				memset(data[i], 0xFF, buff_size);
			else
				memset(parity[i - data_count], 0xFF, buff_size);
		}
}

static bool expected_cmp(const uint32_t data_count, const uint32_t buff_size)
{
	int i;
	int j;

	for (i = 0; i < data_count; ++i)
		for (j = 0; j < buff_size; ++j)
			if (expected[i][j] != data[i][j])
				return false;

	return true;
}

static bool config_generate(uint32_t *data_count,
			    uint32_t *parity_count,
			    uint32_t *buff_size,
			    const enum m0_parity_cal_algo algo)
{
	int32_t i;
	int32_t j;
	int32_t puc_max = PRTY_UNIT_COUNT_MAX;

	if (algo == M0_PARITY_CAL_ALGO_XOR) {
		fuc = 1;
		puc = 1;
		puc_max = 1;
		fail_index_xor--;
		if (fail_index_xor < 0) {
			duc --;
			fail_index_xor = duc;
		}
		if(duc == 1)
			return false;
	} else if (algo == M0_PARITY_CAL_ALGO_REED_SOLOMON) {
		if (fuc <= 1) {
			puc-=3;
			if (puc <= 1) {
				duc-=9;
				puc = duc / DATA_TO_PRTY_RATIO_MAX;
			}
			fuc = puc+duc;
		}

		if (puc < 1)
			return false;
	}
	memset(fail, 0, DATA_UNIT_COUNT_MAX + puc_max);

	for (i = 0; i < duc; ++i) {
		for (j = 0; j < UNIT_BUFF_SIZE; ++j) {
			data[i][j] = (uint8_t) rand();
			expected[i][j] = data[i][j];
		}
	}

	j = 0;

	if (algo == M0_PARITY_CAL_ALGO_XOR)
		fail[fail_index_xor] = 1;
	else if (algo == M0_PARITY_CAL_ALGO_REED_SOLOMON) {
		for (i = 0; i < fuc; ++i) {
			if (j >= puc)
				break;
			fail[i] = (data[i][0] & 1) || (data[i][0] & 2) ||
				(data[i][0] & 3);
			if (fail[i])
				++j;
		}

		if (!j) { /* at least one fail */
			fail[fuc/2] = 1;
		}
	}

	*data_count = duc;
	*parity_count = puc;
	*buff_size = UNIT_BUFF_SIZE;

	if (algo == M0_PARITY_CAL_ALGO_REED_SOLOMON)
		fuc -= 3;

	return true;
}

static void test_recovery(const enum m0_parity_cal_algo algo,
			  const enum recovery_type rt)
{
	uint32_t              i;
	uint32_t              data_count;
	uint32_t              parity_count;
	uint32_t              buff_size;
	uint32_t              fail_count;
	struct m0_buf         data_buf[DATA_UNIT_COUNT_MAX];
	struct m0_buf         parity_buf[DATA_UNIT_COUNT_MAX];
	struct m0_buf         fail_buf;
	struct m0_parity_math math;

	while (config_generate(&data_count, &parity_count, &buff_size, algo)) {
		fail_count = data_count + parity_count;

		M0_UT_ASSERT(m0_parity_math_init(&math, data_count,
					         parity_count) == 0);

		for (i = 0; i < data_count; ++i) {
			m0_buf_init(&data_buf[i], data[i], buff_size);
			m0_buf_init(&parity_buf[i], parity[i], buff_size);
		}

		m0_buf_init(&fail_buf, fail, buff_size);

		m0_parity_math_calculate(&math, data_buf, parity_buf);

		unit_spoil(buff_size, fail_count, data_count);

		if (rt == FAIL_INDEX) {
			m0_parity_math_fail_index_recover(&math, data_buf,
							  parity_buf,
							  fail_index_xor);
		} else if (rt == FAIL_VECTOR)
			m0_parity_math_recover(&math, data_buf, parity_buf,
					       &fail_buf);

		m0_parity_math_fini(&math);

		if (!expected_cmp(data_count, buff_size))
			M0_UT_ASSERT(0 && "Recovered data is unexpected");
	}
}

static void test_rs_fv_recover(void)
{
	test_recovery(M0_PARITY_CAL_ALGO_REED_SOLOMON, FAIL_VECTOR);
}

static void test_xor_fv_recover(void)
{
	duc = DATA_UNIT_COUNT_MAX;
	fail_index_xor = DATA_UNIT_COUNT_MAX + 1;
	test_recovery(M0_PARITY_CAL_ALGO_XOR, FAIL_VECTOR);
}

static void test_xor_fail_idx_recover(void)
{
	duc = DATA_UNIT_COUNT_MAX;
	fail_index_xor = DATA_UNIT_COUNT_MAX + 1;
	test_recovery(M0_PARITY_CAL_ALGO_XOR, FAIL_INDEX);
}

static void test_buffer_xor(void)
{

        uint32_t      data_count;
        uint32_t      parity_count;
        uint32_t      buff_size;
        struct m0_buf data_buf[DATA_UNIT_COUNT_MAX];
        struct m0_buf parity_buf[DATA_UNIT_COUNT_MAX];

	duc = 2;
	fail_index_xor = 0;
	config_generate(&data_count, &parity_count, &buff_size,
			M0_PARITY_CAL_ALGO_XOR);

	m0_buf_init(&data_buf[0], data[0], buff_size);
	m0_buf_init(&parity_buf[0], parity[0], buff_size);
	m0_parity_math_buffer_xor(data_buf, parity_buf);
	m0_parity_math_buffer_xor(parity_buf, data_buf);

	if (!expected_cmp(data_count, buff_size))
		M0_UT_ASSERT(0 && "Recovered data is unexpected");
}

static void test_parity_math_diff(uint32_t parity_cnt)
{
	uint32_t              i;
	uint32_t              j;
	uint32_t              ret;
	uint8_t		     *arr;
	struct m0_buf         data_buf_old[DATA_UNIT_COUNT];
	struct m0_buf         data_buf_new[DATA_UNIT_COUNT];
	struct m0_parity_math math;
	struct m0_buf        *p_old;
	struct m0_buf	     *p_new;


	for (i = 0; i < DATA_UNIT_COUNT; ++i) {
		for (j = 0; j < UNIT_BUFF_SIZE; ++j) {
			data[i][j] = (uint8_t) rand();
			if (i % 2)
				data[i + DATA_UNIT_COUNT][j] =
					(uint8_t) rand();
			else
				data[i + DATA_UNIT_COUNT][j] = data[i][j];
		}
	}

	for (i = 0; i < DATA_UNIT_COUNT; ++i) {
		m0_buf_init(&data_buf_old[i], data[i], UNIT_BUFF_SIZE);
		m0_buf_init(&data_buf_new[i], data[i + DATA_UNIT_COUNT],
			    UNIT_BUFF_SIZE);
	}

	ret = m0_parity_math_init(&math, DATA_UNIT_COUNT,
				  parity_cnt);
	M0_UT_ASSERT(ret == 0);
	M0_ALLOC_ARR(p_old, parity_cnt);
	M0_UT_ASSERT(p_old != NULL);
	M0_ALLOC_ARR(p_new, parity_cnt);
	M0_UT_ASSERT(p_new != NULL);

	for(i = 0; i < parity_cnt; ++i) {
		M0_ALLOC_ARR(arr, UNIT_BUFF_SIZE);
		M0_UT_ASSERT(arr != NULL);
		m0_buf_init(&p_old[i], arr, UNIT_BUFF_SIZE);
		M0_ALLOC_ARR(arr, UNIT_BUFF_SIZE);
		M0_UT_ASSERT(arr != NULL);
		m0_buf_init(&p_new[i], arr, UNIT_BUFF_SIZE);
	}

	m0_parity_math_calculate(&math, data_buf_old, p_old);
	m0_parity_math_calculate(&math, data_buf_new, p_new);

	for (i = 0; i < DATA_UNIT_COUNT; ++i) {
		if (i % 2)
			m0_parity_math_diff(&math, data_buf_old,
					    data_buf_new,
					    p_old, i);
	}

	for(i = 0; i < parity_cnt; ++i) {
		M0_UT_ASSERT(m0_buf_eq(&p_old[i], &p_new[i]));
	}

	m0_parity_math_fini(&math);

	for(i = 0; i < parity_cnt; ++i) {
		m0_buf_free(&p_old[i]);
		m0_buf_free(&p_new[i]);
	}
	m0_free(p_old);
	m0_free(p_new);
}

static void test_parity_math_diff_xor(void)
{
	test_parity_math_diff(PARITY_UNIT_COUNT);
}


static void test_parity_math_diff_rs(void)
{
	uint32_t i;
	for (i = 2; i <= RS_MAX_PARITY_UNIT_COUNT; ++i) {
		test_parity_math_diff(i);
	}
}
const struct m0_test_suite parity_math_ut = {
        .ts_name = "parity_math-ut",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "reed_solomon_recover_with_fail_vec", test_rs_fv_recover },
                { "xor_recover_with_fail_vec", test_xor_fv_recover },
                { "xor_recover_with_fail_index", test_xor_fail_idx_recover },
                { "buffer_xor", test_buffer_xor },
                { "parity_math_diff_xor", test_parity_math_diff_xor },
                { "parity_math_diff_rs", test_parity_math_diff_rs },
                { NULL, NULL }
        }
};

enum {
	UB_ITER = 1
};

static void ut_ub_init(void)
{
	srand(1285360231);
}

void parity_math_tb(void)
{
	int ret = 0;
	uint32_t i = 0;
	struct m0_parity_math math;
	uint32_t data_count = 0;
	uint32_t parity_count = 0;
	uint32_t buff_size = 0;
	uint32_t fail_count = 0;
	struct m0_buf data_buf[DATA_UNIT_COUNT_MAX];
	struct m0_buf parity_buf[DATA_UNIT_COUNT_MAX];
	struct m0_buf fail_buf;

	config_generate(&data_count, &parity_count, &buff_size,
			M0_PARITY_CAL_ALGO_REED_SOLOMON);
	{
		fail_count = data_count + parity_count;

		ret = m0_parity_math_init(&math, data_count, parity_count);
		M0_ASSERT(ret == 0);

		for (i = 0; i < data_count; ++i) {
			m0_buf_init(&data_buf  [i], data  [i], buff_size);
			m0_buf_init(&parity_buf[i], parity[i], buff_size);
		}

		m0_buf_init(&fail_buf, fail, buff_size);

		m0_parity_math_calculate(&math, data_buf, parity_buf);

		unit_spoil(buff_size, fail_count, data_count);

		m0_parity_math_recover(&math, data_buf, parity_buf, &fail_buf);

		m0_parity_math_fini(&math);
	}
}

void ub_small_4096(int iter)
{
	UNIT_BUFF_SIZE = 4096;
	duc = 10;
	puc = 5;
	fuc = duc+puc;
	parity_math_tb();
}

void ub_medium_4096(int iter)
{
	UNIT_BUFF_SIZE = 4096;
	duc = 20;
	puc = 6;
	fuc = duc+puc;
	parity_math_tb();
}

void ub_large_4096(int iter)
{
	UNIT_BUFF_SIZE = 4096;
	duc = 30;
	puc = 12;
	fuc = duc+puc;
	parity_math_tb();
}

void ub_small_1048576(int iter)
{
	UNIT_BUFF_SIZE = 1048576;
	duc = 3;
	puc = 2;
	fuc = duc+puc;
	parity_math_tb();
}

void ub_medium_1048576(int iter)
{
	UNIT_BUFF_SIZE = 1048576;
	duc = 6;
	puc = 3;
	fuc = duc+puc;
	parity_math_tb();
}

void ub_large_1048576(int iter)
{
	UNIT_BUFF_SIZE = 1048576;
	duc = 8;
	puc = 4;
	fuc = duc+puc;
	parity_math_tb();
}

void ub_small_32768(int iter)
{
	UNIT_BUFF_SIZE = 32768;
	duc = 10;
	puc = 5;
	fuc = duc+puc;
	parity_math_tb();
}

void ub_medium_32768(int iter)
{
	UNIT_BUFF_SIZE = 32768;
	duc = 20;
	puc = 6;
	fuc = duc+puc;
	parity_math_tb();
}

void ub_large_32768(int iter)
{
	UNIT_BUFF_SIZE = 32768;
	duc = 30;
	puc = 12;
	fuc = duc+puc;
	parity_math_tb();
}

struct m0_ub_set m0_parity_math_ub = {
        .us_name = "m0_parity_math-ub",
        .us_init = ut_ub_init,
        .us_fini = NULL,
        .us_run  = {
		/*             parity_math-: */
                { .ub_name  = "s 10/05/ 4K",
                  .ub_iter  = UB_ITER,
                  .ub_round = ub_small_4096 },

                { .ub_name  = "m 20/06/ 4K",
                  .ub_iter  = UB_ITER,
                  .ub_round = ub_medium_4096 },

                { .ub_name  = "l 30/12/ 4K",
                  .ub_iter  = UB_ITER,
                  .ub_round = ub_large_4096 },

                { .ub_name  = "s 10/05/32K",
                  .ub_iter  = UB_ITER,
                  .ub_round = ub_small_32768 },

                { .ub_name  = "m 20/06/32K",
                  .ub_iter  = UB_ITER,
                  .ub_round = ub_medium_32768 },

                { .ub_name  = "l 30/12/32K",
                  .ub_iter  = UB_ITER,
                  .ub_round = ub_large_32768 },

                { .ub_name  = "s  03/02/ 1M",
                  .ub_iter  = UB_ITER,
                  .ub_round = ub_small_1048576 },

                { .ub_name  = "m 06/03/ 1M",
                  .ub_iter  = UB_ITER,
                  .ub_round = ub_medium_1048576 },

                { .ub_name  = "l 08/04/ 1M",
                  .ub_iter  = UB_ITER,
                  .ub_round = ub_large_1048576 },

		{ .ub_name = NULL}
	}
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
