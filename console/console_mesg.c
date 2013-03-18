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
#include "lib/misc.h"  /* M0_BITS */

#include "console/console_mesg.h"
#include "console/console_it.h" /* m0_cons_fop_fields_show */

M0_INTERNAL void m0_cons_fop_name_print(const struct m0_fop_type *ftype)
{
	fprintf(stdout, "%.2d %s\n", ftype->ft_rpc_item_type.rit_opcode,
				    ftype->ft_name);
}

M0_INTERNAL int m0_cons_fop_send(struct m0_fop *fop,
				 struct m0_rpc_session *session,
				 m0_time_t resend_interval,
				 uint64_t nr_sent_max)
{
	struct m0_rpc_item *item;
	int		    rc;

	M0_PRE(fop != NULL && session != NULL);

	item = &fop->f_item;
	item->ri_deadline        = 0;
	item->ri_prio            = M0_RPC_ITEM_PRIO_MID;
	item->ri_session         = session;
	item->ri_nr_sent_max     = nr_sent_max;
	item->ri_resend_interval = resend_interval;

        rc = m0_rpc_post(item);
	if (rc == 0) {
		rc = m0_rpc_item_wait_for_reply(item, M0_TIME_NEVER);
		if (rc != 0)
			fprintf(stderr, "Error while waiting for reply: %d\n",
				rc);
	} else {
		fprintf(stderr, "m0_rpc_post failed!\n");
	}
	return rc;
}

M0_INTERNAL int m0_cons_fop_show(struct m0_fop_type *fopt)
{
	struct m0_fop *fop;
	int            rc;

	fop = m0_fop_alloc(fopt, NULL);
	if (fop == NULL) {
		fprintf(stderr, "FOP allocation failed\n");
		return -ENOMEM;
	}

	rc = m0_cons_fop_fields_show(fop);

	m0_fop_put(fop);
	return rc;
}

M0_INTERNAL void m0_cons_fop_list_show(void)
{
        struct m0_fop_type *ftype;

	fprintf(stdout, "List of FOP's: \n");
	ftype = NULL;
	while ((ftype = m0_fop_type_next(ftype)) != NULL)
		m0_cons_fop_name_print(ftype);
}

M0_INTERNAL struct m0_fop_type *m0_cons_fop_type_find(uint32_t opcode)
{
        struct m0_fop_type *ftype;

	ftype = NULL;
	while ((ftype = m0_fop_type_next(ftype)) != NULL) {
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
