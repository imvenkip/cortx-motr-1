/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/types.h"
#include "lib/adt.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/thread.h"

#include "lib/ub.h"
#include "lib/ut.h"

#include "matvec.h"
#include "ls_solve.h"
#include "parity_math.h"

struct tb_cfg {
	uint32_t  tc_data_count;
	uint32_t  tc_parity_count;
	uint32_t  tc_fail_count;

	uint32_t  tc_block_size;

	uint8_t **tc_data;
	uint8_t **tc_parity;
	uint8_t  *tc_fail;
};

enum {
	UB_ITER = 1
};

static void ub_init(void)
{
	srand(1285360231);
}

void tb_cfg_init(struct tb_cfg *cfg,
		 uint32_t data_count,
		 uint32_t parity_count,
		 uint32_t block_size)
{
	uint32_t i;
	uint32_t j;

	cfg->tc_data_count	= data_count;
	cfg->tc_parity_count	= parity_count;
	cfg->tc_fail_count	= data_count + parity_count;
	cfg->tc_block_size      = block_size;


	/* allocate and prepare data */
	cfg->tc_data = c2_alloc(data_count * sizeof(uint8_t*));
	C2_ASSERT(cfg->tc_data != NULL);

	for (i = 0; i < data_count; ++i) {
		cfg->tc_data[i] = c2_alloc(block_size * sizeof(uint8_t));
		C2_ASSERT(cfg->tc_data[i] != NULL);

		for (j = 0; j < block_size; ++j)
			cfg->tc_data[i][j] = (uint8_t)rand();
	}

	/* allocate parity */
	cfg->tc_parity = c2_alloc(parity_count * sizeof(uint8_t*));
	C2_ASSERT(cfg->tc_parity != NULL);

	for (i = 0; i < parity_count; ++i) {
		cfg->tc_parity[i] = c2_alloc(block_size * sizeof(uint8_t));
		C2_ASSERT(cfg->tc_data[i] != NULL);
	}

	/* allocate and set fail info */
	cfg->tc_fail = c2_alloc(cfg->tc_fail_count * sizeof(uint8_t));
	C2_ASSERT(cfg->tc_fail != NULL);

	for (i = 0; i < parity_count; ++i)
		cfg->tc_fail[i] = 1; /* maximal possible fails */
}

void tb_cfg_fini(struct tb_cfg *cfg)
{
	uint32_t i;

	for (i = 0; i < cfg->tc_data_count; ++i)
		c2_free(cfg->tc_data[i]);
	c2_free(cfg->tc_data);

	for (i = 0; i < cfg->tc_parity_count; ++i)
		c2_free(cfg->tc_parity[i]);
	c2_free(cfg->tc_parity);

	c2_free(cfg->tc_fail);
}

void tb_thread(struct tb_cfg *cfg)
{
	int ret = 0;
	uint32_t i = 0;

	uint32_t data_count	= cfg->tc_data_count;
	uint32_t parity_count	= cfg->tc_parity_count;
	uint32_t buff_size	= cfg->tc_block_size;
	uint32_t fail_count	= data_count + parity_count;

	struct c2_parity_math math;
	struct c2_buf *data_buf = 0;
	struct c2_buf *parity_buf = 0;
	struct c2_buf fail_buf;

	data_buf = c2_alloc(data_count * sizeof(struct c2_buf));
	C2_ASSERT(data_buf);

	parity_buf = c2_alloc(parity_count * sizeof(struct c2_buf));
	C2_ASSERT(parity_buf);


	ret = c2_parity_math_init(&math, data_count, parity_count);
	C2_ASSERT(ret == 0);

	for (i = 0; i < data_count; ++i)
		c2_buf_init(&data_buf  [i], cfg->tc_data  [i], buff_size);

	for (i = 0; i < parity_count; ++i)
		c2_buf_init(&parity_buf[i], cfg->tc_parity[i], buff_size);


	c2_buf_init(&fail_buf, cfg->tc_fail, fail_count);
	c2_parity_math_calculate(&math, data_buf, parity_buf);

	c2_parity_math_recover(&math, data_buf, parity_buf, &fail_buf);

	c2_parity_math_fini(&math);
	c2_free(data_buf);
	c2_free(parity_buf);
}

static void ub_mt_test(uint32_t data_count,
		       uint32_t parity_count,
		       uint32_t block_size)
{
	uint32_t num_threads = 30;
	uint32_t i;
	int result = 0;

	struct tb_cfg *cfg;
	struct c2_thread *threads;

	threads = c2_alloc(num_threads * sizeof(struct c2_thread));
	C2_ASSERT(threads != NULL);

	cfg = c2_alloc(num_threads * sizeof(struct tb_cfg));
	C2_ASSERT(cfg != NULL);

	for (i = 0; i < num_threads; i++) {
		tb_cfg_init(&cfg[i], data_count, parity_count, block_size);
		result = C2_THREAD_INIT(&threads[i], struct tb_cfg*, NULL,
					&tb_thread, &cfg[i],
					"tb_thread%d", i);
		C2_ASSERT(result == 0);
	}

	for (i = 0; i < num_threads; i++) {
		result = c2_thread_join(&threads[i]);
		C2_ASSERT(result == 0);
		tb_cfg_fini(&cfg[i]);
	}

	c2_free(cfg);
	c2_free(threads);
}

void ub_small_4096() {
	ub_mt_test(10, 3, 4096);
}

void ub_medium_4096() {
	/* ub_mt_test(20, 6, 4096); */
}

void ub_large_4096() {
	/* ub_mt_test(30, 8, 4096); */
}

void ub_small_1048576() {
	/* ub_mt_test(10, 3, 1048576); */
}

void ub_medium_1048576() {
	/* ub_mt_test(20, 6, 1048576); */
}

void ub_large_1048576() {
	/* ub_mt_test(30, 8, 1048576); */
}

void ub_small_32768() {
	/* ub_mt_test(10, 3, 32768); */
}

void ub_medium_32768() {
	/* ub_mt_test(20, 6, 32768); */
}

void ub_large_32768() {
	/* ub_mt_test(30, 8, 32768); */
}

struct c2_ub_set c2_parity_math_mt_ub = {
        .us_name = "c2_parity_math-ub",
        .us_init = ub_init,
        .us_fini = NULL,
        .us_run  = {
		/*             parity_math-: */
                { .ut_name  = "s 10/03/ 4K",
                  .ut_iter  = UB_ITER,
                  .ut_round = ub_small_4096 },

                { .ut_name  = "m 20/06/ 4K",
                  .ut_iter  = UB_ITER,
                  .ut_round = ub_medium_4096 },

                { .ut_name  = "l 30/08/ 4K",
                  .ut_iter  = UB_ITER,
                  .ut_round = ub_large_4096 },

                { .ut_name  = "s 10/03/32K",
                  .ut_iter  = UB_ITER,
                  .ut_round = ub_small_32768 },

                { .ut_name  = "m 20/06/32K",
                  .ut_iter  = UB_ITER,
                  .ut_round = ub_medium_32768 },

                { .ut_name  = "l 30/08/32K",
                  .ut_iter  = UB_ITER,
                  .ut_round = ub_large_32768 },

                { .ut_name  = "s 10/05/ 1M",
                  .ut_iter  = UB_ITER,
                  .ut_round = ub_small_1048576 },

                { .ut_name  = "m 20/06/ 1M",
                  .ut_iter  = UB_ITER,
                  .ut_round = ub_medium_1048576 },

                { .ut_name  = "l 30/08/ 1M",
                  .ut_iter  = UB_ITER,
                  .ut_round = ub_large_1048576 },

		{ .ut_name = NULL}
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
