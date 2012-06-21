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
#  include "config.h"
#endif

#include <stdlib.h>    /* system */
#include <stdio.h>     /* fopen, fgetc, ... */
#include <unistd.h>    /* unlink */
#include <sys/stat.h>  /* mkdir */
#include <sys/types.h> /* mkdir */

#include "dtm/dtm.h"     /* c2_dtx */
#include "lib/arith.h"   /* min64u */
#include "lib/misc.h"    /* C2_SET0 */
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/ub.h"
#include "lib/ut.h"
#include "lib/assert.h"

#include "stob/stob.h"
#include "stob/linux.h"
#include "stob/ad.h"
#include "balloc/balloc.h"

/**
   @addtogroup stob
   @{
 */

enum {
	NR    = 4,
	MIN_BUF_SIZE = 4096,
	MIN_BUF_SIZE_IN_BLOCKS = 4,
};

static struct c2_stob_domain *dom_back;
static struct c2_stob_domain *dom_fore;

static const struct c2_stob_id id_back = {
	.si_bits = {
		.u_hi = 1,
		.u_lo = 2
	}
};

static const struct c2_stob_id id_fore = {
	.si_bits = {
		.u_hi = 11,
		.u_lo = 22
	}
};

static const char db_name[] = "ut-ad";

static struct c2_stob *obj_back;
static struct c2_stob *obj_fore;
static const char path[] = "./__s/o/0000000000000001.0000000000000002";
static struct c2_stob_io io;
static c2_bcount_t user_vec[NR];
static char *user_buf[NR];
static char *read_buf[NR];
static char *user_bufs[NR];
static char *read_bufs[NR];
static c2_bindex_t stob_vec[NR];
static struct c2_clink clink;
static struct c2_dtx tx;
static struct c2_dbenv db;
static uint32_t block_shift;
static uint32_t buf_size;

struct mock_balloc {
	c2_bindex_t         mb_next;
	struct c2_ad_balloc mb_ballroom;
};

static struct mock_balloc *b2mock(struct c2_ad_balloc *ballroom)
{
	return container_of(ballroom, struct mock_balloc, mb_ballroom);
}

static int mock_balloc_init(struct c2_ad_balloc *ballroom, struct c2_dbenv *db,
			    uint32_t bshift, c2_bindex_t container_size,
			    c2_bcount_t groupsize, c2_bcount_t res_groups)
{
	return 0;
}

static void mock_balloc_fini(struct c2_ad_balloc *ballroom)
{
}

static int mock_balloc_alloc(struct c2_ad_balloc *ballroom, struct c2_dtx *dtx,
			     c2_bcount_t count, struct c2_ext *out)
{
	struct mock_balloc *mb = b2mock(ballroom);
	c2_bcount_t giveout;

	giveout = min64u(count, 500000);
	out->e_start = mb->mb_next;
	out->e_end   = mb->mb_next + giveout;
	mb->mb_next += giveout + 1;
	/* printf("allocated %8lx/%8lx bytes: [%8lx .. %8lx)\n",
	   giveout, count,
	       out->e_start, out->e_end); */
	return 0;
}

static int mock_balloc_free(struct c2_ad_balloc *ballroom, struct c2_dtx *dtx,
			    struct c2_ext *ext)
{
	/* printf("freed     %8lx bytes: [%8lx .. %8lx)\n", c2_ext_length(ext),
	       ext->e_start, ext->e_end); */
	return 0;
}

static const struct c2_ad_balloc_ops mock_balloc_ops = {
	.bo_init  = mock_balloc_init,
	.bo_fini  = mock_balloc_fini,
	.bo_alloc = mock_balloc_alloc,
	.bo_free  = mock_balloc_free,
};

struct mock_balloc mb = {
	.mb_next = 0,
	.mb_ballroom = {
		.ab_ops = &mock_balloc_ops
	}
};

