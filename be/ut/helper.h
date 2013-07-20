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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 5-Jun-2013
 */

#pragma once
#ifndef __MERO_BE_UT_HELPER_H__
#define __MERO_BE_UT_HELPER_H__

#include "lib/types.h"          /* bool */
#include "be/be.h"              /* m0_be */
#include "dtm/dtm.h"            /* m0_dtx */
#include "net/net.h"		/* m0_net_xprt */
#include "rpc/rpclib.h"		/* m0_rpc_server_ctx */

/**
 * Helper structure for easy segment preparing for UT.
 *
 * XXX RENAMEME: s/helper/obfuscator/  --vvv
 */
struct m0_be_ut_h {
	/** Stob domain. All stobs for UT helper are in this domain */
	struct m0_stob_domain	*buh_dom;
	/** Newly created stob will use this m0_dtx */
	struct m0_dtx            buh_dtx;
	/**
	 * Stob to test. It can point to m0_be_ut_h.buh_stob_ if
	 * there is new stob and to existing stob if it isn't new.
	 */
	struct m0_stob		*buh_stob;
	/** Newly created stob. This field is unused if stob already exists */
	struct m0_stob		 buh_stob_;
	/** Segment will be initialized in this m0_be */
	struct m0_be		 buh_be;
	/** Segment to test */
	struct m0_be_seg	 buh_seg;
	/**
	 * Pointer to m0_be_ut_h.buh_seg.bs_allocator
	 * Added to increase readability of UT.
	 * Initialized in m0_be_ut_h_init().
	 */
	struct m0_be_allocator	*buh_allocator;
	/** transaction ID counter */
	uint64_t		 buh_tid;
	/** rpc server for reqh */
	struct m0_rpc_server_ctx buh_rpc_svc;
};

/** Transactions' sm_group. */
extern struct m0_sm_group ut__txs_sm_group;

/** Prepare stob and do m0_be_seg_init() */
void m0_be_ut_seg_initialize(struct m0_be_ut_h *h, bool stob_create);
/** m0_be_seg_fini() and stob finalization */
void m0_be_ut_seg_finalize(struct m0_be_ut_h *h, bool stob_put);

/** m0_be_ut_seg_initialize() + create segment */
void m0_be_ut_seg_create(struct m0_be_ut_h *h);
/** destroy segment + m0_be_ut_seg_finalize() */
void m0_be_ut_seg_destroy(struct m0_be_ut_h *h);

/**
 * Create new stob. Prepare segment on this stob to UT
 * m0_be_ut_seg_create() + m0_be_seg_open().
 */
void m0_be_ut_seg_create_open(struct m0_be_ut_h *h);
/**
 * Destroy segment and stob
 * m0_be_seg_close() + m0_be_ut_seg_destroy().
 */
void m0_be_ut_seg_close_destroy(struct m0_be_ut_h *h);

/** Create linux stob domain directory */
void m0_be_ut_seg_storage_init(void);
/** Remove linux stob domain directory */
void m0_be_ut_seg_storage_fini(void);

/**
 * - create dtx;
 * - create stob domain;
 * - create stob in the domain;
 * - create segment on this stob;
 * - initalize segment allocator.
 *
 * @note m0_be_ut_h.buh_be should be initialized before calling this function.
 */
void m0_be_ut_h_init(struct m0_be_ut_h *h);
/**
 * - finalize segment allocator, segment, stob, stob domain and dtx;
 * - remove stob domain directory.
 */
void m0_be_ut_h_fini(struct m0_be_ut_h *h);

/**
 */
void m0_be_ut_h_seg_reload(struct m0_be_ut_h *h);

/** Initialize m0_be_tx in m0_be_ut_h context */
void m0_be_ut_h_tx_init(struct m0_be_tx *tx, struct m0_be_ut_h *h);

#endif /* __MERO_BE_UT_HELPER_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
