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

/**
 * @addtogroup dtm Distributed transaction manager
 * @{
 */

#include "lib/misc.h"              /* M0_SET0 */
#include "lib/errno.h"             /* ENOMEM */
#include "dtm/dtm.h"
#include "dtm/dtm_update.h"

M0_INTERNAL int m0_dtx_init(struct m0_dtx *tx)
{
	M0_SET0(tx);
	tx->tx_state = M0_DTX_INIT;
	m0_fol_rec_part_list_init(&tx->tx_fol_rec);
}

M0_INTERNAL int m0_dtx_open(struct m0_dtx *tx, struct m0_dbenv *env)
{
	int result;

	M0_PRE(tx->tx_state == M0_DTX_INIT);

	result = m0_db_tx_init(&tx->tx_dbtx, env, 0);
	if (result == 0)
		tx->tx_state = M0_DTX_OPEN;
	return result;
}

M0_INTERNAL int m0_dtx_done(struct m0_dtx *tx)
{
	int rc = 0;

	M0_PRE(M0_IN(tx->tx_state, (M0_DTX_INIT, M0_DTX_OPEN)));

	if (tx->tx_state == M0_DTX_OPEN)
		rc = m0_db_tx_commit(&tx->tx_dbtx);

	tx->tx_state = M0_DTX_DONE;
	m0_dtx_fini(tx);
	return rc;
}

M0_INTERNAL void m0_dtx_fini(struct m0_dtx *tx)
{
	M0_PRE(M0_IN(tx->tx_state, (M0_DTX_INIT, M0_DTX_DONE)));

	m0_fol_rec_part_list_init(&tx->tx_fol_rec);
}

/** @} end of dtm group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
