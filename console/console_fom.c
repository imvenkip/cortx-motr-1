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
 * Original author       : Dipak Dudhabhate <Dipak_Dudhabhate@xyratex.com>
 * Original creation date: 08/04/2011
 * Revision              : Manish Honap <Manish_Honap@xyratex.com>
 * Revision date         : 07/31/2012
 */

#include "lib/errno.h"		/* EINVAL */
#include "lib/memory.h"		/* M0_ALLOC_PTR */
#include "fop/fom_generic.h"    /* M0_FOPH_FAILURE */
#include "console/console.h"
#include "console/console_fom.h"
#include "console/console_fop.h"
#include "console/console_mesg.h"
#include "console/console_xc.h"

/**
   @addtogroup console
   @{
*/

static size_t home_locality(const struct m0_fom *fom)
{
        M0_PRE(fom != NULL);

        return m0_fop_opcode(fom->fo_fop);
}

static void cons_fom_addb_init(struct m0_fom * fom, struct m0_addb_mc *mc)
{
	/**
	 * @todo: Do the actual impl, need to set MAGIC, so that
	 * m0_fom_init() can pass
	 */
	fom->fo_addb_ctx.ac_magic = M0_ADDB_CTX_MAGIC;
}

static void default_fom_fini(struct m0_fom *fom)
{
	m0_fom_fini(fom);
	m0_free(fom);
}

static int cons_fop_fom_create(struct m0_fop *fop, struct m0_fom **m,
			       struct m0_reqh *reqh)
{
        struct m0_fom *fom;
	struct m0_fop *rep_fop;

        M0_PRE(fop != NULL);
        M0_PRE(m != NULL);
	M0_PRE(fop->f_type == &m0_cons_fop_device_fopt);

	/*
	 * XXX
	 * The proper way to do this is to do
	 * struct m0_cons_fom {
	 *         struct m0_fom cf_fom;
	 *         struct m0_fop cf_reply;
	 *         struct m0_cons_fop_reply cf_reply_data;
	 * };
	 * Then fom, reply fop and its data packet can be allocated at once,
	 * simplifying memory management.
	 */
        M0_ALLOC_PTR(fom);
        if (fom == NULL)
                return -ENOMEM;
        rep_fop = m0_fop_alloc(&m0_cons_fop_reply_fopt, NULL);
	if (rep_fop == NULL) {
		m0_free(fom);
		return -ENOMEM;
	}

	/**
	 * NOTE: Though service type is NOT set in the FOP_TYPE_INIT
	 * for console fops, we are setting it in the console UT,
	 * where the client thread creates the fop. So the assertion in
	 * m0_fom_init should pass
	 */
	m0_fom_init(fom, &fop->f_type->ft_fom_type, &m0_cons_fom_device_ops,
		    fop, rep_fop, reqh, fop->f_type->ft_fom_type.ft_rstype);
	m0_fop_put(rep_fop);
        *m = fom;
        return 0;
}

static int cons_fom_tick(struct m0_fom *fom)
{
        struct m0_cons_fop_reply *reply_fop;
        struct m0_rpc_item       *reply_item;
        struct m0_rpc_item       *req_item;
	struct m0_fop		 *fop = fom->fo_fop;
	struct m0_fop		 *rfop = fom->fo_rep_fop;

	M0_PRE(fom != NULL && fop != NULL && rfop != NULL);

	/* Reply fop */
        reply_fop = m0_fop_data(rfop);
	if (reply_fop == NULL) {
		m0_fom_phase_set(fom, M0_FOPH_FAILURE);
		return M0_FSO_AGAIN;
	}

	/* Request item */
        req_item = &fop->f_item;

	/* Set reply FOP */
	reply_fop->cons_notify_type = req_item->ri_type->rit_opcode;
        reply_fop->cons_return = 0;

	/* Reply item */
	reply_item = &rfop->f_item;
	m0_fom_phase_set(fom, M0_FOM_PHASE_FINISH);
        m0_rpc_reply_post(req_item, reply_item);
	return M0_FSO_WAIT;
}

const struct m0_fom_ops m0_cons_fom_device_ops = {
        .fo_tick	  = cons_fom_tick,
	.fo_fini	  = default_fom_fini,
	.fo_home_locality = home_locality,
	.fo_addb_init     = cons_fom_addb_init
};

const struct m0_fom_type_ops m0_cons_fom_device_type_ops = {
        .fto_create = cons_fop_fom_create
};

/** @} end of console */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

