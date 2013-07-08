/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Dave Cohrs <dave_cohrs@xyratex.com>
 * Original creation date: 11/29/2012
 */

/* This file is designed to be included by addb/ut/addb_ut.c */

/*
 ****************************************************************************
 * Test ADDB stob sink interfaces.
 ****************************************************************************
 */

#include <stdio.h>
#include <stdlib.h>    /* system */

#include "stob/linux.h"

static const struct m0_stob_id stobsink_stobid = {
	.si_bits = { .u_hi = 1, .u_lo = 0xaddb570b }
};

enum {
	/* Small segment is OK for UT, real code uses large segments */
	STOBSINK_SEGMENT_SIZE = 8 * 1024,
	STOBSINK_TIMEOUT_SEC = 10 * 60,
	STOBSINK_MIN_SEARCH_SEG_NR = 16,
	STOBSINK_SMALL_SEG_NR = 10,
	STOBSINK_REC_SEQ_NR = 5,
	STOBSINK_TOO_MANY_REC = 10000,
	/* Number of events written to the "full" stob */
	STOBSINK_FULL_EVENT_NR = 406,
};

struct stobsink_formula {
	int32_t ssp_nr;      /* negative for offset from (max + 2) */
	int32_t ssp_rot;     /* negative for left rotation */
	int32_t ssp_exp_off; /* expected resulting segment offset segment */
};
static const struct stobsink_formula stobsink_search_params[] = {
	{ .ssp_nr =  0, .ssp_rot =  0, .ssp_exp_off =  0, },
	{ .ssp_nr =  1, .ssp_rot =  0, .ssp_exp_off =  1, },
	{ .ssp_nr =  8, .ssp_rot =  0, .ssp_exp_off =  8, },
	{ .ssp_nr =  9, .ssp_rot =  0, .ssp_exp_off =  9, },
	{ .ssp_nr = -3, .ssp_rot =  0, .ssp_exp_off = -1, },
	{ .ssp_nr = -2, .ssp_rot =  0, .ssp_exp_off =  0, },
	{ .ssp_nr = -2, .ssp_rot = -2, .ssp_exp_off =  2, },
	{ .ssp_nr = -2, .ssp_rot = -1, .ssp_exp_off =  1, },
	{ .ssp_nr = -2, .ssp_rot =  8, .ssp_exp_off = -8, },
	{ .ssp_nr = -2, .ssp_rot =  7, .ssp_exp_off = -7, },
	{ .ssp_nr = -2, .ssp_rot =  2, .ssp_exp_off = -2, },
	{ .ssp_nr = -2, .ssp_rot =  1, .ssp_exp_off = -1, },
};
static int stobsink_search_idx;
static size_t stobsink_seg_nr;

static const struct m0_stob_op stobsink_mock_stob_op;
static const struct m0_stob_domain_op stobsink_mock_stob_domain_op;
static const struct m0_stob_io_op stobsink_mock_stob_io_op;

struct stobsink_domain {
	struct m0_stob_domain ssd_dom;
	char                  ssd_buf[16];
};

static const char addb_repofile[] = "./_addb/repofile.dat";

static int stobsink_mock_stob_type_init(struct m0_stob_type *stype)
{
	m0_stob_type_init(stype);
	return 0;
}

static void stobsink_mock_stob_type_fini(struct m0_stob_type *stype)
{
	m0_stob_type_fini(stype);
}

static int stobsink_mock_stob_type_domain_locate(struct m0_stob_type *type,
						 const char *domain_name,
						 struct m0_stob_domain **out,
						 uint64_t dom_id)
{
	struct stobsink_domain *sdom;
	struct m0_stob_domain  *dom;
	int                     rc;

	M0_UT_ASSERT(strlen(domain_name) < sizeof sdom->ssd_buf);
	sdom = m0_alloc(sizeof *sdom);
	if (sdom != NULL) {
		dom = &sdom->ssd_dom;
		dom->sd_ops = &stobsink_mock_stob_domain_op;
		m0_stob_domain_init(dom, type, dom_id);
		*out = dom;
		strcpy(sdom->ssd_buf, domain_name);
		dom->sd_name = sdom->ssd_buf;
		rc = 0;
	} else {
		rc = -ENOMEM;
	}
	return rc;
}

static void stobsink_mock_domain_fini(struct m0_stob_domain *self)
{
	struct stobsink_domain *sdom =
	    container_of(self, struct stobsink_domain, ssd_dom);
	m0_stob_domain_fini(self);
	m0_free(sdom);
}

static int stobsink_mock_domain_stob_find(struct m0_stob_domain *dom,
					  const struct m0_stob_id *id,
					  struct m0_stob **out)
{
	struct m0_stob *stob;
	int             rc;

	M0_ALLOC_PTR(stob);
	if (stob != NULL) {
		stob->so_op = &stobsink_mock_stob_op;
		m0_stob_init(stob, id, dom);
		*out = stob;
		rc = 0;
	} else {
		rc = -ENOMEM;
	}
	return rc;
}

static int stobsink_mock_domain_tx_make(struct m0_stob_domain *dom,
					struct m0_dtx *tx)
{
	return 0;
}

static void stobsink_mock_stob_fini(struct m0_stob *stob)
{
	m0_stob_fini(stob);
	m0_free(stob);
}

static int stobsink_mock_stob_create(struct m0_stob *obj, struct m0_dtx *tx)
{
	return 0;
}

static int stobsink_mock_stob_locate(struct m0_stob *obj, struct m0_dtx *tx)
{
	return 0;
}

