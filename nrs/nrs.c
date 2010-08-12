/* -*- C -*- */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "addb/addb.h"
#include "reqh/reqh.h"
#include "fop/fop.h"
#include "nrs/nrs.h"

/**
   @addtogroup nrs

   An extremely simple mock NRS for now: immediately forward en-queued fop to
   the request handler.

   @{
*/

static const struct c2_addb_loc nrs_addb = {
	.al_name = "nrs"
};

C2_ADDB_EV_DEFINE(sns_addb_enqueue, "enqueue", 0x10, C2_ADDB_STAMP);

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
	C2_ADDB_ADD(&fop->f_addb, &nrs_addb, sns_addb_enqueue);
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
