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


/**
 * @addtogroup dtm
 *
 * @{
 */

#include "lib/memory.h"
#include "lib/errno.h"              /* ENOMEM */

#include "dtm/history.h"
#include "dtm/dtx.h"

M0_INTERNAL void m0_dtm_dtx_init(struct m0_dtm_dtx *dtx, struct m0_dtm *dtm)
{
	m0_dtm_history_init(&dtx->dx_history, dtm);
	m0_dtm_oper_init(&dtx->dx_close, dtm);
}

M0_INTERNAL void m0_dtm_dtx_fini(struct m0_dtm_dtx *dtx)
{
	m0_dtm_oper_fini(&dtx->dx_close);
	m0_dtm_history_fini(&dtx->dx_history);
}

M0_INTERNAL int m0_dtm_dtx_add(struct m0_dtm_dtx *dtx, struct m0_dtm_oper *oper)
{
	return 0;
}

M0_INTERNAL int m0_dtm_dtx_close(struct m0_dtm_dtx *dtx)
{
	return 0;
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
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
