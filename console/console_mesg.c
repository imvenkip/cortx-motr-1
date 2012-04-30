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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 09/09/2011
 */
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "lib/errno.h" /* ENOTSUP */

#include "console/console_fop.h"
#include "console/console_it.h"
#include "console/console_mesg.h"

void c2_cons_fop_name_print(const struct c2_fop_type *ftype)
{
	fprintf(stdout, "%.2d, %s", ftype->ft_rpc_item_type.rit_opcode,
				    ftype->ft_name);
}

int c2_cons_fop_send(struct c2_fop *fop, struct c2_rpc_session *session,
		     c2_time_t deadline)
{
	struct c2_rpc_item *item;
	struct c2_clink	    clink;
	int		    rc = 0;
	bool		    wait;

	C2_PRE(fop != NULL && session != NULL);

	/* Init rpc item and assign priority, session, etc */
	item = &fop->f_item;
	/* Add link to wait for item reply */
	c2_clink_init(&clink, NULL);
	c2_clink_add(&item->ri_chan, &clink);
	item->ri_deadline = 0;
	item->ri_prio     = C2_RPC_ITEM_PRIO_MAX;
	item->ri_group    = NULL;
	item->ri_session  = session;
	item->ri_error    = 0;
        rc = c2_rpc_post(item);
	if (rc != 0) {
		fprintf(stderr, "c2_rpc_post failed!\n");
		goto error;
	}

	/* Wait for reply */
	wait = c2_chan_timedwait(&clink, deadline);
	if (!wait) {
		fprintf(stderr, "Timed out for reply.\n");
		rc = -ETIMEDOUT;
	}
error:
	/* Fini clink */
	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	return rc;
}

int c2_cons_fop_show(struct c2_fop_type *fopt)
{
	struct c2_fop *fop;
	void	      *fdata;

	fop = c2_fop_alloc(fopt, NULL);
	if (fop != NULL) {
		fdata = c2_fop_data(fop);
		if (fdata != NULL) {
			c2_cons_fop_fields_show(fop);
			c2_fop_free(fop);
		} else {
			fprintf(stderr, "FOP data does not exist\n");
			return -EINVAL;
		}
	} else {
		fprintf(stderr, "FOP allocation failed\n");
		return -EINVAL;
	}
	return 0;
}

void c2_cons_fop_list_show(void)
{
        struct c2_fop_type *ftype;

	fprintf(stdout, "List of FOP's: \n");
	ftype = NULL;
	while ((ftype = c2_fop_type_next(ftype)) != NULL) {
		c2_cons_fop_name_print(ftype);
		fprintf(stdout, "\n");
	}
}

struct c2_fop_type *c2_cons_fop_type_find(uint32_t opcode)
{
        struct c2_fop_type *ftype;

	ftype = NULL;
	while ((ftype = c2_fop_type_next(ftype)) != NULL) {
		if(ftype->ft_rpc_item_type.rit_opcode == opcode)
			break;
	}
	return ftype;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

