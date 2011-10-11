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
#include "reqh/reqh_service.h"

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

bool c2_reqh_invariant(const struct c2_reqh *reqh)
{
	return reqh != NULL && reqh->rh_stdom != NULL &&
		reqh->rh_dbenv != NULL && reqh->rh_cob_domain != NULL &&
		reqh->rh_fol != NULL;
}

int  c2_reqh_init(struct c2_reqh *reqh, struct c2_dtm *dtm,
                struct c2_stob_domain *stdom, struct c2_dbenv *db,
                struct c2_cob_domain *cdom, struct c2_fol *fol)
{
	int result;

	C2_PRE(reqh != NULL);

	result = c2_fom_domain_init(&reqh->rh_fom_dom);
	if (result == 0) {
		C2_ASSERT(c2_fom_domain_invariant(&reqh->rh_fom_dom));
                reqh->rh_dtm = dtm;
                reqh->rh_stdom = stdom;
                reqh->rh_dbenv = db;
                reqh->rh_cob_domain = cdom;
                reqh->rh_fol = fol;
                reqh->rh_fom_dom.fd_reqh = reqh;
                c2_list_init(&reqh->rh_services);
                c2_list_init(&reqh->rh_rpcmachines);

	} else
		REQH_ADDB_ADD(c2_reqh_addb_ctx, "c2_reqh_init", result);

	return result;
}
C2_EXPORTED(c2_reqh_init);

void c2_reqh_fini(struct c2_reqh *reqh)
{
        C2_PRE(reqh != NULL);
        c2_fom_domain_fini(&reqh->rh_fom_dom);
        c2_list_fini(&reqh->rh_services);
        c2_list_fini(&reqh->rh_rpcmachines);
}
C2_EXPORTED(c2_reqh_fini);

void c2_reqhs_fini(void)
{
	c2_addb_ctx_fini(&c2_reqh_addb_ctx);
	c2_reqh_service_types_fini();
	c2_reqh_fop_fini();
}
C2_EXPORTED(c2_reqhs_fini);

int c2_reqhs_init(void)
{
	c2_addb_ctx_init(&c2_reqh_addb_ctx, &c2_reqh_addb_ctx_type,
					&c2_addb_global_ctx);
	c2_reqh_service_types_init();
	return c2_reqh_fop_init();
}
C2_EXPORTED(c2_reqhs_init);

void c2_reqh_fop_handle(struct c2_reqh *reqh,  struct c2_fop *fop)
{
	struct c2_fom	       *fom;
	struct c2_fom_domain   *dom;
	int			result;
	size_t			loc_idx;

	C2_PRE(reqh != NULL);
	C2_PRE(fop != NULL);

	result = fop->f_type->ft_ops->fto_fom_init(fop, &fom);
	if (result != -ENOMEM) {
		fom->fo_fol = reqh->rh_fol;
		dom = &reqh->rh_fom_dom;

		loc_idx = fom->fo_ops->fo_home_locality(fom) % dom->fd_localities_nr;
		C2_ASSERT(loc_idx >= 0 && loc_idx < dom->fd_localities_nr);
		fom->fo_loc = &reqh->rh_fom_dom.fd_localities[loc_idx];
		if (result != 0) {
			fom->fo_phase = FOPH_FAILURE;
			fom->fo_rc = result;
		}
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
