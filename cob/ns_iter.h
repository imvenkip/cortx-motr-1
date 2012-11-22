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

struct c2_cob_ns_iter {
	/** DB environment. */
	struct c2_dbenv      *cni_dbenv;

	/** Cob domain. */
	struct c2_cob_domain *cni_cdom;

	/** Last fid value returned. */
	struct c2_fid         cni_last_fid;
};

C2_INTERNAL int c2_cob_ns_iter_init(struct c2_cob_ns_iter *iter,
				    struct c2_fid *gfid,
				    struct c2_dbenv *dbenv,
				    struct c2_cob_domain *cdom);

C2_INTERNAL int c2_cob_ns_iter_next(struct c2_cob_ns_iter *iter,
				    struct c2_fid *gfid,
				    struct c2_db_tx *tx);

C2_INTERNAL void c2_cob_ns_iter_fini(struct c2_cob_ns_iter *iter);

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

