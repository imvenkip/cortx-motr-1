/* -*- C -*- */
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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation: 10/23/2012
 */

/* This file is designed to be included by addb/addb.c */

#include "rpc/rpc_helpers.h" /* enum m0_bufvec_what */

/**
   @addtogroup addb_pvt
   @{
 */

struct addb_rec_post_ut_data {
	struct m0_addb_ctx       **cv;
	size_t                     cv_nr;
	uint64_t                  *fields;
	size_t                     fields_nr;
	size_t                     reclen;
	uint64_t                   rid;
	enum m0_addb_base_rec_type brt;
};
/** UT global for use with a serialized rs alloc/save */
static struct addb_rec_post_ut_data addb_rec_post_ut_data = {
	.brt = M0_ADDB_BRT_NR
};

/** Enables UT data */
static bool addb_rec_post_ut_data_enabled;

/**
   Low level record post subroutine.
 */
static void addb_rec_post(struct m0_addb_mc *mc,
			  uint64_t rid,
			  struct m0_addb_ctx **cv,
			  uint64_t *fields,
			  size_t fields_nr)
{
	struct m0_addb_rec *rec;
	size_t              len;
	size_t              ctxid_seq_data_size;
	size_t              bytes_nr;
	int                 i;
	uint64_t           *dp;
	char               *p;

	M0_PRE(m0_addb_mc_has_evmgr(mc));

	/*
	 * Compute record length.
	 */
	ctxid_seq_data_size = addb_ctxid_seq_data_size(cv);
	len = sizeof *rec + ctxid_seq_data_size + sizeof fields[0] * fields_nr;

	/*
	 * Allocate memory from the event manager.
	 * Note that the event manager may start serializing this thread after a
	 * successful call.
	 */
	rec = mc->am_evmgr->evm_rec_alloc(mc, len);
	if (rec == NULL) {
		M0_LOG(M0_NOTICE, "Unable to post ADDB record rid=%lx len=%d",
		       (unsigned long)rid, (int)len);
		return;
	}

	/*
	 * Assemble the record. Data follow the record structure.
	 */
	p = (char *)(rec + 1);
	rec->ar_rid = rid;
	rec->ar_ts = m0_time_now();
	/* context path */
	rec->ar_ctxids.acis_data = (struct m0_addb_uint64_seq *)p;
	rec->ar_ctxids.acis_nr = 0;
	bytes_nr = addb_ctxid_seq_build(&rec->ar_ctxids, cv);
	M0_ASSERT(bytes_nr == ctxid_seq_data_size);
	M0_ASSERT(rec->ar_ctxids.acis_nr > 0);
	dp = (uint64_t *)(p + ctxid_seq_data_size);
	/* field sequence */
	rec->ar_data.au64s_nr = fields_nr;
	rec->ar_data.au64s_data = dp;
	for (i = 0; i < fields_nr; ++i)
		dp[i] = fields[i];
	M0_ASSERT(len == ((void *)&dp[i] - (void *)rec));

	/* Save context for UT if required (serialized by sink alloc/save) */
	if (addb_rec_post_ut_data_enabled) {
		addb_rec_post_ut_data.cv        = cv;
		addb_rec_post_ut_data.cv_nr     = rec->ar_ctxids.acis_nr;
		addb_rec_post_ut_data.fields    = fields;
		addb_rec_post_ut_data.fields_nr = fields_nr;
		addb_rec_post_ut_data.reclen    = len;
		addb_rec_post_ut_data.rid       = rid;
		addb_rec_post_ut_data.brt       = m0_addb_rec_rid_to_brt(rid);
	}

	/*
	 * Post the record, which also releases the memory and stops serializing
	 * the thread.
	 */
	mc->am_evmgr->evm_post(mc, rec);
}

#ifndef __KERNEL__
/** @todo compile these in the kernel when RPC sink requires them */

/**
 * Calculate the size required to xcode encode a given m0_addb_rec.
 */
static m0_bcount_t addb_rec_payload_size(struct m0_addb_rec *rec)
{
	struct m0_xcode_ctx  ctx;
	struct m0_xcode_obj  obj = {
		.xo_type = m0_addb_rec_xc,
		.xo_ptr  = rec,
	};

	M0_PRE(rec != NULL);

	m0_xcode_ctx_init(&ctx, &obj);
	return m0_xcode_length(&ctx);
}

static typeof (m0_xcode_encode) * const addb_encdec_op[] = {
	[M0_BUFVEC_ENCODE] = m0_xcode_encode,
	[M0_BUFVEC_DECODE] = m0_xcode_decode,
};

/**
 * Helper function for encoding/decoding a m0_adbb_rec into/from a bufvec.
 * @param rec  record to be encoded, or receives pointer to allocated record
 * On decode, *rec is set to the allocated m0_addb_rec if *rec == NULL on input.
 * @param cur  cursor into buffer, updated up success
 * @param what direction, encode or decode
 */
static int addb_rec_encdec(struct m0_addb_rec     **rec,
			   struct m0_bufvec_cursor *cur,
			   enum m0_bufvec_what      what)
{
	struct m0_xcode_ctx ctx;
	struct m0_xcode_obj obj = {
		.xo_type = m0_addb_rec_xc
	};
	int                 rc;

	M0_PRE(rec != NULL && cur != NULL);
	M0_PRE(ergo(what == M0_BUFVEC_ENCODE, *rec != NULL));

	obj.xo_ptr = *rec;
	m0_xcode_ctx_init(&ctx, &obj);
	ctx.xcx_buf   = *cur;
	ctx.xcx_alloc = m0_xcode_alloc;

	rc = addb_encdec_op[what](&ctx);
	if (rc == 0) {
		if (what == M0_BUFVEC_DECODE)
			*rec = m0_xcode_ctx_top(&ctx);
		*cur = ctx.xcx_buf;
	}

	return rc;
}
#endif

/** @} addb_pvt */

/*
 ******************************************************************************
 * Public interfaces
 ******************************************************************************
 */
M0_INTERNAL bool m0_addb_rec_is_event(const struct m0_addb_rec *rec)
{
	M0_PRE(rec != NULL);
	return m0_addb_rec_rid_to_brt(rec->ar_rid) <= M0_ADDB_BRT_SEQ;
}

M0_INTERNAL bool m0_addb_rec_is_ctx(const struct m0_addb_rec *rec)
{
	M0_PRE(rec != NULL);
	return m0_addb_rec_rid_to_brt(rec->ar_rid) == M0_ADDB_BRT_CTXDEF;
}

M0_INTERNAL uint64_t m0_addb_rec_rid_make(enum m0_addb_base_rec_type brt,
					  uint32_t id)
{
	return (uint64_t)brt << 32 | id;
}

M0_INTERNAL enum m0_addb_base_rec_type m0_addb_rec_rid_to_brt(uint64_t rid)
{
	return (enum m0_addb_base_rec_type)(rid >> 32);
}

M0_INTERNAL uint32_t m0_addb_rec_rid_to_id(uint64_t rid)
{
	return (uint32_t)(rid & 0xffffffff);
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