int stobsink_mock_stob_io_init(struct m0_stob *stob, struct m0_stob_io *io)
{
	M0_UT_ASSERT(io->si_state == SIS_IDLE);
	io->si_op = &stobsink_mock_stob_io_op;
	return 0;
}

uint32_t stobsink_mock_stob_block_shift(const struct m0_stob *stob)
{
	return 0;
}

static void stobsink_mock_stob_io_fini(struct m0_stob_io *io)
{
}

/*
 * Mock launch that supports reading segment headers.  The contents of
 * the header is set based on the current stobsink_formula and the
 * requested offset.
 */
static int stobsink_mock_stob_io_launch(struct m0_stob_io *io)
{
	const struct stobsink_formula *params;
	uint64_t                       seg_nr;
	int                            nr;
	uint64_t                       ret_seq_nr;
	uint32_t                       bshift;
	void                          *addr;
	m0_bcount_t                    count;
	struct m0_bufvec               out = M0_BUFVEC_INIT_BUF(&addr, &count);
	struct m0_bufvec_cursor        cur;
	struct m0_addb_seg_header      head;

	if (io->si_opcode != SIO_READ)
		return -ENOSYS;
	M0_UT_ASSERT(io->si_stob.iv_vec.v_nr == 1);
	M0_UT_ASSERT(io->si_user.ov_vec.v_nr == 1);
	bshift = stobsink_mock_stob_block_shift(io->si_obj);
	seg_nr = io->si_stob.iv_index[0] << bshift;
	M0_UT_ASSERT(seg_nr % STOBSINK_SEGMENT_SIZE == 0);
	seg_nr /= STOBSINK_SEGMENT_SIZE;

	params = &stobsink_search_params[stobsink_search_idx];
	ret_seq_nr = (((int) seg_nr + params->ssp_rot + stobsink_seg_nr) %
		      stobsink_seg_nr) + 1;
	nr = params->ssp_nr;
	if (nr < 0)
		nr += stobsink_seg_nr + 2;
	if (ret_seq_nr > nr || seg_nr >= stobsink_seg_nr) {
		head.sh_seq_nr = 0;
		head.sh_ver_nr = 0;
		head.sh_segsize = 0;
	} else {
		head.sh_seq_nr = ret_seq_nr;
		head.sh_ver_nr = STOBSINK_XCODE_VER_NR;
		head.sh_segsize = STOBSINK_SEGMENT_SIZE;
	}

	addr = m0_stob_addr_open(io->si_user.ov_buf[0], bshift);
	count = io->si_user.ov_vec.v_count[0] << bshift;
	m0_bufvec_cursor_init(&cur, &out);
	io->si_rc = stobsink_header_encdec(&head, &cur, M0_XCODE_ENCODE);
	M0_UT_ASSERT(io->si_rc == 0);

	if (nr == 0)
		io->si_count = 0;
	else
		io->si_count = io->si_stob.iv_vec.v_count[0];
	io->si_state = SIS_IDLE;
	m0_chan_broadcast_lock(&io->si_wait);

	return 0;
}

static const struct m0_stob_type_op stobsink_mock_stob_type_op = {
	.sto_init          = stobsink_mock_stob_type_init,
	.sto_fini          = stobsink_mock_stob_type_fini,
	.sto_domain_locate = stobsink_mock_stob_type_domain_locate,
};

static const struct m0_stob_domain_op stobsink_mock_stob_domain_op = {
	.sdo_fini        = stobsink_mock_domain_fini,
	.sdo_stob_find   = stobsink_mock_domain_stob_find,
	.sdo_tx_make     = stobsink_mock_domain_tx_make,
};

static const struct m0_stob_op stobsink_mock_stob_op = {
	.sop_fini         = stobsink_mock_stob_fini,
	.sop_create       = stobsink_mock_stob_create,
	.sop_locate       = stobsink_mock_stob_locate,
	.sop_io_init      = stobsink_mock_stob_io_init,
	.sop_block_shift  = stobsink_mock_stob_block_shift,
};

static const struct m0_stob_io_op stobsink_mock_stob_io_op = {
	.sio_fini    = stobsink_mock_stob_io_fini,
	.sio_launch  = stobsink_mock_stob_io_launch,
};

static struct m0_stob_type stobsink_mock_stob_type = {
	.st_op    = &stobsink_mock_stob_type_op,
	.st_name  = "mockstob",
	.st_magic = 0xFA15E,
};

static int stobsink_mock_stobs_init(void)
{
	return M0_STOB_TYPE_OP(&stobsink_mock_stob_type, sto_init);
}

static void stobsink_mock_stobs_fini(void)
{
	M0_STOB_TYPE_OP(&stobsink_mock_stob_type, sto_fini);
}

