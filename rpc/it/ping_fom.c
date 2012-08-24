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
 * Original author: Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 07/07/2011
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fop/fop.h"
#include "rpc/it/ping_fom.h"
#include "rpc/it/ping_fop_ff.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "rpc/rpc2.h"
#include "fop/fom_generic.h"

static int ping_fop_fom_create(struct c2_fop *fop, struct c2_fom **m);

/** Generic ops object for ping */
struct c2_fom_ops c2_fom_ping_ops = {
	.fo_fini          = c2_fop_ping_fom_fini,
	.fo_tick          = c2_fom_ping_state,
	.fo_home_locality = c2_fom_ping_home_locality
};

/** FOM type specific functions for ping FOP. */
const struct c2_fom_type_ops c2_fom_ping_type_ops = {
	.fto_create = ping_fop_fom_create
};

size_t c2_fom_ping_home_locality(const struct c2_fom *fom)
{
	C2_PRE(fom != NULL);

	return fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode;
}

/**
 * State function for ping request
 */
int c2_fom_ping_state(struct c2_fom *fom)
{
	struct c2_fop			*fop;
        struct c2_fop_ping_rep		*ping_fop_rep;
        struct c2_rpc_item              *item;
        struct c2_fom_ping		*fom_obj;

	fom_obj = container_of(fom, struct c2_fom_ping, fp_gen);
        fop = c2_fop_alloc(&c2_fop_ping_rep_fopt, NULL);
        C2_ASSERT(fop != NULL);
        ping_fop_rep = c2_fop_data(fop);
        ping_fop_rep->fpr_rc = true;
	item = c2_fop_to_rpc_item(fop);
        c2_rpc_reply_post(&fom_obj->fp_fop->f_item, item);
	c2_fom_phase_set(fom, C2_FOPH_FINISH);
	return C2_FSO_WAIT;
}

/* Init for ping */
static int ping_fop_fom_create(struct c2_fop *fop, struct c2_fom **m)
{
        struct c2_fom                   *fom;
        struct c2_fom_ping		*fom_obj;

        C2_PRE(fop != NULL);
        C2_PRE(m != NULL);

        fom_obj= c2_alloc(sizeof(struct c2_fom_ping));
        if (fom_obj == NULL)
                return -ENOMEM;
	fom = &fom_obj->fp_gen;
	c2_fom_init(fom, &fop->f_type->ft_fom_type, &c2_fom_ping_ops, fop,
		    NULL);
	fom_obj->fp_fop = fop;
	*m = fom;
	return 0;
}

void c2_fop_ping_fom_fini(struct c2_fom *fom)
{
	struct c2_fom_ping *fom_obj;

	fom_obj = container_of(fom, struct c2_fom_ping, fp_gen);
	c2_fom_fini(fom);
	c2_free(fom_obj);

	return;
}

/** @} end of io_foms */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

