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
#  include <config.h>
#endif

#include "lib/errno.h" /* ENOTSUP */

#include "console/console_fop.h"
#include "console/console_it.h"
#include "console/console_mesg.h"

/**
 * @brief Prints name and type of console message.
 *
 * @param mesg console message
 */
void c2_cons_mesg_name_print(const struct c2_cons_mesg *mesg)
{
	printf("%.2d %s", mesg->cm_type, mesg->cm_name);
}

/**
 * @brief Prints names of FOP members.
 */
static void mesg_show(struct c2_fop *fop)
{
	struct c2_fit it;

	c2_fop_all_object_it_init(&it, fop);
        c2_cons_fop_fields_show(&it);
        c2_fop_all_object_it_fini(&it);
        c2_fop_free(fop);
}

/**
 * @brief Assigns values to FOP members using FOP iterator.
 */
static void mesg_input(struct c2_fop *fop)
{
	struct c2_fit it;

        /* FOP iterator will prompt for each field in fop. */
        c2_fop_all_object_it_init(&it, fop);
        c2_cons_fop_obj_input(&it);
        c2_fop_all_object_it_fini(&it);
}

int c2_cons_mesg_send(struct c2_cons_mesg *mesg, c2_time_t deadline)
{
	struct c2_fop	   *fop;
	struct c2_rpc_item *item;
	struct c2_clink	    clink;
	int		    rc = 0;
	bool		    wait;

	/* Allocate fop */
	fop = c2_fop_alloc(mesg->cm_fopt, NULL);
	if (fop == NULL)
                return -EINVAL;
	mesg->cm_fop = fop;
	/* Init fop by input from console or yaml file */
	mesg_input(fop);

	/* Init rpc item and assign priority, session, etc */
	item = &fop->f_item;
	/* Add link to wait for item reply */
	c2_clink_init(&clink, NULL);
	c2_clink_add(&item->ri_chan, &clink);
	item->ri_deadline = 0;
	item->ri_prio = C2_RPC_ITEM_PRIO_MAX;
	item->ri_group = NULL;
	item->ri_type = mesg->cm_item_type;
	item->ri_ops = mesg->cm_item_ops;
	item->ri_session = mesg->cm_rpc_session;
	item->ri_error = 0;
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

/* Disk FOP message */
/**
 * @brief RPC item operation for disk failure notification.
 */
static const struct c2_rpc_item_ops c2_rpc_item_cons_disk_ops = {
};

static struct c2_cons_mesg c2_cons_disk_mesg = {
	.cm_name      = "Disk FOP Message",
	.cm_type      = CMT_DISK_FAILURE,
	.cm_fopt      = &c2_cons_fop_disk_fopt,
	.cm_item_ops  = &c2_rpc_item_cons_disk_ops,
	.cm_item_type = &c2_cons_fop_disk_fopt.ft_rpc_item_type,
};

/* Device FOP message */
/**
 * @brief RPC item operation for device failure notification.
 */
static const struct c2_rpc_item_ops c2_rpc_item_cons_device_ops = {
};

static struct c2_cons_mesg c2_cons_device_mesg = {
	.cm_name     = "Device FOP Message",
	.cm_type     = CMT_DEVICE_FAILURE,
	.cm_fopt     = &c2_cons_fop_device_fopt,
	.cm_item_ops = &c2_rpc_item_cons_device_ops,
	.cm_item_type = &c2_cons_fop_device_fopt.ft_rpc_item_type,
};


/* Reply FOP message */
/**
 * @brief RPC item operation for device failure notification.
 */
static const struct c2_rpc_item_ops c2_rpc_item_cons_reply_ops = {
};

static struct c2_cons_mesg c2_cons_reply_mesg = {
	.cm_name     = "Reply FOP Message",
	.cm_type     = CMT_REPLY_FAILURE,
	.cm_fopt     = &c2_cons_fop_reply_fopt,
	.cm_item_ops = &c2_rpc_item_cons_reply_ops,
	.cm_item_type = &c2_cons_fop_reply_fopt.ft_rpc_item_type,
};

/**
 * @brief Array holds refrences to console message types.
 */
static struct c2_cons_mesg *cons_mesg[] = {
	&c2_cons_disk_mesg,
	&c2_cons_device_mesg,
	&c2_cons_reply_mesg
};

void c2_cons_mesg_fop_show(struct c2_fop_type *fopt)
{
	struct c2_fop *fop;
	void	      *fdata;

	fop = c2_fop_alloc(fopt, NULL);
	if (fop != NULL) {
		fdata = c2_fop_data(fop);
		if (fdata != NULL)
			mesg_show(fop);
		else
			fprintf(stderr, "FOP data does not exist\n");
	 } else
		fprintf(stderr, "FOP allocation failed\n");
}

void c2_cons_mesg_list_show(void)
{
	struct c2_cons_mesg *mesg;
	int		     i;

	printf("List of FOP's: \n");
	for (i = 0; i < ARRAY_SIZE(cons_mesg); i++) {
		mesg = cons_mesg[i];
		C2_ASSERT(mesg->cm_type == i);
		c2_cons_mesg_name_print(mesg);
		printf("\n");
	}
}

struct c2_cons_mesg *c2_cons_mesg_get(enum c2_cons_mesg_type type)
{
	struct c2_cons_mesg *mesg;

	C2_PRE(type < ARRAY_SIZE(cons_mesg));
	mesg = cons_mesg[type];
	C2_POST(mesg->cm_type == type);

	return mesg;
}

int c2_cons_mesg_init(void)
{
	C2_CASSERT(ARRAY_SIZE(cons_mesg) == CMT_MESG_NR);
	return 0;
}

void c2_cons_mesg_fini(void)
{
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

