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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 04/01/2010
 */

#pragma once

#ifndef __MERO_DTM_DTM_H__
#define __MERO_DTM_DTM_H__

#include "db/db.h"

/**
   @defgroup dtm Distributed transaction manager
   @{
*/

/* export */
struct m0_dtm;
struct m0_dtx;
struct m0_epoch_id;
struct m0_update_id;

struct m0_dtm {};

enum m0_dtx_state {
	M0_DTX_INIT = 1,
	M0_DTX_OPEN,
	M0_DTX_DONE,
	M0_DTX_COMMIT,
	M0_DTX_STABLE
};

struct m0_dtx {
	/**
	   @todo placeholder for now.
	 */
	enum m0_dtx_state  tx_state;
	struct m0_db_tx    tx_dbtx;
	struct m0_fol_rec *tx_fol_rec;
};

struct m0_update_id {
	uint32_t ui_node;
	uint64_t ui_update;
};

enum m0_update_state {
	M0_US_INVALID,
	M0_US_VOLATILE,
	M0_US_PERSISTENT,
	M0_US_NR
};

M0_INTERNAL int m0_dtx_init(struct m0_dtx *tx);
M0_INTERNAL int m0_dtx_open(struct m0_dtx *tx, struct m0_dbenv *env);
M0_INTERNAL int m0_dtx_commit(struct m0_dtx *tx);
M0_INTERNAL int m0_dtx_abort(struct m0_dtx *tx);
M0_INTERNAL void m0_dtx_fini(struct m0_dtx *tx);

/** @} end of dtm group */

/* __MERO_DTM_DTM_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