static void addb_ut_stobsink_search(void)
{
	struct m0_stob_domain    *dom;
	struct m0_stob           *stob;
	struct m0_addb_mc         mc;
	struct stobsink          *sink;
	struct m0_addb_seg_header head;
	m0_bcount_t               stob_size;
	m0_time_t                 timeout = M0_MKTIME(STOBSINK_TIMEOUT_SEC, 0);
	int                       sz = STOBSINK_MIN_SEARCH_SEG_NR;
	int                       rc;
	int                       i;
	int64_t                   expected_nr;
	int64_t                   expected_offset;

	rc = stobsink_mock_stobs_init();
	M0_UT_ASSERT(rc == 0);

	m0_addb_mc_init(&mc);

	/* must jump thru hoops to make mock stob usable */
	rc = m0_stob_domain_locate(&stobsink_mock_stob_type, "mock", &dom,
				   0);
	M0_UT_ASSERT(rc == 0);
	rc = m0_stob_find(dom, &stobsink_stobid, &stob);
	M0_UT_ASSERT(rc == 0 && stob != NULL);
	M0_UT_ASSERT(stob->so_state == CSS_UNKNOWN);
	rc = m0_stob_create(stob, NULL);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(stob->so_state == CSS_EXISTS);

	/* Test: full configure path, exercises EOF handling. */
	stobsink_search_idx = 0;
	stobsink_seg_nr = sz;
	stob_size = STOBSINK_SEGMENT_SIZE * sz + 1; /* +1: check seg rounding */
	rc = m0_addb_mc_configure_stob_sink(&mc, stob, STOBSINK_SEGMENT_SIZE,
					    stob_size, timeout);
	M0_UT_ASSERT(rc == 0);
	sink = stobsink_from_mc(&mc);
	M0_UT_ASSERT(sink->ss_segsize == STOBSINK_SEGMENT_SIZE);
	M0_UT_ASSERT(sink->ss_bshift == stobsink_mock_stob_block_shift(stob));
	M0_UT_ASSERT(sink->ss_seg_nr == sz);
	M0_UT_ASSERT(sink->ss_timeout == timeout);
	M0_UT_ASSERT(sink->ss_stob == stob);
	/* Test: verifies rs_get work correctly */
	M0_UT_ASSERT(m0_atomic64_get(&stob->so_ref) == 2);
	M0_UT_ASSERT(sink->ss_seq_nr == 1);
	M0_UT_ASSERT(sink->ss_offset == 0);

	/* Test: search must handle various mock partial & full stobs */
	sink->ss_sync = true;
	for (; sz <= STOBSINK_MIN_SEARCH_SEG_NR + 1; ++sz) {
		stob_size = STOBSINK_SEGMENT_SIZE * sz;
		sink->ss_seg_nr = stob_size / STOBSINK_SEGMENT_SIZE;
		stobsink_seg_nr = sz;
		for (i = 0; i < ARRAY_SIZE(stobsink_search_params); ++i) {
			stobsink_search_idx = i;
			sink->ss_seq_nr = 0;
			sink->ss_offset = 0;
			head.sh_seq_nr = stobsink_seq_at(sink, 0);
			stobsink_offset_search(sink, head.sh_seq_nr);
			expected_nr = stobsink_search_params[i].ssp_nr + 1;
			if (expected_nr < 0)
				expected_nr += (int) sink->ss_seg_nr + 2;
			expected_offset = stobsink_search_params[i].ssp_exp_off;
			if (expected_offset < 0)
				expected_offset += (int) sink->ss_seg_nr;
			expected_offset *= STOBSINK_SEGMENT_SIZE;
			M0_UT_ASSERT(sink->ss_seq_nr == expected_nr);
			M0_UT_ASSERT(sink->ss_offset == expected_offset);
		}
	}
	sink->ss_sync = false;

	/* Test: verifies rs_put work correctly */
	m0_addb_mc_fini(&mc);
	M0_UT_ASSERT(m0_atomic64_get(&stob->so_ref) == 1);

	m0_stob_put(stob);
	dom->sd_ops->sdo_fini(dom);
	stobsink_mock_stobs_fini();
}


/*
 * Verify all segments are valid and can decode all records.
 * Perform mimimal validation of record data.
 */