static int test_ad_init(void)
{
	int i;
	int result;

	result = system("rm -fr ./__s");
	C2_ASSERT(result == 0);

	result = mkdir("./__s", 0700);
	C2_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	result = mkdir("./__s/o", 0700);
	C2_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	result = c2_dbenv_init(&db, db_name, 0);
	C2_ASSERT(result == 0);

	result = c2_stob_domain_locate(&c2_linux_stob_type, "./__s", &dom_back);
	C2_ASSERT(result == 0);

	result = c2_stob_find(dom_back, &id_back, &obj_back);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj_back->so_state == CSS_UNKNOWN);

	result = c2_stob_create(obj_back, NULL);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj_back->so_state == CSS_EXISTS);

	result = c2_stob_domain_locate(&c2_ad_stob_type, "", &dom_fore);
	C2_ASSERT(result == 0);

	result = c2_ad_stob_setup(dom_fore, &db, obj_back, &mb.mb_ballroom,
				  BALLOC_DEF_CONTAINER_SIZE,
				  BALLOC_DEF_BLOCK_SHIFT,
				  BALLOC_DEF_BLOCKS_PER_GROUP,
				  BALLOC_DEF_RESERVED_GROUPS);
	C2_ASSERT(result == 0);

	c2_stob_put(obj_back);

	result = c2_stob_find(dom_fore, &id_fore, &obj_fore);
	C2_ASSERT(result == 0);
	C2_ASSERT(obj_fore->so_state == CSS_UNKNOWN);

	c2_dtx_init(&tx);
	result = dom_fore->sd_ops->sdo_tx_make(dom_fore, &tx);
	C2_ASSERT(result == 0);

	result = c2_stob_locate(obj_fore, &tx);
	C2_ASSERT(result == 0 || result == -ENOENT);
	if (result == -ENOENT) {
		result = c2_stob_create(obj_fore, &tx);
		C2_ASSERT(result == 0);
	}
	C2_ASSERT(obj_fore->so_state == CSS_EXISTS);

	block_shift = obj_fore->so_op->sop_block_shift(obj_fore);
	/* buf_size is chosen so it would be at least MIN_BUF_SIZE in bytes
	 * or it would consist of at least MIN_BUF_SIZE_IN_BLOCKS blocks */
	buf_size = max_check(MIN_BUF_SIZE
			, (1 << block_shift) * MIN_BUF_SIZE_IN_BLOCKS);

	for (i = 0; i < ARRAY_SIZE(user_buf); ++i) {
		user_buf[i] = c2_alloc_aligned(buf_size, block_shift);
		C2_ASSERT(user_buf[i] != NULL);
	}

	for (i = 0; i < ARRAY_SIZE(read_buf); ++i) {
		read_buf[i] = c2_alloc_aligned(buf_size, block_shift);
		C2_ASSERT(read_buf[i] != NULL);
	}

	for (i = 0; i < NR; ++i) {
		user_bufs[i] = c2_stob_addr_pack(user_buf[i], block_shift);
		read_bufs[i] = c2_stob_addr_pack(read_buf[i], block_shift);
		user_vec[i] = buf_size >> block_shift;
		stob_vec[i] = (buf_size * (2 * i + 1)) >> block_shift;
		memset(user_buf[i], ('a' + i)|1, buf_size);
	}
	return result;
}

static int test_ad_fini(void)
{
	int i;

	c2_dtx_done(&tx);

	c2_stob_put(obj_fore);
	dom_fore->sd_ops->sdo_fini(dom_fore);
	dom_back->sd_ops->sdo_fini(dom_back);
	c2_dbenv_fini(&db);

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
	io.si_user.ov_vec.v_nr = i;
	io.si_user.ov_vec.v_count = user_vec;
	io.si_user.ov_buf = (void **)user_bufs;

	io.si_stob.iv_vec.v_nr = i;
	io.si_stob.iv_vec.v_count = user_vec;
	io.si_stob.iv_index = stob_vec;

	c2_clink_init(&clink, NULL);
	c2_clink_add(&io.si_wait, &clink);

	result = c2_stob_io_launch(&io, obj_fore, &tx, NULL);
	C2_ASSERT(result == 0);

	c2_chan_wait(&clink);

	C2_ASSERT(io.si_rc == 0);
	C2_ASSERT(io.si_count == (buf_size * i) >> block_shift);

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
	io.si_user.ov_vec.v_nr = i;
	io.si_user.ov_vec.v_count = user_vec;
	io.si_user.ov_buf = (void **)read_bufs;

	io.si_stob.iv_vec.v_nr = i;
	io.si_stob.iv_vec.v_count = user_vec;
	io.si_stob.iv_index = stob_vec;

	c2_clink_init(&clink, NULL);
	c2_clink_add(&io.si_wait, &clink);

	result = c2_stob_io_launch(&io, obj_fore, &tx, NULL);
	C2_ASSERT(result == 0);

	c2_chan_wait(&clink);

	C2_ASSERT(io.si_rc == 0);
	C2_ASSERT(io.si_count == (buf_size * i) >> block_shift);

	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	c2_stob_io_fini(&io);
}

static void test_ad_rw_unordered()
{
	int i;

	/* Unorderd write requests */
	for (i = NR/2; i < NR; ++i) {
		stob_vec[i-(NR/2)] = (buf_size * (i + 1)) >> block_shift;
		memset(user_buf[i-(NR/2)], ('a' + i)|1, buf_size);
	}
	test_write(NR/2);

	for (i = 0; i < NR/2; ++i) {
	 	stob_vec[i] = (buf_size * (i + 1)) >> block_shift;
		memset(user_buf[i], ('a' + i)|1, buf_size);
	}
	test_write(NR/2);

	for (i = 0; i < NR; ++i) {
		stob_vec[i] = (buf_size * (i + 1)) >> block_shift;
		memset(user_buf[i], ('a' + i)|1, buf_size);
	}

	/* This generates unordered offsets for back stob io */
	test_read(NR);
	for (i = 0; i < NR; ++i)
		C2_ASSERT(memcmp(user_buf[i], read_buf[i], buf_size) == 0);
}

/**
   AD unit-test.
 */
static void test_ad(void)
{
	int i;

	for (i = 1; i < NR; ++i)
		test_write(i);

	for (i = 1; i < NR; ++i) {
		int j;
		test_read(i);
		for (j = 0; j < i; ++j)
			C2_ASSERT(memcmp(user_buf[j], read_buf[j], buf_size) == 0);
	}
}

const struct c2_test_suite ad_ut = {
	.ts_name = "ad-ut",
	.ts_init = test_ad_init,
	.ts_fini = test_ad_fini,
	.ts_tests = {
		{ "ad", test_ad },
		{ "ad-rw-unordered", test_ad_rw_unordered },
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

struct c2_ub_set c2_ad_ub = {
	.us_name = "ad-ub",
	.us_init = (void *)test_ad_init,
	.us_fini = (void *)test_ad_fini,
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
