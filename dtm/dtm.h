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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 04/01/2010
 */

#pragma once

#ifndef __MERO_DTM_DTM_H__
#define __MERO_DTM_DTM_H__

#include "db/db.h"
#include "fol/fol.h"

/**
   @defgroup dtm Distributed transaction manager
   @{
*/

/* export */
struct m0_dtm;
struct m0_dtx;

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
	struct m0_fol_rec  tx_fol_rec;
};

M0_INTERNAL void m0_dtx_init(struct m0_dtx *tx);
M0_INTERNAL int m0_dtx_open(struct m0_dtx *tx, struct m0_dbenv *env);
M0_INTERNAL int m0_dtx_done(struct m0_dtx *tx);
M0_INTERNAL void m0_dtx_fini(struct m0_dtx *tx);

M0_INTERNAL int m0_dtm_init(void);
M0_INTERNAL void m0_dtm_fini(void);

/** @} end of dtm group */
#endif /* __MERO_DTM_DTM_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
