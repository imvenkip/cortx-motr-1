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

#include <stdlib.h>    /* system */
#include <stdio.h>     /* fopen, fgetc, ... */
#include <unistd.h>    /* unlink */
#include <sys/stat.h>  /* mkdir */
#include <sys/types.h> /* mkdir */

#include "lib/arith.h"   /* min64u */
#include "lib/misc.h"    /* M0_SET0 */
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/ub.h"
#include "lib/assert.h"
#include "ut/ut.h"
#include "ut/ast_thread.h"

#include "dtm/dtm.h"     /* m0_dtx */
#include "stob/stob.h"
#include "stob/linux.h"
#include "stob/ad.h"
#include "balloc/balloc.h"

#include "be/ut/helper.h"

/**
   @addtogroup stob
   @{
 */

enum {
	NR    = 4,
	MIN_BUF_SIZE = 4096,
	MIN_BUF_SIZE_IN_BLOCKS = 4,
};

static struct m0_stob_domain *dom_back;
static struct m0_stob_domain *dom_fore;

static const struct m0_stob_id id_back = {
	.si_bits = {
		.u_hi = 1,
		.u_lo = 2
	}
};

static const struct m0_stob_id id_fore = {
	.si_bits = {
		.u_hi = 11,
		.u_lo = 22
	}
};

static const char db_name[] = "ut-ad";

static struct m0_stob *obj_back;
static struct m0_stob *obj_fore;
static const char path[] = "./__s/o/0000000000000001.0000000000000002";
static struct m0_stob_io io;
static m0_bcount_t user_vec[NR];
static char *user_buf[NR];
static char *read_buf[NR];
static char *user_bufs[NR];
static char *read_bufs[NR];
static m0_bindex_t stob_vec[NR];
static struct m0_clink clink;
static struct m0_dtx tx;
struct m0_be_ut_backend	 ut_be;
struct m0_be_ut_seg	 ut_seg;
static struct m0_be_seg *db;
static struct m0_sm_group *sm_grp;
static uint32_t block_shift;
static uint32_t buf_size;

struct mock_balloc {
	m0_bindex_t         mb_next;
	struct m0_ad_balloc mb_ballroom;
};

static struct mock_balloc *b2mock(struct m0_ad_balloc *ballroom)
{
	return container_of(ballroom, struct mock_balloc, mb_ballroom);
}

static int mock_balloc_init(struct m0_ad_balloc *ballroom, struct m0_be_seg *db,
			    struct m0_sm_group *grp,
			    uint32_t bshift, m0_bindex_t container_size,
			    m0_bcount_t groupsize, m0_bcount_t res_groups)
{
	return 0;
}

static void mock_balloc_fini(struct m0_ad_balloc *ballroom)
{
}

static int mock_balloc_alloc(struct m0_ad_balloc *ballroom, struct m0_dtx *dtx,
			     m0_bcount_t count, struct m0_ext *out)
{
	struct mock_balloc *mb = b2mock(ballroom);
	m0_bcount_t giveout;

	giveout = min64u(count, 500000);
	out->e_start = mb->mb_next;
	out->e_end   = mb->mb_next + giveout;
	mb->mb_next += giveout + 1;
	/* printf("allocated %8lx/%8lx bytes: [%8lx .. %8lx)\n",
	   giveout, count,
	       out->e_start, out->e_end); */
	return 0;
}

static int mock_balloc_free(struct m0_ad_balloc *ballroom, struct m0_dtx *dtx,
			    struct m0_ext *ext)
{
	/* printf("freed     %8lx bytes: [%8lx .. %8lx)\n", m0_ext_length(ext),
	       ext->e_start, ext->e_end); */
	return 0;
}

