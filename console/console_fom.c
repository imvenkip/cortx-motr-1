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
 * Original author: Dipak Dudhabhate <Dipak_Dudhabhate@xyratex.com>
 * Original creation date: 08/04/2011
 */
/*
 * Failure fops should be defined by not yet existing "failure" module. For the
 * time being, it makes sense to put them in cm/ or console/. ioservice is not
 * directly responsible for handling failures, it is intersected by copy-machine
 * (cm).
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "lib/errno.h"		/* EINVAL */
#include "lib/memory.h"		/* C2_ALLOC_PTR */
#include "console/console_fom.h"
#include "console/console_fop.h"
#include "console/console_mesg.h"
#include "console/console_u.h"

/**
   @addtogroup console
   @{
*/

static size_t home_locality(const struct c2_fom *fom)
{
        C2_PRE(fom != NULL);

        return fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode;
}

static void default_fom_fini(struct c2_fom *fom)
{
        return;
}

static int cons_fop_fom_create(struct c2_fop *fop, struct c2_fom **m)
{
        struct c2_fom *fom;
	struct c2_fop *rep_fop;

        C2_PRE(fop != NULL);
        C2_PRE(m != NULL);
	C2_PRE(fop->f_type == &c2_cons_fop_device_fopt);

	/*
	 * XXX
	 * The proper way to do this is to do
	 * struct c2_cons_fom {
	 *         struct c2_fom cf_fom;
	 *         struct c2_fop cf_reply;
	 *         struct c2_cons_fop_reply cf_reply_data;
	 * };
	 * Then fom, reply fop and its data packet can be allocated at once,
	 * simplifying memory management.
	 */
        C2_ALLOC_PTR(fom);
        if (fom == NULL)
                return -ENOMEM;
        rep_fop = c2_fop_alloc(&c2_cons_fop_reply_fopt, NULL);
	if (rep_fop == NULL) {
		c2_free(fom);
		return -ENOMEM;
	}

	c2_fom_create(fom, &fop->f_type->ft_fom_type, &c2_cons_fom_device_ops,
			fop, rep_fop);

        *m = fom;
        return 0;
}

static int cons_fom_state(struct c2_fom *fom)
{
        struct c2_cons_fop_reply *reply_fop;
        struct c2_rpc_item       *reply_item;
        struct c2_rpc_item       *req_item;
	struct c2_fop		 *fop = fom->fo_fop;
	struct c2_fop		 *rfop = fom->fo_rep_fop;

	C2_PRE(fom != NULL && fop != NULL && rfop != NULL);

	/* Reply fop */
        reply_fop = c2_fop_data(rfop);
	if (reply_fop == NULL)
		return -EINVAL;

	/* Request item */
        req_item = &fop->f_item;

	/* Set repy FOP */
	reply_fop->cons_notify_type = req_item->ri_type->rit_opcode;
        reply_fop->cons_return = 0;

	/* Reply item */
	reply_item = &rfop->f_item;
	fom->fo_phase = FOPH_FINISH;
        return c2_rpc_reply_post(req_item, reply_item);
}

const struct c2_fom_ops c2_cons_fom_device_ops = {
        .fo_state	  = cons_fom_state,
	.fo_fini	  = default_fom_fini,
	.fo_home_locality = home_locality,
};

static const struct c2_fom_type_ops c2_cons_fom_device_type_ops = {
        .fto_create = cons_fop_fom_create
};

struct c2_fom_type c2_cons_fom_device_type = {
        .ft_ops = &c2_cons_fom_device_type_ops
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

