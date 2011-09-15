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

#include "lib/errno.h"
#include "lib/memory.h"
#include "fop/fop_format_def.h"
#include "fop/fop_iterator.h"
#include "rm_fops.h"
#include "rm_foms.h"

#include "rm/rm.ff"

/**
 * Forward declaration.
 */
int c2_rm_fop_borrow_fom_init(struct c2_fop *, struct c2_fom **);
int c2_rm_fop_revoke_fom_init(struct c2_fop *, struct c2_fom **);
int c2_rm_fop_cancel_fom_init(struct c2_fop *, struct c2_fom **);

/**
 * FOP operation vector for right borrow.
 */
struct c2_fop_type_ops c2_fop_rm_borrow_ops = {
	.fto_fom_init = c2_rm_fop_borrow_fom_init
};

/**
 * FOP operation vector for right borrow reply.
 */
struct c2_fop_type_ops c2_fop_rm_borrow_reply_ops = {
	.fto_fom_init = NULL,
};

/**
 * FOP operation vector for right revoke.
 */
struct c2_fop_type_ops c2_fop_rm_revoke_ops = {
	.fto_fom_init = c2_rm_fop_revoke_fom_init,
};

/**
 * FOP operation vector for right revoke reply.
 */
struct c2_fop_type_ops c2_fop_rm_revoke_reply_ops = {
	.fto_fom_init = NULL,
};

/**
 * FOP operation vector for right cancel.
 */
struct c2_fop_type_ops c2_fop_rm_cancel_ops = {
	.fto_fom_init = c2_rm_fop_cancel_fom_init,
};

static struct c2_fop_type *fops[] = {
	&c2_fop_rm_right_borrow_fopt,
	&c2_fop_rm_right_borrow_reply_fopt,
	&c2_fop_rm_right_revoke_fopt,
	&c2_fop_rm_right_revoke_reply_fopt,
	&c2_fop_rm_right_cancel_fopt,
};

static struct c2_fop_type_format *rm_fmts[] = {
	&c2_fop_rm_right_tfmt,
	&c2_fop_rm_res_data_tfmt,
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
 * FOP definitions for resource-right surrender.
 */
C2_FOP_TYPE_DECLARE(c2_fop_rm_right_cancel, "Right Surrender",
		    C2_RM_FOP_CANCEL, &c2_fop_rm_cancel_ops);

/**
 * Set the RM FOM ops vectors - constructor, destructor, other functions.
 *
 */
static void c2_rm_fom_set_ops(struct c2_rm_fom_right_request *fom,
			     enum c2_rm_fop_opcodes rm_fop_opcode)
{
	switch (rm_fop_opcode) {
	case C2_RM_FOP_BORROW:
		fom->frr_gen.fo_type = &c2_rm_fom_borrow_type;
		fom->frr_gen.fo_ops = &c2_rm_fom_borrow_ops;
		break;
	case C2_RM_FOP_REVOKE:
		fom->frr_gen.fo_type = &c2_rm_fom_revoke_type;
		fom->frr_gen.fo_ops = &c2_rm_fom_revoke_ops;
		break;
	case C2_RM_FOP_CANCEL:
		fom->frr_gen.fo_type = &c2_rm_fom_cancel_type;
		fom->frr_gen.fo_ops = &c2_rm_fom_cancel_ops;
		break;
	default:
		/* Not reached */
		break;
	}
}

/**
 * This is a generic constructor for RM FOMs.
 * This creates an instance of c2_rm_fom_right_request FOM.
 *
 * @pre fop != NULL
 * @pre fom != NULL
 *
 * @param *fop - incoming FOP
 * @param **fom - fom instance created. Memory is allocated by this function.
 * @param rm_fop_opcode - Indicates the FOP-type.
 *
 * @retval 0 - on success
 *         -EINVAL - Invalid FOM type
 *         -ENOMEM - out of memory.
 *
 * @see c2_rm_fom_right_request
 */
static int c2_rm_fop_generic_fom_init(struct c2_fop *fop, struct c2_fom **fom,
				      enum c2_rm_fop_opcodes rm_fop_opcode)
{
	struct c2_rm_fom_right_request *req_fom;
	bool check_reply_fop = false;

	C2_PRE(fop != NULL);
	C2_PRE(fom != NULL);

#if 0
	/* TODO - Make sure that FOM type is set */
	fom_type = c2_fom_type_map(fop->f_type->ft_code);
	if (fom_type == NULL)
		return -EINVAL;
#endif

	/* Allocate FOM object */
	C2_ALLOC_PTR(req_fom);
	if (req_fom == NULL)
		return -ENOMEM;

