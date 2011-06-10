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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 08/24/2010
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>    /* system */
#include <stdio.h>     /* fopen, fgetc, ... */
#include <unistd.h>    /* unlink */
#include <sys/stat.h>  /* mkdir */
#include <sys/types.h> /* mkdir */

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/memory.h" /* c2_alloc_align */
#include "lib/errno.h"
#include "lib/ub.h"
#include "lib/ut.h"
#include "lib/assert.h"

#include "stob/stob.h"
#include "stob/linux.h"

/**
   @addtogroup stob
   @{
 */

enum {
	NR    = 3,
	SHIFT = 0, /* 12, */
	COUNT = (1 << SHIFT)*1024
};

static struct c2_stob_domain *dom;
static const struct c2_stob_id id = {
	.si_bits = {
		.u_hi = 1,
		.u_lo = 2
	}
};
static struct c2_stob *obj;
static struct c2_stob *obj1;
static const char path[] = "./__s/o/0000000000000001.0000000000000002";
static struct c2_stob_io io;
static c2_bcount_t user_vec[NR];
static char *user_buf[NR];
static char *read_buf[NR];
static char *user_bufs[NR];
static char *read_bufs[NR];
static c2_bindex_t stob_vec[NR];
static struct c2_clink clink;
static FILE *f;

static int test_adieu_init(void)
{
	int i;
	int result;

	for (i = 0; i < ARRAY_SIZE(user_buf); ++i) {
		user_buf[i] = c2_alloc_aligned(COUNT, SHIFT);
		C2_ASSERT(user_buf[i] != NULL);
	}

	for (i = 0; i < ARRAY_SIZE(read_buf); ++i) {
		read_buf[i] = c2_alloc_aligned(COUNT, SHIFT);
		C2_ASSERT(read_buf[i] != NULL);
	}

	result = system("rm -fr ./__s");
	C2_ASSERT(result == 0);

	result = mkdir("./__s", 0700);
	C2_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	result = mkdir("./__s/o", 0700);
	C2_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	result = linux_stob_type.st_op->sto_domain_locate(&linux_stob_type, 
							  "./__s", &dom);
	C2_ASSERT(result == 0);

	result = dom->sd_ops->sdo_stob_find(dom, &id, &obj);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj->so_state == CSS_UNKNOWN);

	result = c2_stob_locate(obj, NULL);
	C2_ASSERT(result == -ENOENT);
	C2_ASSERT(obj->so_state == CSS_NOENT);

	result = dom->sd_ops->sdo_stob_find(dom, &id, &obj1);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj == obj1);

	c2_stob_put(obj);
	c2_stob_put(obj1);

	result = dom->sd_ops->sdo_stob_find(dom, &id, &obj);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj->so_state == CSS_UNKNOWN);

	result = c2_stob_create(obj, NULL);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj->so_state == CSS_EXISTS);
	c2_stob_put(obj);

	result = dom->sd_ops->sdo_stob_find(dom, &id, &obj);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj->so_state == CSS_UNKNOWN);

	result = c2_stob_locate(obj, NULL);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj->so_state == CSS_EXISTS);
	C2_ASSERT(obj->so_op->sop_block_shift(obj) == SHIFT);

	for (i = 0; i < NR; ++i) {
		user_bufs[i] = c2_stob_addr_pack(user_buf[i], SHIFT);
		read_bufs[i] = c2_stob_addr_pack(read_buf[i], SHIFT);
		user_vec[i] = COUNT >> SHIFT;
		stob_vec[i] = (COUNT * (2 * i + 1)) >> SHIFT;
		memset(user_buf[i], ('a' + i)|1, COUNT);
	}
	return result;
}

static int test_adieu_fini(void)
{
	int i;

	c2_stob_put(obj);
	dom->sd_ops->sdo_fini(dom);

	for (i = 0; i < ARRAY_SIZE(user_buf); ++i)
		c2_free(user_buf[i]);

	for (i = 0; i < ARRAY_SIZE(read_buf); ++i)
		c2_free(read_buf[i]);
	return 0;
}

