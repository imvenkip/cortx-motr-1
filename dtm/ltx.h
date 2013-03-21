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
 * Original creation date: 27-Jan-2013
 */


#pragma once

#ifndef __MERO_DTM_LTX_H__
#define __MERO_DTM_LTX_H__


/**
 * @defgroup dtm
 *
 * @{
 */

#include "db/db.h"
#include "dtm/history.h"

/* export */
struct m0_dtm_ltx;

struct m0_dtm_ltx {
	struct m0_dtm_history  lx_hi;
	struct m0_db_tx        lx_tx;
	struct m0_db_tx_waiter lx_waiter;
};

M0_INTERNAL int  m0_dtm_ltx_init(struct m0_dtm_ltx *ltx, struct m0_dbenv *env);
M0_INTERNAL void m0_dtm_ltx_fini(struct m0_dtm_ltx *ltx);


/** @} end of dtm group */

#endif /* __MERO_DTM_LTX_H__ */


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
