/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 07/31/2010
 */

#include "addb/addb.h"
#include "fop/fop.h"
#include "fop/fom.h"
#include "reqh/reqh.h"
#include "nrs/nrs.h"
#include "rpc/rpc2.h"

/**
   @addtogroup nrs

   An extremely simple mock NRS for now: immediately forward en-queued fop to
   the request handler.

   @{
*/

static const struct c2_addb_loc nrs_addb = {
	.al_name = "nrs"
};

C2_ADDB_EV_DEFINE(nrs_addb_enqueue, "enqueue", 0x10, C2_ADDB_STAMP);

int c2_nrs_init(struct c2_nrs *nrs, struct c2_reqh *reqh)
{
	nrs->n_reqh = reqh;
	return 0;
}

void c2_nrs_fini(struct c2_nrs *nrs)
{
}

void c2_nrs_enqueue(struct c2_nrs *nrs, struct c2_fop *fop)
{
	C2_ADDB_ADD(&fop->f_addb, &nrs_addb, nrs_addb_enqueue);
	c2_reqh_fop_handle(nrs->n_reqh, fop);
}

/** @} end of nrs group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