static void addb_ut_stobsink_verify(struct stobsink *sink)
{
	struct stobsink_poolbuf   *pb;
	struct m0_stob_domain     *dom;
	struct m0_bufvec_cursor    cur;
	struct m0_addb_seg_header  head;
	struct m0_addb_seg_trailer trl;
	struct m0_addb_rec        *r;
	struct m0_xcode_obj        obj = {
		.xo_type = m0_addb_rec_xc
	};
	struct m0_addb_rec_type   *dp = &m0__addb_ut_rt_dp9;
	uint64_t                   dp_rid;
	uint32_t                   bshift;
	m0_bcount_t                trl_offset;
	bool                       eob;
	uint64_t                   u;
	int                        ctx_nr = 0;
	int                        rc;
	int                        i;
	int                        j;
	int                        count = 0;

	pb = sink->ss_current;
	M0_UT_ASSERT(!pb->spb_busy);
	M0_UT_ASSERT(sink->ss_segsize == STOBSINK_SEGMENT_SIZE);
	M0_UT_ASSERT(sink->ss_record_nr == 0);

	dp_rid = m0_addb_rec_rid_make(dp->art_base_type, dp->art_id);
	trl_offset = sink->ss_segsize - sizeof trl;
	dom = sink->ss_stob->so_domain;
	bshift = sink->ss_bshift;
	pb->spb_io.si_opcode = SIO_READ;
	sink->ss_sync = true;

	for (i = 0; i < sink->ss_seg_nr; ++i) {
		m0_dtx_init(&pb->spb_tx);
		rc = dom->sd_ops->sdo_tx_make(dom, &pb->spb_tx);
		M0_UT_ASSERT(rc == 0);
		if (rc != 0)
			break;

		pb->spb_io_iv_index = (i * STOBSINK_SEGMENT_SIZE) >> bshift;
		pb->spb_io.si_obj = NULL;
		pb->spb_io.si_rc = 0;
		pb->spb_io.si_count = 0;
		pb->spb_busy = true;
		/*
		 * There is no simultaneous writer during this UT, so UT can
		 * read the whole segment at once here, unlike the seek/seek
		 * approach required in a general retrieval mechanism.
		 */
		rc = m0_stob_io_launch(&pb->spb_io,
				       sink->ss_stob, &pb->spb_tx, NULL);
		M0_UT_ASSERT(rc == 0);
		if (rc != 0) {
			pb->spb_busy = false;
			m0_dtx_done(&pb->spb_tx);
			break;
		}
		while (pb->spb_busy)
			m0_chan_wait(&pb->spb_wait);
		rc = pb->spb_io.si_rc;
		M0_UT_ASSERT(rc == 0);
		M0_UT_ASSERT(pb->spb_io.si_count == pb->spb_io_v_count);
		if (rc != 0 || pb->spb_io.si_count != pb->spb_io_v_count)
			break;

		m0_bufvec_cursor_init(&cur, &pb->spb_buf);
		eob = m0_bufvec_cursor_move(&cur, trl_offset);
		M0_UT_ASSERT(!eob);
		rc = stobsink_trailer_encdec(&trl, &cur, M0_XCODE_DECODE);
		M0_UT_ASSERT(rc == 0);

		m0_bufvec_cursor_init(&cur, &pb->spb_buf);
		rc = stobsink_header_encdec(&head, &cur, M0_XCODE_DECODE);
		M0_UT_ASSERT(rc == 0); /* fail iff short buffer */
		M0_UT_ASSERT(head.sh_seq_nr != 0);
		M0_UT_ASSERT(head.sh_ver_nr == STOBSINK_XCODE_VER_NR);
		M0_UT_ASSERT(head.sh_segsize == sink->ss_segsize);
		M0_UT_ASSERT(head.sh_seq_nr == trl.st_seq_nr);
		M0_UT_ASSERT(trl.st_rec_nr > 0);

		for (j = 0; j < trl.st_rec_nr; ++j) {
			r = NULL;
			rc = addb_rec_encdec(&r, &cur, M0_XCODE_DECODE);
			M0_UT_ASSERT(rc == 0);
			M0_UT_ASSERT(r != NULL);
			if (m0_addb_rec_is_ctx(r)) {
				ctx_nr++;
				obj.xo_ptr = r;
				m0_xcode_free(&obj);
				continue;
			}
			M0_UT_ASSERT(r->ar_rid == dp_rid);
			M0_UT_ASSERT(r->ar_ctxids.acis_nr == 3);
			M0_UT_ASSERT(r->ar_data.au64s_nr == 9); /* dp9 */
			if (j == 0) {
				u = r->ar_data.au64s_data[0];
			} else {
				++u;
				M0_UT_ASSERT(u == r->ar_data.au64s_data[0]);
			}
			count++;
			obj.xo_ptr = r;
			m0_xcode_free(&obj);
		}
	}
	M0_UT_ASSERT(ctx_nr == 1);
	M0_UT_ASSERT(count == STOBSINK_FULL_EVENT_NR);

	pb->spb_io.si_opcode = SIO_WRITE;
	M0_UT_ASSERT(!pb->spb_busy);
	sink->ss_sync = false;
}

static void addb_ut_cursor(struct m0_addb_segment_iter *iter,
			   uint32_t flags, int expected)
{
	struct m0_addb_rec      *rec;
	struct m0_addb_cursor    cur;
	struct m0_addb_rec_type *dp = &m0__addb_ut_rt_dp9;
	uint64_t                 dp_rid;
	uint64_t                 u = 0;
	int                      count = 0;
	int                      rc;

	dp_rid = m0_addb_rec_rid_make(dp->art_base_type, dp->art_id);
	rc = m0_addb_cursor_init(&cur, iter, flags);
	M0_UT_ASSERT(rc == 0);
	while (1) {
		rc = m0_addb_cursor_next(&cur, &rec);
		if (rc == -ENODATA)
			break;
		M0_UT_ASSERT(rc == 0);
		M0_UT_ASSERT(rec != NULL);
		if (flags == M0_ADDB_CURSOR_REC_EVENT) {
			M0_UT_ASSERT(m0_addb_rec_is_event(rec));
			M0_UT_ASSERT(rec->ar_rid == dp_rid);
			M0_UT_ASSERT(rec->ar_ctxids.acis_nr == 3);
			M0_UT_ASSERT(rec->ar_data.au64s_nr == 9); /* dp9 */
			M0_UT_ASSERT(u != rec->ar_data.au64s_data[0]);
			u = rec->ar_data.au64s_data[0];
		} else if (flags == M0_ADDB_CURSOR_REC_CTX)
			M0_UT_ASSERT(m0_addb_rec_is_ctx(rec));
		count++;
	}
	m0_addb_cursor_fini(&cur);
	M0_UT_ASSERT(count == expected);
}

static void addb_ut_stob_cursor(struct m0_stob *stob,
				uint32_t flags, int expected)
{
	struct m0_addb_segment_iter *iter;
	int                          rc;

	rc = m0_addb_stob_iter_alloc(&iter, stob);
	M0_UT_ASSERT(rc == 0);
	addb_ut_cursor(iter, flags, expected);
	m0_addb_segment_iter_free(iter);
}

