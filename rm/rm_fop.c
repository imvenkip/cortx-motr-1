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
 * Original author: Rajesh Bhalerao <Rajesh_Bhalerao@xyratex.com>
 * Original creation date: 07/18/2011
 */

#include "rm_fop.h"

/**
 * FOP operation vector for right borrow.
 */
sruct c2_fop_type_ops c2_fop_rm_borrow_ops = {
	.fto_fom_init = c2_rm_fop_borrow_fom_init
};

/**
 * FOP operation vector for right borrow reply.
 */
sruct c2_fop_type_ops c2_fop_rm_borrow_reply_ops = {
	.fto_fom_init = c2_rm_fop_borrow_reply_fom_init
};

/**
 * FOP operation vector for right revoke.
 */
sruct c2_fop_type_ops c2_fop_rm_revoke_ops = {
	.fto_fom_init = c2_rm_fop_revoke_fom_init,
};

/**
 * FOP operation vector for right revoke reply.
 */
sruct c2_fop_type_ops c2_fop_rm_revoke_reply_ops = {
	.fto_fom_init = c2_rm_fop_revoke_reply_fom_init,
};

/**
 * FOP operation vector for right cancel.
 */
sruct c2_fop_type_ops c2_fop_rm_revoke_reply_ops = {
	.fto_fom_init = c2_rm_fop_cancel_fom_init,
};

/**
 * FOP definitions for resource-right borrow request and reply.
 */
C2_FOP_TYPE_DECLARE(c2_fop_rm_right_borrow, "Right Borrow",
		    C2_RM_FOP_BORROW, &c2_fop_rm_borrow_ops);
C2_FOP_TYPE_DECLARE(c2_fop_rm_right_borrow_reply, "Right Borrow Reply",
		    C2_RM_FOP_BORROW_REPLY, &c2_fop_rm_borrow_reply_ops);

/**
 * FOP definitions for resource-right revoke request and reply.
 */
C2_FOP_TYPE_DECLARE(c2_fop_rm_right_revoke, "Right Revoke",
		    C2_RM_FOP_REVOKE, &c2_fop_rm_revoke_ops);
C2_FOP_TYPE_DECLARE(c2_fop_rm_right_revoke_reply, "Right Revoke Reply",
		    C2_RM_FOP_REVOKE_REPLY, &c2_fop_rm_revoke_reply_ops);

/**
 * FOP definitions for resource-surrender.
 */
C2_FOP_TYPE_DECLARE(c2_fop_rm_right_cancel, "Right Surrender",
		    C2_RM_FOP_CANCEL, &c2_fop_rm_cancel_ops);

/**
 * FOM initialization function invoked by request handler.
 * This creates an instance of c2_rm_fom_right_request FOM.
 *
 * @param *fop - incoming FOP
 * @param **fom - fom instance created. Memory is allocated by this function.
 *
 * @retval 0 - on success
 *         -EINVAL - Invalid FOM type
 *         -ENOMEM - out of memory.
 */
int c2_rm_fop_borrow_fom_init(struct c2_fop *fop, struct c2_fom **fom)
{
}

/**
 * FOM initialization function invoked by request handler.
 * This creates an instance of c2_rm_fom_right_request FOM.
 *
 * @param *fop - incoming FOP
 * @param **fom - fom instance created. Memory is allocated by this function.
 *
 * @retval 0 - on success
 *         -EINVAL - Invalid FOM type
 *         -ENOMEM - out of memory.
 */
int c2_rm_fop_borrow_reply_fom_init(struct c2_fop *fop, struct c2_fom **fom)
{
}

/**
 * FOM initialization function invoked by request handler.
 * This creates an instance of c2_rm_fom_right_request FOM.
 *
 * @param *fop - incoming FOP
 * @param **fom - fom instance created. Memory is allocated by this function.
 *
 * @retval 0 - on success
 *         -EINVAL - Invalid FOM type
 *         -ENOMEM - out of memory.
 */
int c2_rm_fop_revoke_fom_init(struct c2_fop *fop, struct c2_fom **fom)
{
}

/**
 * FOM initialization function invoked by request handler.
 * This creates an instance of c2_rm_fom_right_request FOM.
 *
 * @param *fop - incoming FOP
 * @param **fom - fom instance created. Memory is allocated by this function.
 *
 * @retval 0 - on success
 *         -EINVAL - Invalid FOM type
 *         -ENOMEM - out of memory.
 */
int c2_rm_fop_revoke_reply_fom_init(struct c2_fop *fop, struct c2_fom **fom)
{
}

/**
 * FOM initialization function invoked by request handler.
 * This creates an instance of c2_rm_fom_right_request FOM.
 *
 * @param *fop - incoming FOP
 * @param **fom - fom instance created. Memory is allocated by this function.
 *
 * @retval 0 - on success
 *         -EINVAL - Invalid FOM type
 *         -ENOMEM - out of memory.
 */
int c2_rm_fop_cancel_fom_init(struct c2_fop *fop, struct c2_fom **fom)
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
