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
 * Original author: Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 11/21/2012
 */

#pragma once

#ifndef __COLIBRI_COB_NS_ITER_H__
#define __COLIBRI_COB_NS_ITER_H__

#include "cob/cob.h"

/**
 * @defgroup cob_fid_ns_iter Cob-fid namespace iterator
 *
 * The cob on data server has cob nskey = <gob_fid, unit_index>,
 * where,
 * gob_fid   : global file identifier corresponding to which the cob is
 *             being created.
 * unit_index: the index of the cob in the parity group.
 *
 * @see c2_cob_nskey
 *
 * The cob-fid iterator uniquely iterates over gob_fids, thus skipping entries
 * with same gob_fids but different unit_index.
 *
 * This iterator is used in SNS repair iterator. @see c2_sns_repair_iter
 *
 * @{
 */


struct c2_cob_fid_ns_iter {
	/** DB environment. */
	struct c2_dbenv      *cni_dbenv;

	/** Cob domain. */
	struct c2_cob_domain *cni_cdom;

	/** Last fid value returned. */
	struct c2_fid         cni_last_fid;
};

/**
 * Initialises the namespace iterator.
 * @param iter - Cob dif namespace ietrator that is to be initialised.
 * @param gfid - Initial gob-fid with which iterator is initialised.
 * @param dbenv - DB environment from which the records should be extracted.
 * @param cdom - Cob domain.
 */
C2_INTERNAL int c2_cob_ns_iter_init(struct c2_cob_fid_ns_iter *iter,
				    struct c2_fid *gfid,
				    struct c2_dbenv *dbenv,
				    struct c2_cob_domain *cdom);

/**
 * Iterates over namespace to point to unique gob fid in the namespace.
 * @param iter - Pointer to the namespace iterator.
 * @param tx - Database transaction used for DB operations by iterator.
 * @param gfid - Next unique gob-fid in the iterator. This is output variable.
 */
C2_INTERNAL int c2_cob_ns_iter_next(struct c2_cob_fid_ns_iter *iter,
				    struct c2_db_tx *tx,
				    struct c2_fid *gfid);

/**
 * Finalises the namespace iterator.
 * @param iter - Namespace iterator that is to be finalised.
 */
C2_INTERNAL void c2_cob_ns_iter_fini(struct c2_cob_fid_ns_iter *iter);

/** @} end group cob_fid_ns_iter */

#endif    /* __COLIBRI_COB_NS_ITER_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