static void addb_ut_file_cursor(uint32_t flags, int expected)
{
	struct m0_addb_segment_iter *iter;
	int                          rc;

	rc = m0_addb_file_iter_alloc(&iter, addb_repofile);
	M0_UT_ASSERT(rc == 0);
	addb_ut_cursor(iter, flags, expected);
	m0_addb_segment_iter_free(iter);
}
static struct m0_stob *addb_ut_retrieval_stob_setup(const char *domain_name,
						    const struct m0_stob_id *id)
{
	struct m0_stob_domain *dom;
	struct m0_stob        *stob = NULL;
	int                    rc;

	M0_UT_ASSERT(id != NULL);
	rc = m0_linux_stob_domain_locate(domain_name, &dom);
	M0_UT_ASSERT(rc == 0);
	rc = m0_stob_find(dom, id, &stob);
	M0_UT_ASSERT(rc == 0 && stob != NULL);
	M0_UT_ASSERT(stob->so_state == CSS_UNKNOWN);
	rc = m0_stob_create(stob, NULL);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(stob->so_state == CSS_EXISTS);

	return stob;
}

static void addb_ut_retrieval(void)
{
	struct m0_stob              *stob;
	struct m0_addb_segment_iter *iter;
	struct m0_stob_domain       *dom;
	struct addb_segment_iter    *ai;
	struct m0_bufvec_cursor      cur;
	const struct m0_bufvec      *bv;
	FILE                        *f;
	int                          count;
	int                          rc;

	stob = addb_ut_retrieval_stob_setup("./_addb", &stobsink_stobid);
	M0_UT_ASSERT(stob != NULL);
	dom = stob->so_domain;

	/* Test: segment size written to stob is retreived */
	rc = stob_retrieval_segsize_get(stob);
	M0_UT_ASSERT(rc == STOBSINK_SEGMENT_SIZE);

	/* Test: all written segments are retrieved via asi_next */
	rc = m0_addb_stob_iter_alloc(&iter, stob);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(iter->asi_next == stob_segment_iter_next);
	count = 0;
	while (1) {
		rc = iter->asi_next(iter, &cur);
		if (rc == 0)
			break;
		M0_UT_ASSERT(rc > 0);
		M0_UT_ASSERT(cur.bc_vc.vc_seg == 0 &&
			     cur.bc_vc.vc_offset ==
			     sizeof(struct m0_addb_seg_header));
		count++;
	}
	m0_addb_segment_iter_free(iter);
	M0_UT_ASSERT(count == STOBSINK_SMALL_SEG_NR);

	/* Test: all written segments are retrieved via asi_nextbuf */
	rc = m0_addb_stob_iter_alloc(&iter, stob);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(iter->asi_nextbuf == addb_segment_iter_nextbuf);
	M0_UT_ASSERT(iter->asi_seq_get(iter) == 0);
	/* save data in a file, for file iter tests */
	f = fopen(addb_repofile, "w");
	M0_UT_ASSERT(f != NULL);
	count = 0;
	while (1) {
		rc = iter->asi_nextbuf(iter, &bv);
		if (rc == -ENODATA)
			break;
		M0_UT_ASSERT(rc == 0);
		M0_UT_ASSERT(bv->ov_vec.v_nr == 1);
		M0_UT_ASSERT(bv->ov_vec.v_count[0] == STOBSINK_SEGMENT_SIZE);
		M0_UT_ASSERT(iter->asi_seq_get(iter) ==
			     (count < 3 ? count + 12 : count + 2));
		rc = fwrite(bv->ov_buf[0], bv->ov_vec.v_count[0], 1, f);
		M0_UT_ASSERT(rc == 1);
		count++;
	}
	M0_UT_ASSERT(iter->asi_seq_get(iter) == 0);
	fclose(f);
	m0_addb_segment_iter_free(iter);
	M0_UT_ASSERT(count == STOBSINK_SMALL_SEG_NR);

	/* Test: use of asi_seq_set skips segments that are too low */
	rc = m0_addb_stob_iter_alloc(&iter, stob);
	M0_UT_ASSERT(rc == 0);
	iter->asi_seq_set(iter, STOBSINK_SMALL_SEG_NR - 3);
	count = 0;
	while (1) {
		rc = iter->asi_nextbuf(iter, &bv);
		if (rc == -ENODATA)
			break;
		M0_UT_ASSERT(rc == 0);
		M0_UT_ASSERT(iter->asi_seq_get(iter) >=
			     STOBSINK_SMALL_SEG_NR - 3);
		count++;
	}
	m0_addb_segment_iter_free(iter);
	M0_UT_ASSERT(count == STOBSINK_SMALL_SEG_NR - 2);

	/* Test: event retrieval retrieves all/only event records */
	addb_ut_stob_cursor(stob,
			    M0_ADDB_CURSOR_REC_EVENT, STOBSINK_FULL_EVENT_NR);
	addb_ut_stob_cursor(stob, M0_ADDB_CURSOR_REC_CTX, 1);
	addb_ut_stob_cursor(stob, 0, STOBSINK_FULL_EVENT_NR + 1);

	M0_UT_ASSERT(m0_atomic64_get(&stob->so_ref) == 2);
	m0_stob_put(stob);
	dom->sd_ops->sdo_fini(dom);

	/* Test: file iter alloc correctly determines segment size */
	rc = m0_addb_file_iter_alloc(&iter, addb_repofile);
	M0_UT_ASSERT(rc == 0);
	ai = container_of(iter, struct addb_segment_iter, asi_base);
	M0_UT_ASSERT(ai->asi_magic == M0_ADDB_FILERET_MAGIC);
	M0_UT_ASSERT(ai->asi_segsize == STOBSINK_SEGMENT_SIZE);

	/* Test: all segments in file are retrieved via asi_next */
	M0_UT_ASSERT(iter->asi_next == file_segment_iter_next);
	count = 0;
	while (1) {
		rc = iter->asi_next(iter, &cur);
		if (rc == 0)
			break;
		M0_UT_ASSERT(rc > 0);
		M0_UT_ASSERT(cur.bc_vc.vc_seg == 0 &&
			     cur.bc_vc.vc_offset ==
			     sizeof(struct m0_addb_seg_header));
		count++;
	}
	m0_addb_segment_iter_free(iter);
	M0_UT_ASSERT(count == STOBSINK_SMALL_SEG_NR);

	/* Test: all segments in file are retrieved via asi_nextbuf */
	rc = m0_addb_file_iter_alloc(&iter, addb_repofile);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(iter->asi_nextbuf == addb_segment_iter_nextbuf);
	M0_UT_ASSERT(iter->asi_seq_get(iter) == 0);
	count = 0;
	while (1) {
		rc = iter->asi_nextbuf(iter, &bv);
		if (rc == -ENODATA)
			break;
		M0_UT_ASSERT(rc == 0);
		M0_UT_ASSERT(bv->ov_vec.v_nr == 1);
		M0_UT_ASSERT(bv->ov_vec.v_count[0] == STOBSINK_SEGMENT_SIZE);
		M0_UT_ASSERT(iter->asi_seq_get(iter) ==
			     (count < 3 ? count + 12 : count + 2));
		count++;
	}
	M0_UT_ASSERT(iter->asi_seq_get(iter) == 0);
	m0_addb_segment_iter_free(iter);
	M0_UT_ASSERT(count == STOBSINK_SMALL_SEG_NR);

	/* Test: use of asi_seq_set skips segments that are too low */
	rc = m0_addb_file_iter_alloc(&iter, addb_repofile);
	M0_UT_ASSERT(rc == 0);
	iter->asi_seq_set(iter, STOBSINK_SMALL_SEG_NR - 3);
	count = 0;
	while (1) {
		rc = iter->asi_nextbuf(iter, &bv);
		if (rc == -ENODATA)
			break;
		M0_UT_ASSERT(rc == 0);
		M0_UT_ASSERT(iter->asi_seq_get(iter) >=
			     STOBSINK_SMALL_SEG_NR - 3);
		count++;
	}
	m0_addb_segment_iter_free(iter);
	M0_UT_ASSERT(count == STOBSINK_SMALL_SEG_NR - 2);

	/* Test: event retrieval retrieves all/only event records from file */
	addb_ut_file_cursor(M0_ADDB_CURSOR_REC_EVENT, STOBSINK_FULL_EVENT_NR);
	addb_ut_file_cursor(M0_ADDB_CURSOR_REC_CTX, 1);
	addb_ut_file_cursor(0, STOBSINK_FULL_EVENT_NR + 1);
}

