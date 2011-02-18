#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/assert.h"
#include "stob/stob.h"
#include "fop/fop.h"

#include "reqh.h"

/**
   @addtogroup reqh
   @{
 */

int  c2_reqh_init(struct c2_reqh *reqh,
		  struct c2_rpcmachine *rpc, struct c2_dtm *dtm,
		  struct c2_stob_domain *dom)
{
	reqh->rh_rpc = rpc;
	reqh->rh_dtm = dtm;
	reqh->rh_dom = dom;
	return 0;
}

void c2_reqh_fini(struct c2_reqh *reqh)
{
}

void c2_reqh_fop_handle(struct c2_reqh *reqh, struct c2_fop *fop)
{
}

void c2_reqh_fop_sortkey_get(struct c2_reqh *reqh, struct c2_fop *fop,
			     struct c2_fop_sortkey *key)
{
}

/** @} endgroup reqh */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
