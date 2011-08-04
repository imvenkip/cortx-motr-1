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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>,
 *			Mandar Sawant <Mandar_Sawant@xyratex.com>
 * Original creation date: 05/19/2010
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "stob/stob.h"
#include "net/net.h"
#include "fop/fop.h"
#include "fop/fom.h"
#include "fop/fop_iterator.h"
#include "dtm/dtm.h"
#include "fop/fop_format_def.h"

#include "reqh.h"

/**
   @addtogroup reqh
   @{
 */

/**
 * Reqh addb event location identifier object.
 */
const struct c2_addb_loc c2_reqh_addb_loc = {
	.al_name = "reqh"
};

/**
 * Reqh state of addb context.
 */
const struct c2_addb_ctx_type c2_reqh_addb_ctx_type = {
	.act_name = "reqh"
};

/**
 * Reqh addb context.
 */
struct c2_addb_ctx c2_reqh_addb_ctx;

#define REQH_ADDB_ADD(addb_ctx, name, rc)  \
C2_ADDB_ADD(&(addb_ctx), &c2_reqh_addb_loc, c2_addb_func_fail, (name), (rc))

extern int c2_reqh_fop_init(void);
extern void c2_reqh_fop_fini(void);

int  c2_reqh_init(struct c2_reqh *reqh,
		struct c2_rpcmachine *rpc, struct c2_dtm *dtm,
		struct c2_stob_domain *stdom, struct c2_fol *fol,
		struct c2_service *serv)
{
	int result;

	C2_PRE(reqh != NULL && stdom != NULL && fol != NULL && serv != NULL);

	result = c2_fom_domain_init(&reqh->rh_fom_dom);
	if (result == 0) {
		C2_ASSERT(c2_fom_domain_invariant(&reqh->rh_fom_dom));
		reqh->rh_rpc = rpc;
		reqh->rh_dtm = dtm;
		reqh->rh_stdom = stdom;
		reqh->rh_fol = fol;
		reqh->rh_serv = serv;
		reqh->rh_fom_dom.fd_reqh = reqh;
	} else
		REQH_ADDB_ADD(c2_reqh_addb_ctx, "c2_reqh_init", result);

	return result;
}
C2_EXPORTED(c2_reqh_init);

void c2_reqh_fini(struct c2_reqh *reqh)
{
	C2_PRE(reqh != NULL);
	c2_fom_domain_fini(&reqh->rh_fom_dom);
}
C2_EXPORTED(c2_reqh_fini);

void c2_reqhs_fini(void)
{
	c2_addb_ctx_fini(&c2_reqh_addb_ctx);
	c2_reqh_fop_fini();
}
C2_EXPORTED(c2_reqhs_fini);

int c2_reqhs_init(void)
{
	c2_addb_ctx_init(&c2_reqh_addb_ctx, &c2_reqh_addb_ctx_type,
					&c2_addb_global_ctx);
	return c2_reqh_fop_init();
}
C2_EXPORTED(c2_reqhs_init);

void c2_reqh_fop_handle(struct c2_reqh *reqh, struct c2_fop *fop, void *cookie)
{
	struct c2_fom	       *fom;
	int			result;
	size_t			iloc;

	C2_PRE(reqh != NULL);
	C2_PRE(fop != NULL);

	result = fop->f_type->ft_ops->fto_fom_init(fop, &fom);
	if (result == 0) {
		fom->fo_cookie = cookie;
		fom->fo_fol = reqh->rh_fol;
		fom->fo_domain = &reqh->rh_fom_dom;

		iloc = fom->fo_ops->fo_home_locality(fom);
		C2_ASSERT(iloc >= 0 && iloc <= fom->fo_domain->fd_localities_nr);
		fom->fo_loc = &reqh->rh_fom_dom.fd_localities[iloc];
		C2_ASSERT(c2_fom_invariant(fom));
		c2_fom_queue(fom);
	} else
		REQH_ADDB_ADD(c2_reqh_addb_ctx, "c2_reqh_fop_handle", result);
}
C2_EXPORTED(c2_reqh_fop_handle);

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