static const struct m0_ad_balloc_ops mock_balloc_ops = {
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
	int	i;
	int	result;

	result = system("rm -fr ./__s");
	M0_ASSERT(result == 0);

	result = mkdir("./__s", 0700);
	M0_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	result = mkdir("./__s/o", 0700);
	M0_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	/* Init BE */
	m0_be_ut_backend_init(&ut_be);
	m0_be_ut_seg_init(&ut_seg, &ut_be, 1ULL << 24);
	m0_be_ut_seg_allocator_init(&ut_seg, &ut_be);
	db = &ut_seg.bus_seg;

	sm_grp = m0_be_ut_backend_sm_group_lookup(&ut_be);

	result = m0_linux_stob_domain_locate("./__s", &dom_back);
	M0_ASSERT(result == 0);

	result = m0_stob_find(dom_back, &id_back, &obj_back);
	M0_ASSERT(result == 0);
	M0_ASSERT(obj_back->so_state == CSS_UNKNOWN);

	result = m0_stob_create(obj_back, NULL);
	M0_ASSERT(result == 0);
	M0_ASSERT(obj_back->so_state == CSS_EXISTS);

	result = m0_ad_stob_domain_locate("", db, sm_grp,
					  &dom_fore, obj_back);
	M0_ASSERT(result == 0);

	result = m0_ad_stob_setup(dom_fore, db, sm_grp, obj_back,
				  &mb.mb_ballroom,
				  BALLOC_DEF_CONTAINER_SIZE,
				  BALLOC_DEF_BLOCK_SHIFT,
				  BALLOC_DEF_BLOCKS_PER_GROUP,
				  BALLOC_DEF_RESERVED_GROUPS);
	M0_ASSERT(result == 0);

	m0_stob_put(obj_back);

	result = m0_stob_find(dom_fore, &id_fore, &obj_fore);
	M0_ASSERT(result == 0);
	M0_ASSERT(obj_fore->so_state == CSS_UNKNOWN);

	block_shift = obj_fore->so_op->sop_block_shift(obj_fore);
	/* buf_size is chosen so it would be at least MIN_BUF_SIZE in bytes
	 * or it would consist of at least MIN_BUF_SIZE_IN_BLOCKS blocks */
	buf_size = max_check(MIN_BUF_SIZE
			, (1 << block_shift) * MIN_BUF_SIZE_IN_BLOCKS);

	m0_dtx_init(&tx, db->bs_domain, sm_grp);
	m0_stob_create_credit(obj_fore, &tx.tx_betx_cred);
	result = dom_fore->sd_ops->sdo_tx_make(dom_fore, &tx);
	M0_ASSERT(result == 0);
	M0_ASSERT(m0_be_tx_state(&tx.tx_betx) == M0_BTS_ACTIVE);

	result = m0_stob_locate(obj_fore);
	M0_ASSERT(result == 0 || result == -ENOENT);
	if (result == -ENOENT) {
		result = m0_stob_create(obj_fore, &tx);
		M0_ASSERT(result == 0);
	}
	M0_ASSERT(obj_fore->so_state == CSS_EXISTS);
	result = m0_dtx_done_sync(&tx);
	M0_ASSERT(result == 0);
	m0_dtx_fini(&tx);

	for (i = 0; i < ARRAY_SIZE(user_buf); ++i) {
		user_buf[i] = m0_alloc_aligned(buf_size, block_shift);
		M0_ASSERT(user_buf[i] != NULL);
	}

	for (i = 0; i < ARRAY_SIZE(read_buf); ++i) {
		read_buf[i] = m0_alloc_aligned(buf_size, block_shift);
		M0_ASSERT(read_buf[i] != NULL);
	}

	for (i = 0; i < NR; ++i) {
		user_bufs[i] = m0_stob_addr_pack(user_buf[i], block_shift);
		read_bufs[i] = m0_stob_addr_pack(read_buf[i], block_shift);
		user_vec[i] = buf_size >> block_shift;
		stob_vec[i] = (buf_size * (2 * i + 1)) >> block_shift;
		memset(user_buf[i], ('a' + i)|1, buf_size);
	}

	return result;
}

static int test_ad_fini(void)
{
	int i;

	m0_stob_put(obj_fore);
	dom_fore->sd_ops->sdo_fini(dom_fore, sm_grp);
	dom_back->sd_ops->sdo_fini(dom_back, NULL);

	m0_be_ut_seg_allocator_fini(&ut_seg, &ut_be);
	m0_be_ut_seg_fini(&ut_seg);
	m0_be_ut_backend_fini(&ut_be);

	for (i = 0; i < ARRAY_SIZE(user_buf); ++i)
		m0_free(user_buf[i]);

	for (i = 0; i < ARRAY_SIZE(read_buf); ++i)
		m0_free(read_buf[i]);

	return 0;
}

static void test_write(int i)
{
	int			result;
	struct m0_fol_rec_part *fol_rec_part;

	/* @Note: This Fol record part object is not freed and shows as leak,
	 * as it is passed as embbedded object in other places.
	 */
	M0_ALLOC_PTR(fol_rec_part);
	M0_UB_ASSERT(fol_rec_part != NULL);

	m0_stob_io_init(&io);

	io.si_opcode = SIO_WRITE;
	io.si_flags  = 0;
	io.si_fol_rec_part = fol_rec_part;
	io.si_user.ov_vec.v_nr = i;
	io.si_user.ov_vec.v_count = user_vec;
	io.si_user.ov_buf = (void **)user_bufs;

	io.si_stob.iv_vec.v_nr = i;
	io.si_stob.iv_vec.v_count = user_vec;
	io.si_stob.iv_index = stob_vec;

	m0_clink_init(&clink, NULL);
	m0_clink_add_lock(&io.si_wait, &clink);

	result = m0_stob_io_launch(&io, obj_fore, &tx, NULL);
	M0_ASSERT(result == 0);

	m0_chan_wait(&clink);

	M0_ASSERT(io.si_rc == 0);
	M0_ASSERT(io.si_count == (buf_size * i) >> block_shift);

	m0_clink_del_lock(&clink);
	m0_clink_fini(&clink);

	m0_stob_io_fini(&io);
}

