/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED, A SEAGATE COMPANY
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
 * Original authors: Juan   Gonzalez <juan.gonzalez@seagate.com>
 *                   James  Morse    <james.s.morse@seagate.com>
 *                   Sining Wu       <sining.wu@seagate.com>
 * Revision:         Pratik Shinde   <pratik.shinde@seagate.com>
 * Original creation date: 01-Apr-2014
 */


#include "mdservice/fsync_fops.h"       /* m0_fop_fsync_mds_fopt */
#include "fop/fop.h"                    /* m0_fop */

#pragma once

#ifndef __MERO_CLOVIS_OSYNC_H__
#define __MERO_CLOVIS_OSYNC_H__

/**
 * Experimental: sync operation for objects (osync for short).
 * This is heavily based on the m0t1fs::fsync work, see fsync DLD
 * for details. Clovis re-uses the fsync fop defined in m0t1fs.
 */

/* import */
struct m0_clovis_obj;
struct m0_reqh_service_ctx;

M0_TL_DESCR_DECLARE(ospti, M0_EXTERN);
M0_TL_DECLARE(ospti, M0_EXTERN, struct m0_reqh_service_txid);

/**
 * Wrapper for sync messages, used to list/group pending replies
 * and pair fop/reply with the struct m0_reqh_service_txid
 * that needs updating.
 */
struct clovis_osync_fop_wrapper {
	/** The fop for fsync messages */
	struct m0_fop                ofw_fop;

	/**
	 * The service transaction that needs updating
	 * gain the m0t1fs_inode::ci_pending_txid_lock lock
	 * for inodes or the m0_reqh_service_ctx::sc_max_pending_tx_lock
	 * for the super block before dereferencing
	 */
	struct m0_reqh_service_txid *ofw_stx;

	struct m0_tlink              ofw_tlink;
	uint64_t                     ofw_tlink_magic;
};

/**
 * Ugly abstraction of clovis_osync interactions with wider mero code
 * - purely to facilitate unit testing.
 * - this is used in osync.c and its unit tests.
 */
struct clovis_osync_interactions {
	int (*post_rpc)(struct m0_rpc_item *item);
	int (*wait_for_reply)(struct m0_rpc_item *item, m0_time_t timeout);
	void (*fop_fini)(struct m0_fop *fop);
	void (*fop_put)(struct m0_fop *fop);
};

/**
 * Updates osync records in fop callbacks.
 * Service must be specified, one or both of csb/inode should be specified.
 * new_txid may be null.
 */
M0_INTERNAL void
clovis_osync_record_update(struct m0_reqh_service_ctx *service,
			   struct m0_clovis           *m0c,
			   struct m0_clovis_obj       *obj,
			   struct m0_be_tx_remid      *btr);


/**
 * Creates and sends an fsync fop from the provided m0_reqh_service_txid.
 */
M0_INTERNAL int
clovis_osync_request_create(struct m0_reqh_service_txid      *stx,
			    struct clovis_osync_fop_wrapper **ofw,
			    enum m0_fsync_mode                mode);


/**
 * Waits for a reply to an fsync fop and process it.
 * Cleans-up the fop allocated in clovis_osync_request_create.
 *
 * obj may be NULL if the reply is only likely to touch the super block.
 * m0c may be NULL, iff obj is specified.
 *
 */
M0_INTERNAL int
clovis_osync_reply_process(struct m0_clovis                *m0c,
			   struct m0_clovis_obj            *obj,
			   struct clovis_osync_fop_wrapper *ofw);

/**
 * clovis_osync_core sends an fsync-fop to a list of services, then blocks,
 * waiting for replies. This is implemented as two loops.
 * The 'fop sending loop', generates and posts fops, adding them to a list
 * of pending fops. This is all done while holding the
 * m0_clovis_obj::ob_pending_txid_lock. The 'reply receiving loop'
 * works over the list of pending fops, waiting for a reply for each one.
 * It acquires the m0_clovis_obj::ob_pending_txid_lock only
 * when necessary.
 */
M0_INTERNAL int clovis_osync_core(struct m0_clovis_obj *obj,
				  enum m0_fsync_mode    mode);

#endif /* __MERO_CLOVIS_OSYNC_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
