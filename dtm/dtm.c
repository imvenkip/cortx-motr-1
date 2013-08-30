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

/**
 * @addtogroup dtm Distributed transaction manager
 * @{
 */

#include "lib/misc.h"              /* M0_SET0 */
#include "lib/errno.h"             /* ENOMEM */
#include "dtm/dtm.h"
#include "dtm/dtm_update_xc.h"

M0_INTERNAL int m0_dtm_init(void)
{
	m0_xc_dtm_update_init();
	m0_xc_verno_init();
	return 0;
}

M0_INTERNAL void m0_dtm_fini(void)
{
	m0_xc_dtm_update_fini();
	m0_xc_verno_fini();
}

M0_INTERNAL void m0_dtx_init(struct m0_dtx *tx,
			     struct m0_be_domain *be_domain,
			     struct m0_sm_group  *sm_group)
{
	M0_PRE(be_domain != NULL);

	m0_be_tx_init(&tx->tx_betx, 0, be_domain, sm_group,
		      NULL, NULL, NULL, NULL);
	m0_be_tx_prep(&tx->tx_betx, &tx->tx_betx_cred);

	tx->tx_state = M0_DTX_INIT;
	m0_fol_rec_init(&tx->tx_fol_rec);
}

M0_INTERNAL void m0_dtx_open(struct m0_dtx *tx)
{
	M0_PRE(tx->tx_state == M0_DTX_INIT);
	M0_PRE(m0_be_tx_state(&tx->tx_betx) == M0_BTS_PREPARE);

	m0_be_tx_open(&tx->tx_betx);

	tx->tx_state = M0_DTX_OPEN;
}

M0_INTERNAL void m0_dtx_done(struct m0_dtx *tx)
{
	M0_PRE(M0_IN(tx->tx_state, (M0_DTX_INIT, M0_DTX_OPEN)));

	m0_be_tx_close(&tx->tx_betx);
	tx->tx_state = M0_DTX_DONE;
}

M0_INTERNAL void m0_dtx_fini(struct m0_dtx *tx)
{
	M0_PRE(M0_IN(tx->tx_state, (M0_DTX_INIT, M0_DTX_DONE)));

	m0_be_tx_fini(&tx->tx_betx);
	m0_fol_rec_fini(&tx->tx_fol_rec);
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