static void test_read(int i)
{
	int result;

	m0_stob_io_init(&io);

	io.si_opcode = SIO_READ;
	io.si_flags  = 0;
	io.si_user.ov_vec.v_nr = i;
	io.si_user.ov_vec.v_count = user_vec;
	io.si_user.ov_buf = (void **)read_bufs;

	io.si_stob.iv_vec.v_nr = i;
	io.si_stob.iv_vec.v_count = user_vec;
	io.si_stob.iv_index = stob_vec;

	m0_clink_init(&clink, NULL);
	m0_clink_add_lock(&io.si_wait, &clink);

	result = m0_stob_io_launch(&io, obj_fore, &tx, NULL);
	M0_ASSERT(result == 0);

	m0_chan_wait(&clink);

	M0_ASSERT(io.si_rc == 0);
	M0_ASSERT(io.si_count == (buf_size * i) >> block_shift);

	m0_clink_del_lock(&clink);
	m0_clink_fini(&clink);

	m0_stob_io_fini(&io);
}

static void test_ad_rw_unordered()
{
	int result;
	int i;

	m0_dtx_init(&tx, db->bs_domain, sm_grp);
	dom_fore->sd_ops->sdo_write_credit(dom_fore, NR, &tx.tx_betx_cred);
	result = dom_fore->sd_ops->sdo_tx_make(dom_fore, &tx);
	M0_ASSERT(result == 0);
	M0_ASSERT(m0_be_tx_state(&tx.tx_betx) == M0_BTS_ACTIVE);

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
		M0_ASSERT(memcmp(user_buf[i], read_buf[i], buf_size) == 0);

	result = m0_dtx_done_sync(&tx);
	M0_ASSERT(result == 0);
	m0_dtx_fini(&tx);
}

/**
   AD unit-test.
 */
static void test_ad(void)
{
	int i;
	int result;

	m0_dtx_init(&tx, db->bs_domain, sm_grp);
	dom_fore->sd_ops->sdo_write_credit(dom_fore, (NR * NR / 2),
						&tx.tx_betx_cred);
	result = dom_fore->sd_ops->sdo_tx_make(dom_fore, &tx);
	M0_ASSERT(result == 0);
	M0_ASSERT(m0_be_tx_state(&tx.tx_betx) == M0_BTS_ACTIVE);

	for (i = 1; i < NR; ++i)
		test_write(i);

	for (i = 1; i < NR; ++i) {
		int j;
		test_read(i);
		for (j = 0; j < i; ++j)
			M0_ASSERT(memcmp(user_buf[j], read_buf[j], buf_size) == 0);
	}
	result = m0_dtx_done_sync(&tx);
	M0_ASSERT(result == 0);
	m0_dtx_fini(&tx);
}

static void test_ad_undo(void)
{
	int                     result;
	struct m0_fol_rec_part *rpart;

	m0_dtx_init(&tx, db->bs_domain, sm_grp);
	dom_fore->sd_ops->sdo_write_credit(dom_fore, 2, &tx.tx_betx_cred);
	result = dom_fore->sd_ops->sdo_tx_make(dom_fore, &tx);
	M0_UT_ASSERT(result == 0);
	M0_ASSERT(m0_be_tx_state(&tx.tx_betx) == M0_BTS_ACTIVE);

	memset(user_buf[0], 'a', buf_size);
	test_write(1);

	test_read(1);

	M0_ASSERT(memcmp(user_buf[0], read_bufs[0], buf_size) == 0);

	rpart = m0_rec_part_tlist_head(&tx.tx_fol_rec.fr_parts);
	M0_ASSERT(rpart != NULL);

	/* Write new data in stob */
	memset(user_buf[0], 'b', buf_size);
	test_write(1);

	/* Do the undo operation. */
	result = rpart->rp_ops->rpo_undo(rpart, &tx.tx_betx);
	M0_UT_ASSERT(result == 0);

	test_read(1);

	result = m0_dtx_done_sync(&tx);
	M0_ASSERT(m0_be_tx_state(&tx.tx_betx) == M0_BTS_DONE);
	m0_dtx_fini(&tx);

	M0_ASSERT(memcmp(user_buf[0], read_bufs[0], buf_size) != 0);

}

const struct m0_test_suite ad_ut = {
	.ts_name = "ad-ut",
	.ts_init = test_ad_init,
	.ts_fini = test_ad_fini,
	.ts_tests = {
		{ "ad", test_ad },
		{ "ad-rw-unordered", test_ad_rw_unordered },
		{ "ad-undo", test_ad_undo},
		{ NULL, NULL }
	}
};

static void ub_write(int i)
{
	test_write(NR - 1);
}

static void ub_read(int i)
{
	test_read(NR - 1);
}

static int ub_init(const char *opts M0_UNUSED)
{
	return test_ad_init();
}

static void ub_fini(void)
{
	(void)test_ad_fini();
}

enum { UB_ITER = 100 };

struct m0_ub_set m0_ad_ub = {
	.us_name = "ad-ub",
	.us_init = ub_init,
	.us_fini = ub_fini,
	.us_run  = {
		{ .ub_name = "write-prime",
		  .ub_iter = 1,
		  .ub_round = ub_write },

		{ .ub_name = "write",
		  .ub_iter = UB_ITER,
		  .ub_round = ub_write },

		{ .ub_name = "read",
		  .ub_iter = UB_ITER,
		  .ub_round = ub_read },

		{ .ub_name = NULL }
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