static void addb_ut_stob(void)
{
	struct m0_stob_domain    *dom;
	struct m0_stob           *stob;
	struct m0_addb_mc         mc;
	struct stobsink          *sink;
	struct stobsink_poolbuf  *current;
	struct stobsink_poolbuf  *next;
	m0_bcount_t               stob_size;
	m0_time_t                 timeout = M0_MKTIME(STOBSINK_TIMEOUT_SEC, 0);
	int                       rc;
	int                       i;
	int                       j;
	struct m0_addb_ctx        ctx;
	struct m0_addb_ctx *cv[4] = {
		NULL, &m0_addb_proc_ctx, &m0_addb_node_ctx, NULL
	};
	struct m0_addb_rec_type  *dp = &m0__addb_ut_rt_dp9;
	struct m0_addb_rec_seq    rs = { .ars_nr = STOBSINK_REC_SEQ_NR };
	struct m0_addb_rec       *r;
	size_t                    ctxid_seq_data_size;
	size_t                    bytes_nr;
	struct m0_bufvec          bv;
	struct m0_bufvec_cursor   cur;
	m0_bindex_t               offset;
	m0_bcount_t               len;
	m0_time_t                 when;
	uint32_t                  rec_nr;
	uint64_t                  seq;
	uint64_t                  u = 1;
	void                     *vp;
	bool                      stob_wrapped;
	bool                      bp_wrapped;

	/* Skip rec_post UT hooks, only for validation of posting rec's data */
	addb_rec_post_ut_data_enabled = false;
	rc = system("rm -fr ./_addb");
	M0_UT_ASSERT(rc == 0);
	rc = mkdir("./_addb", 0700);
	M0_UT_ASSERT(rc == 0 || (rc == -1 && errno == EEXIST));
	rc = mkdir("./_addb/o", 0700);
	M0_UT_ASSERT(rc == 0 || (rc == -1 && errno == EEXIST));

	rc = m0_linux_stob_domain_locate("./_addb", &dom);
	M0_UT_ASSERT(rc == 0);
	rc = m0_stob_find(dom, &stobsink_stobid, &stob);
	M0_UT_ASSERT(rc == 0 && stob != NULL);
	M0_UT_ASSERT(stob->so_state == CSS_UNKNOWN);
	rc = m0_stob_create(stob, NULL);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(stob->so_state == CSS_EXISTS);

	m0_addb_mc_init(&mc);
	stob_size = STOBSINK_SEGMENT_SIZE * STOBSINK_SMALL_SEG_NR;
	rc = m0_addb_mc_configure_stob_sink(&mc, stob, STOBSINK_SEGMENT_SIZE,
					    stob_size, timeout);
	M0_UT_ASSERT(rc == 0);
	sink = stobsink_from_mc(&mc);
	M0_UT_ASSERT(sink->ss_seq_nr == 1);
	M0_UT_ASSERT(sink->ss_offset == 0);

	/* re-use addb_ut_md.c fake context type and datapoint */
	m0__addb_ut_ct0.act_magic = 0;
	m0__addb_ut_ct0.act_id = addb_ct_max_id + 1;
	m0_addb_ctx_type_register(&m0__addb_ut_ct0);
	dp->art_magic = 0;
	dp->art_id = addb_rt_max_id + 1;
	m0_addb_rec_type_register(dp);

	m0_addb_mc_configure_pt_evmgr(&mc);

	/* Test: CTX init writes a record */
	M0_UT_ASSERT(sink->ss_record_nr == 0);
	M0_ADDB_CTX_INIT(&mc, &ctx, &m0__addb_ut_ct0, &m0_addb_proc_ctx);
	cv[0] = &ctx;
	M0_UT_ASSERT(sink->ss_record_nr == 1);

	/* Test: posting writes a record */
	M0_ADDB_POST(&mc, dp, cv, 9, 8, 7, 6, 5, 4, 3, 2, 1);
	M0_UT_ASSERT(sink->ss_record_nr == 2);

	m0_addb_mc_fini(&mc);
	M0_UT_ASSERT(m0_atomic64_get(&stob->so_ref) == 2);

	/* Test: re-open stob, detects 1 segment, sets seq and offset */
	m0_addb_mc_init(&mc);
	rc = m0_addb_mc_configure_stob_sink(&mc, stob, STOBSINK_SEGMENT_SIZE,
					    stob_size, timeout);
	M0_UT_ASSERT(rc == 0);
	sink = stobsink_from_mc(&mc);
	M0_UT_ASSERT(sink->ss_seq_nr == 2);
	M0_UT_ASSERT(sink->ss_offset == STOBSINK_SEGMENT_SIZE);
	m0_addb_mc_configure_pt_evmgr(&mc);

	/* Test: writing works in this segment */
	M0_UT_ASSERT(sink->ss_record_nr == 0);
	M0_ADDB_POST(&mc, dp, cv, u++, 2, 3, 4, 5, 6, 7, 8, 9);
	M0_UT_ASSERT(sink->ss_record_nr == 1);

	/* Test: timeout causes persist without advancing */
	M0_UT_ASSERT(sink->ss_record_nr > sink->ss_persist_nr);
	stobsink_skulk(&mc); /* no timeout yet */
	M0_UT_ASSERT(sink->ss_record_nr > sink->ss_persist_nr);
	sink->ss_persist_time -= sink->ss_timeout + M0_TIME_ONE_BILLION;
	offset = sink->ss_offset;
	when = m0_time_now();
	seq = sink->ss_seq_nr;
	M0_UT_ASSERT(sink->ss_persist_time <= when - sink->ss_timeout);
	M0_UT_ASSERT(sink->ss_record_nr > sink->ss_persist_nr);
	M0_ADDB_POST(&mc, dp, cv, u++, 2, 3, 4, 5, 6, 7, 8, 9);
	M0_UT_ASSERT(sink->ss_record_nr == 2);
	M0_UT_ASSERT(sink->ss_record_nr > sink->ss_persist_nr);
	stobsink_skulk(&mc);
	M0_UT_ASSERT(sink->ss_record_nr == sink->ss_persist_nr);
	M0_UT_ASSERT(offset == sink->ss_offset);
	M0_UT_ASSERT(sink->ss_persist_time >= when);
	M0_UT_ASSERT(sink->ss_seq_nr > seq);

	/* Test: buffer pool grows when next is still busy */
	current = sink->ss_current;
	next = stobsink_pool_tlist_next(&sink->ss_pool, sink->ss_current);
	M0_UT_ASSERT(next != NULL);
	M0_UT_ASSERT(!next->spb_busy);
	next->spb_busy = true;
	offset = sink->ss_offset;
	do {
		M0_ADDB_POST(&mc, dp, cv, u++, 2, 3, 4, 5, 6, 7, 8, 9);
	} while (offset == sink->ss_offset);
	M0_UT_ASSERT(sink->ss_current != current);
	M0_UT_ASSERT(sink->ss_current != next);
	M0_UT_ASSERT(sink->ss_record_nr == 1);
	M0_UT_ASSERT(next == stobsink_pool_tlist_next(&sink->ss_pool,
						      sink->ss_current));
	/*
	 * Test: save a sequence
	 * This requires populating a complete m0_addb_rec_seq, serializing
	 * it to a buffer, then posting that serialized sequence (just as
	 * the ADDB service will do).  The size is picked such that it will
	 * not cause the current segment to overflow.
	 * Note: record sequence cannot use the trick used by addb_rec_post()
	 * of allocating each record and its data in a single allocation.
	 * The records in the sequence are an array of m0_addb_rec.  The data
	 * of each record must be allocated separately (it could be done as
	 * one huge allocation, with all the data for all the records after
	 * the sequence of m0_addb_rec, but the data cannot be mixed in
	 * between the m0_addb_rec).
	 */
	M0_ALLOC_ARR(rs.ars_data, STOBSINK_REC_SEQ_NR);
	M0_UT_ASSERT(rs.ars_data != NULL);
	for (i = 0; i < STOBSINK_REC_SEQ_NR; ++i) {
		r = &rs.ars_data[i];
		r->ar_rid = m0_addb_rec_rid_make(dp->art_base_type, dp->art_id);
		r->ar_ts = m0_time_now();
		ctxid_seq_data_size = addb_ctxid_seq_data_size(cv);
		vp = m0_alloc(ctxid_seq_data_size);
		M0_UT_ASSERT(vp != NULL);
		r->ar_ctxids.acis_data = vp;
		r->ar_ctxids.acis_nr = 0;
		bytes_nr = addb_ctxid_seq_build(&r->ar_ctxids, cv);
		M0_UT_ASSERT(bytes_nr == ctxid_seq_data_size);
		M0_ALLOC_ARR(r->ar_data.au64s_data, dp->art_rf_nr);
		M0_UT_ASSERT(r->ar_data.au64s_data != NULL);
		r->ar_data.au64s_data[0] = u++;
		for (j = 1; j < dp->art_rf_nr; ++j)
			r->ar_data.au64s_data[j] = j;
	}
	rc = m0_bufvec_alloc(&bv, 1, sink->ss_segsize);
	M0_UT_ASSERT(rc == 0);
	m0_bufvec_cursor_init(&cur, &bv);
	rc = addb_rec_seq_enc(&rs, &cur, m0_addb_rec_seq_xc);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(cur.bc_vc.vc_seg == 0 && cur.bc_vc.vc_offset > 0);
	len = cur.bc_vc.vc_offset;
	m0_bufvec_cursor_init(&cur, &bv);
	rec_nr = sink->ss_record_nr;
	stobsink_save_seq(&mc, &cur, len);
	M0_UT_ASSERT(sink->ss_record_nr == rec_nr + STOBSINK_REC_SEQ_NR);
	m0_bufvec_free(&bv);
	for (i = 0; i < STOBSINK_REC_SEQ_NR; ++i) {
		r = &rs.ars_data[i];
		m0_free(r->ar_ctxids.acis_data);
		m0_free(r->ar_data.au64s_data);
	}
	m0_free(rs.ars_data);

	/* Test: pool growth is limited by max pool size */
	current = sink->ss_current;
	do {
		rc = stobsink_poolbuf_grow(sink);
		M0_UT_ASSERT(stobsink_pool_tlist_length(&sink->ss_pool) <=
			     STOBSINK_MAXPOOL_NR);
	} while (rc == 0);
	M0_UT_ASSERT(rc == -E2BIG);
	M0_UT_ASSERT(current == sink->ss_current);
	next->spb_busy = false;

	/* Test: wraparound, both stob and buffer pool */
	M0_UT_ASSERT(sink->ss_current !=
		     stobsink_pool_tlist_head(&sink->ss_pool));
	M0_UT_ASSERT(sink->ss_offset > 0);
	bp_wrapped = false;
	stob_wrapped = false;
	i = 0;
	do {
		M0_ADDB_POST(&mc, dp, cv, u++, 2, 3, 4, 5, 6, 7, 8, 9);
		if (sink->ss_current->spb_busy) {
			/* wait for buffer: test posts very fast */
			m0_mutex_lock(&sink->ss_mutex);
			sink->ss_sync = true;
			m0_mutex_unlock(&sink->ss_mutex);
			while (sink->ss_current->spb_busy)
				m0_chan_wait(&sink->ss_current->spb_wait);
			m0_mutex_lock(&sink->ss_mutex);
			sink->ss_sync = false;
			m0_mutex_unlock(&sink->ss_mutex);
		}
		if (i >= STOBSINK_TOO_MANY_REC) {
			M0_UT_ASSERT(i < STOBSINK_TOO_MANY_REC);
			break;
		}
		if (sink->ss_current ==
		    stobsink_pool_tlist_head(&sink->ss_pool))
			bp_wrapped = true;
		if (sink->ss_offset == 0) {
			if (!stob_wrapped) {
				/*
				 * ensure ctxdef record is present for
				 * retrieval tests.
				 */
				m0_addb_ctx_fini(&ctx);
				M0_ADDB_CTX_INIT(&mc, &ctx, &m0__addb_ut_ct0,
						 &m0_addb_proc_ctx);
			}
			stob_wrapped = true;
		}
	} while (!stob_wrapped || sink->ss_offset != sink->ss_segsize * 2);
	M0_UT_ASSERT(bp_wrapped);
	M0_UT_ASSERT(stob_wrapped);
	M0_UT_ASSERT(sink->ss_offset == sink->ss_segsize * 2);

	/* Test: restart */
	seq = sink->ss_seq_nr;
	M0_UT_ASSERT(sink->ss_record_nr > sink->ss_persist_nr);
	m0_addb_mc_fini(&mc);
	M0_UT_ASSERT(m0_atomic64_get(&stob->so_ref) == 2);
	m0_addb_mc_init(&mc);
	rc = m0_addb_mc_configure_stob_sink(&mc, stob, STOBSINK_SEGMENT_SIZE,
					    stob_size, timeout);
	M0_UT_ASSERT(rc == 0);
	sink = stobsink_from_mc(&mc);
	M0_UT_ASSERT(sink->ss_seq_nr == seq + 1);
	M0_UT_ASSERT(sink->ss_offset == STOBSINK_SEGMENT_SIZE * 3);
	m0_addb_mc_configure_pt_evmgr(&mc);

	/* Test: verify stob contents */
	addb_ut_stobsink_verify(sink);

	m0_addb_mc_fini(&mc);
	m0_stob_put(stob);
	dom->sd_ops->sdo_fini(dom);
	addb_rt_tlist_del(dp);
	addb_ct_tlist_del(&m0__addb_ut_ct0);

	/*
	 * Retrieval tests.  Re-uses the just-written stob.
	 */
	addb_ut_retrieval();
	/* Reset to default */
	addb_rec_post_ut_data_enabled = true;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