	/* Allocate reply FOP, if applicable */
	switch (rm_fop_opcode) {
	case C2_RM_FOP_BORROW:
		req_fom->frr_reply_fop
		= c2_fop_alloc(&c2_fop_rm_right_borrow_reply_fopt, NULL);
		check_reply_fop = true;
		break;
	case C2_RM_FOP_REVOKE:
		req_fom->frr_reply_fop
		= c2_fop_alloc(&c2_fop_rm_right_borrow_reply_fopt, NULL);
		check_reply_fop = true;
		break;
	default:
		req_fom->frr_reply_fop = NULL;
		break;
	}


	if (check_reply_fop && req_fom->frr_reply_fop == NULL) {
		c2_free(req_fom);
		return -ENOMEM;
	}

	/* Save FOP pointer for future reference */
	req_fom->frr_fop = fop;

	/* Set various ops vectors */
	c2_rm_fom_set_ops(req_fom, rm_fop_opcode);

	*fom = &req_fom->frr_gen;
	return 0;
}

/**
 * FOM initialization function invoked by request handler.
 * This is a right borrow FOM constructor.
 * This creates an instance of c2_rm_fom_right_request FOM.
 *
 * @param *fop - incoming FOP
 * @param **fom - fom instance created. Memory is allocated by this function.
 *
 * @retval 0 - on success
 *         -EINVAL - Invalid FOM type
 *         -ENOMEM - out of memory.
 *
 * @see c2_rm_fom_right_request
 */
int c2_rm_fop_borrow_fom_init(struct c2_fop *fop, struct c2_fom **fom)
{
	int rc;

	rc = c2_rm_fop_generic_fom_init(fop, fom, C2_RM_FOP_BORROW);
	return rc;
}

#if 0
/**
 * This reply function will be called by RPC layer.
 * This will be in response to borrow request.
 *
 * @param *req - Request FOP - Borrow request
 * @param *fom - Reply FOP - Borrow reply
 * @param rc - return code
 *
 */
void c2_rm_fop_borrow_reply(struct c2_rpc_item *req,
			    struct c2_rpc_item *reply,
			    int rc)
{
	struct c2_fop_rm_right_borrow_reply *brep_fop;

	C2_PRE(req != NULL && reply != NULL);

	brep_fop = c2_rpc_item_to_fop(reply);
	brep_fop = c2_fop_data(brep_fop);
	/* TODO - Figure out how RM-generic wants to be notified */
}
#endif

/**
 * FOM initialization function invoked by request handler.
 * This creates an instance of c2_rm_fom_right_request FOM.
 * This is a right revoke FOM constructor.
 *
 * @param *fop - incoming FOP
 * @param **fom - fom instance created. Memory is allocated by this function.
 *
 * @retval 0 - on success
 *         -EINVAL - Invalid FOM type
 *         -ENOMEM - out of memory.
 *
 * @see c2_rm_fom_right_request
 */
int c2_rm_fop_revoke_fom_init(struct c2_fop *fop, struct c2_fom **fom)
{
	int rc;

	rc = c2_rm_fop_generic_fom_init(fop, fom, C2_RM_FOP_REVOKE);
	return rc;
}

#if 0
/**
 * This reply function will be called by RPC layer.
 * This will be in response to revoke request.
 *
 * @param *req - Request FOP - Revoke request
 * @param *fom - Reply FOP - Revoke reply
 * @param rc - return code
 *
 */
void c2_rm_fop_revoke_reply(struct c2_prc_item *req,
			    struct c2_rpc_item *reply,
			    int rc)
{
	struct c2_fop_rm_right_revoke_reply *rrep_fop;

	C2_PRE(req != NULL && reply != NULL);

	rrep_fop = c2_rpc_item_to_fop(reply);
	rrep_fop = c2_fop_data(rrep_fop);
	/* TODO - Figure out how RM-generic wants to be notified */
}
#endif

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
 *
 * @see c2_rm_fom_right_request
 */
int c2_rm_fop_cancel_fom_init(struct c2_fop *fop, struct c2_fom **fom)
{
	int rc;

	rc = c2_rm_fop_generic_fom_init(fop, fom, C2_RM_FOP_CANCEL);
	return rc;
}

void c2_rm_fop_fini(void)
{
	c2_fop_object_fini();
	c2_fop_type_fini_nr(fops, ARRAY_SIZE(fops));
}

/**
 * Initializes RM fops.
 *
 * @retval 0 - on success
 *         non-zero - on failure
 *
 * @see rm_fop_fini()
 */
int c2_rm_fop_init(void)
{
	int rc;

	/* Parse RM defined types */
	rc = c2_fop_type_format_parse_nr(rm_fmts, ARRAY_SIZE(rm_fmts));
	if (rc == 0) {
		rc = c2_fop_type_build_nr(fops, ARRAY_SIZE(fops));
		if (rc == 0) {
			/* Initialize RM defined types */
			c2_fop_object_init(&c2_fop_rm_right_tfmt);
			c2_fop_object_init(&c2_fop_rm_res_data_tfmt);
		}
	}

	if (rc != 0)
		c2_rm_fop_fini();

	return rc;
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
