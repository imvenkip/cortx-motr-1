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

#include "lib/errno.h" /* ENOTSUP */
#include "lib/misc.h"  /* C2_BITS */

#include "console/console_fop.h"
#include "console/console_it.h"
#include "console/console_mesg.h"

C2_INTERNAL void c2_cons_fop_name_print(const struct c2_fop_type *ftype)
{
	fprintf(stdout, "%.2d, %s", ftype->ft_rpc_item_type.rit_opcode,
				    ftype->ft_name);
}

C2_INTERNAL int c2_cons_fop_send(struct c2_fop *fop,
				 struct c2_rpc_session *session,
				 c2_time_t timeout)
{
	struct c2_rpc_item *item;
	int		    rc;

	C2_PRE(fop != NULL && session != NULL);

	item = &fop->f_item;
	item->ri_deadline   = 0;
	item->ri_prio       = C2_RPC_ITEM_PRIO_MID;
	item->ri_session    = session;
	item->ri_op_timeout = timeout;

        rc = c2_rpc_post(item);
	if (rc == 0) {
		rc = c2_rpc_item_wait_for_reply(item, C2_TIME_NEVER);
		if (rc != 0)
			fprintf(stderr, "Error while waiting for reply: %d\n",
				rc);
	} else {
		fprintf(stderr, "c2_rpc_post failed!\n");
	}
	return rc;
}

C2_INTERNAL int c2_cons_fop_show(struct c2_fop_type *fopt)
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

C2_INTERNAL void c2_cons_fop_list_show(void)
{
        struct c2_fop_type *ftype;

	fprintf(stdout, "List of FOP's: \n");
	ftype = NULL;
	while ((ftype = c2_fop_type_next(ftype)) != NULL) {
		c2_cons_fop_name_print(ftype);
		fprintf(stdout, "\n");
	}
}

C2_INTERNAL struct c2_fop_type *c2_cons_fop_type_find(uint32_t opcode)
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

