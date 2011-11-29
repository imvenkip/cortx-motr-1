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

#ifndef __COLIBRI_CONSOLE_MESG_H__
#define __COLIBRI_CONSOLE_MESG_H__

#include "fop/fop.h"
#include "rpc/rpc2.h"
#include "rpc/session.h"

struct c2_cons_mesg;

/**
 * @enum c2_cons_mesg_type
 * @brief Console Notification types.
 */
enum c2_cons_mesg_type {
        CMT_DISK_FAILURE,	/**< Disk failure. */
        CMT_DEVICE_FAILURE,	/**< Device failure */
        CMT_REPLY_FAILURE,	/**< Reply to Device failure */
	CMT_MESG_NR
};

/**
 * @brief c2_cons_mesg represents console message. It has fop message
 *	  and all information required to send rpc item.
 */
struct c2_cons_mesg {
	/** Message Name */
	const char		*cm_name;
	/** Message type i.e disk or device failure */
	enum c2_cons_mesg_type	 cm_type;
	/** Console message operations */
	const struct c2_cons_mesg_ops *cm_ops;
	/** fop message to be send using console */
	struct c2_fop		*cm_fop;
	/** fop type to be send using console */
	struct c2_fop_type	*cm_fopt;
	/** rpc item operation */
	const struct c2_rpc_item_ops  *cm_item_ops;
	/** RPC item type */
	struct c2_rpc_item_type *cm_item_type;
	/** RPC machine through which mesg to be send */
	struct c2_rpcmachine	*cm_rpc_mach;
	/** RPC session */
	struct c2_rpc_session	*cm_rpc_session;
};

/**
 *  Prints name and type of console message.
 *  It can be used to print more info if required.
 */
void c2_cons_mesg_name_print(const struct c2_cons_mesg *mesg);

/**
 * @brief Builds and send message using rpc_post and waits for reply.
 *
 * @param mesg console message
 *
 * @return 0 success, !0 failure.
 */
int c2_cons_mesg_send(struct c2_cons_mesg *mesg, c2_time_t deadline);

/**
 *  @brief Iterate over FOP fields and print names.
 */
void c2_cons_mesg_fop_show(struct c2_fop_type *fopt);

/**
 * @brief Helper function to print list of FOPs.
 */
void c2_cons_mesg_list_show(void);

/**
 * @brief returns the consle message refrence specific to type.
 *
 * @param type console message type.
 *
 * @return c2_cons_mesg ref. or NULL
 */
struct c2_cons_mesg *c2_cons_mesg_get(enum c2_cons_mesg_type type);

/**
 * @brief Init console message subsystem. Curently it
 *	  only check for cons_mesg size and assert.
 *
 * @return 0 success, -errno failure.
 */
int c2_cons_mesg_init(void);

/**
 * @brief Fini console message subsystem. Currently it
 *	  does nothing.
 */
void c2_cons_mesg_fini(void);

/* __COLIBRI_CONSOLE_MESG_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

