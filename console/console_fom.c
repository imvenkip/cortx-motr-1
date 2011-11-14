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
/*
 * Disk FOM processing
 */
static int fom_disk_state(struct c2_fom *fom)
{
        struct c2_cons_fom_disk  *disk_fom;
	struct c2_cons_fop_disk  *disk_fop;
        struct c2_cons_fop_reply *reply_fop;
        struct c2_rpc_item       *reply_item;
        struct c2_rpc_item       *req_item;
	int			  rc;

	/* Request FOM */
        disk_fom = container_of(fom, struct c2_cons_fom_disk, disk_gen);
	C2_ASSERT(disk_fom != NULL);

	/* Request FOP */
	disk_fop = c2_fop_data(disk_fom->disk_fop);
	C2_ASSERT(disk_fop != NULL);

	/* Reply fop */
        reply_fop = c2_fop_data(disk_fom->disk_reply_fop);
	C2_ASSERT(reply_fop != NULL);
	reply_fop->cons_notify_type = CMT_DISK_FAILURE;
	reply_fop->cons_return = C2_CONS_FOP_DISK_OPCODE;

	/* Request item */
        req_item = c2_fop_to_rpc_item(disk_fom->disk_fop);
	C2_ASSERT(req_item != NULL);

	/* Reply item */
        reply_item = c2_fop_to_rpc_item(disk_fom->disk_reply_fop);
	C2_ASSERT(reply_item != NULL);
        c2_rpc_item_init(reply_item);
        reply_item->ri_type =
	&disk_fom->disk_reply_fop->f_type->ft_rpc_item_type;
        reply_item->ri_group = NULL;

        rc = c2_rpc_reply_post(req_item, reply_item);
	fom->fo_phase = FOPH_FINISH;

	return rc;
}

const struct c2_fom_ops c2_cons_fom_disk_ops = {
        .fo_state	  = fom_disk_state,
	.fo_fini	  = default_fom_fini,
	.fo_home_locality = home_locality,
};

static struct c2_fom_type_ops c2_cons_fom_disk_type_ops = {
        .fto_create = NULL
};

struct c2_fom_type c2_cons_fom_disk_type = {
        .ft_ops = &c2_cons_fom_disk_type_ops
};

/*
 * Device FOM processing
 */
static int fom_device_state(struct c2_fom *fom)
{
        struct c2_cons_fom_device  *dev_fom;
        struct c2_cons_fop_device  *dev_fop;
        struct c2_cons_fop_reply   *reply_fop;
        struct c2_rpc_item         *reply_item;
        struct c2_rpc_item         *req_item;
	int			    rc;

        /* Request FOM */
        dev_fom = container_of(fom, struct c2_cons_fom_device, dev_gen);
        C2_ASSERT(dev_fom != NULL);

        /* Request FOP */
        dev_fop = c2_fop_data(dev_fom->dev_fop);
        C2_ASSERT(dev_fop != NULL);

        /* Reply fop */
        reply_fop = c2_fop_data(dev_fom->dev_reply_fop);
        C2_ASSERT(reply_fop != NULL);
        reply_fop->cons_notify_type = CMT_DEVICE_FAILURE;
        reply_fop->cons_return = C2_CONS_FOP_DEVICE_OPCODE;

        /* Request item */
        req_item = c2_fop_to_rpc_item(dev_fom->dev_fop);
        C2_ASSERT(req_item != NULL);

        /* Reply item */
        reply_item = c2_fop_to_rpc_item(dev_fom->dev_reply_fop);
        C2_ASSERT(reply_item != NULL);
        c2_rpc_item_init(reply_item);
        reply_item->ri_type =
	&dev_fom->dev_reply_fop->f_type->ft_rpc_item_type;
        reply_item->ri_group = NULL;

        rc = c2_rpc_reply_post(req_item, reply_item);
	fom->fo_phase = FOPH_FINISH;

	return rc;
}

const struct c2_fom_ops c2_cons_fom_device_ops = {
        .fo_state	  = fom_device_state,
	.fo_fini	  = default_fom_fini,
	.fo_home_locality = home_locality,
};

static struct c2_fom_type_ops c2_cons_fom_device_type_ops = {
        .fto_create = NULL
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