static void test_write(int i)
{
	int result;
	c2_stob_io_init(&io);

	io.si_opcode = SIO_WRITE;
	io.si_flags  = 0;
	io.si_user.div_vec.ov_vec.v_nr = i;
	io.si_user.div_vec.ov_vec.v_count = user_vec;
	io.si_user.div_vec.ov_buf = (void **)user_bufs;

	io.si_stob.iv_vec.v_nr = i;
	io.si_stob.iv_vec.v_count = user_vec;
	io.si_stob.iv_index = stob_vec;

	c2_clink_init(&clink, NULL);
	c2_clink_add(&io.si_wait, &clink);

	result = c2_stob_io_launch(&io, obj, NULL, NULL);
	C2_ASSERT(result == 0);

	c2_chan_wait(&clink);

	C2_ASSERT(io.si_rc == 0);
	C2_ASSERT(io.si_count == (COUNT * i) >> SHIFT);

	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	c2_stob_io_fini(&io);
}

static void test_read(int i)
{
	int result;
	c2_stob_io_init(&io);

	io.si_opcode = SIO_READ;
	io.si_flags  = 0;
	io.si_user.div_vec.ov_vec.v_nr = i;
	io.si_user.div_vec.ov_vec.v_count = user_vec;
	io.si_user.div_vec.ov_buf = (void **)read_bufs;

	io.si_stob.iv_vec.v_nr = i;
	io.si_stob.iv_vec.v_count = user_vec;
	io.si_stob.iv_index = stob_vec;

	c2_clink_init(&clink, NULL);
	c2_clink_add(&io.si_wait, &clink);

	result = c2_stob_io_launch(&io, obj, NULL, NULL);
	C2_ASSERT(result == 0);

	c2_chan_wait(&clink);

	C2_ASSERT(io.si_rc == 0);
	C2_ASSERT(io.si_count == (COUNT * i) >> SHIFT);

	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	c2_stob_io_fini(&io);
}

/**
   Adieu unit-test. 
 */
static void test_adieu(void)
{
	int ch;
	int i;
	int j;

	for (i = 1; i < NR; ++i) {
		test_write(i);

		f = fopen(path, "r");
		for (j = 0; j < i; ++j) {
			int k;

			for (k = 0; k < COUNT; ++k) {
				ch = fgetc(f);
				C2_ASSERT(ch == '\0');
				C2_ASSERT(!feof(f));
			}
			for (k = 0; k < COUNT; ++k) {
				ch = fgetc(f);
				C2_ASSERT(ch != '\0');
				C2_ASSERT(!feof(f));
			}
		}
		ch = fgetc(f);
		C2_ASSERT(ch == EOF);
		fclose(f);
	}

	for (i = 1; i < NR; ++i) {
		test_read(i);
		C2_ASSERT(memcmp(user_buf[i - 1], read_buf[i - 1], COUNT) == 0);
	}
}

const struct c2_test_suite adieu_ut = {
	.ts_name = "adieu-ut",
	.ts_init = test_adieu_init,
	.ts_fini = test_adieu_fini,
	.ts_tests = {
		{ "adieu", test_adieu },
		{ NULL, NULL }
	}
};

enum {
	UB_ITER = 100
};

static void ub_write(int i)
{
	test_write(NR - 1);
}

static void ub_read(int i)
{
	test_read(NR - 1);
}

struct c2_ub_set c2_adieu_ub = {
	.us_name = "adieu-ub",
	.us_init = (void *)test_adieu_init,
	.us_fini = (void *)test_adieu_fini,
	.us_run  = { 
		{ .ut_name = "write-prime",
		  .ut_iter = 1,
		  .ut_round = ub_write },

		{ .ut_name = "write",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_write },

		{ .ut_name = "read",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_read },

		{ .ut_name = NULL }
	}
};

/** @} end group stob */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
