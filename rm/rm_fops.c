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

/*
 * Data structures.
 */
/*
 * Common data-structure to other tracking structures.
 */
struct rm_out {
	struct c2_rm_incoming *ou_incoming;
	struct c2_rm_outgoing  ou_req;
	struct c2_fop	       ou_fop;
};

/*
 * The tracking structure for BORROW request.
 */
struct rm_borrow {
	struct rm_out		bo_out;
	struct c2_fop_rm_borrow *bo_data;
};

/*
 * The tracking structure for BORROW request.
 */
struct rm_canoke {
	struct rm_out		 bo_out;
	struct c2_fop_rm_canoke *bo_data;
};

/**
 * Forward declaration.
 */
int c2_rm_fop_borrow_fom_init(struct c2_fop *, struct c2_fom **);
int c2_rm_fop_revoke_fom_init(struct c2_fop *, struct c2_fom **);
int c2_rm_fop_cancel_fom_init(struct c2_fop *, struct c2_fom **);

/**
 * FOP operation vector for right borrow.
 */
static const struct c2_fop_type_ops rm_borrow_fop_ops = {
	.fto_fop_replied = rm_borrow_fop_reply,
	.fto_size_get = c2_xcode_fop_size_get,
};

/**
 * FOP operation vector for right revoke or cancel.
 */
static const struct c2_fop_type_ops rm_canoke_fop_ops = {
	.fto_execute = rm_client_revoke,
	.fto_fop_replied = rm_canoke_fop_reply,
	.fto_size_get = c2_xcode_fop_size_get,
};

/**
 * FOP operation vector for right revoke.
 */
struct c2_fop_type_ops c2_fop_rm_revoke_ops = {
	.fto_fom_init = c2_rm_fop_canoke_fom_init,
};

/**
 * FOP operation vector for right revoke reply.
 */
struct c2_fop_type_ops c2_fop_rm_canoke_reply_ops = {
	.fto_fom_init = NULL,
};

/**
 * FOP operation vector for right cancel.
 */
struct c2_fop_type_ops c2_fop_rm_cancel_ops = {
	.fto_fom_init = c2_rm_fop_cancel_fom_init,
};

static struct c2_fop_type *fops[] = {
	&c2_fop_rm_borrow_fopt,
	&c2_fop_rm_borrow_rep_fopt,
	&c2_fop_rm_canoke_fopt,
	&c2_fop_rm_canoke_rep_fopt,
};

static struct c2_fop_type_format *rm_fmts[] = {
	&c2_fop_rm_cookie_tfmt,
	&c2_fop_rm_opaque_tfmt,
	&c2_fop_rm_owner_tfmt,
	&c2_fop_rm_loan_tfmt,
	&c2_fop_rm_right_tfmt,
};

/**
 * FOP definitions for resource-right borrow request and reply.
 */
C2_FOP_TYPE_DECLARE(c2_fop_rm_borrow, "Right Borrow", &c2_fop_rm_borrow_ops,
		    C2_RM_FOP_BORROW, &c2_fop_rm_borrow_ops);
C2_FOP_TYPE_DECLARE(c2_fop_rm_borrow_rep, "Right Borrow Reply",
		    C2_RM_FOP_BORROW_REPLY, &c2_fop_rm_borrow_reply_ops);

/**
 * FOP definitions for resource-right revoke,cancel request and reply.
 */
C2_FOP_TYPE_DECLARE(c2_fop_rm_canoke, "Right RevokeCancel",
		    C2_RM_FOP_REVOKE, &c2_fop_rm_canoke_ops);
C2_FOP_TYPE_DECLARE(c2_fop_rm_canoke_rep, "Right RevokeCancel Reply",
		    C2_RM_FOP_REVOKE_REPLY, &c2_fop_rm_canoke_reply_ops);

int c2_rm_borrow_out(struct c2_rm_incoming *in,
		     struct c2_rm_right *right)
{
	struct rm_borrow	*borrow;
	struct c2_rm_outgoing	*outreq;
	struct c2_fop_rm_borrow *bfop = &borrow->bo_data;
	char			*right_addr = (char *)bfop->bo_right.ri_opaque;
	c2_bcount_t		 right_nr = 1;
	struct c2_bufvec	 right_buf = C2_BUFVEC_INIT_BUF(&right_addr,
								&right_nr);

	C2_PRE(in->rin_type == RIT_BORROW);

	C2_ALLOC_PTR(borrow);
	if (borrow == NULL)
		goto out;

	/* Store the incoming request pointer */
	borrow->bo_out.ou_incoming = in;

	/* Store the outgoing request information */
	outreq = &borrow->bo_out.ou_req;
	outreq->rog_type  = ROT_BORROW;
	outreq->rog_owner = right->ri_owner;
	right_copy(&outreq->rog_want.rl_right, right);
	outreq->rog_want.rl_other = right->ri_owner->ro_creditor;

	c2_fop_init(&borrow->bo_out.ou_fop, &c2_fop_rm_borrow_fopt, NULL);
	/*
	 * pin_add should return pin, otherwise it's difficult to remove it.
	 * Or outgoing should store pointer to incoming. The outgoing request
	 * is generated as a result of incoming request.
	 */
	pin_add(in, &outreq->rog_want.rl_right, RPF_TRACK);

	/* Fill in the BORROW FOP. Should we store pointer in borrow?? */
	bfop = &borrow->bo_data;

	bfop->bo_rtype = in->rin_type;
	bfop->bo_policy = in->rin_policy;
	bfop->bo_flags = in->rin_flags;
	c2_rm_owner_cookie(in->rin_want.ri_owner, &bfop->bo_owner.ow_cookie);
	/* Sending owner cookie is not necessary. Think of getting rid of it */
	c2_rm_loan_cookie(outreq->rog_want, &bfop->bo_loan.ow_cookie);
	/*
	 * Encode rights data into BORROW FOP
	 */
	right->ri_ops->rro_encode(right, &right_buf);
	borrow->bo_out.ou_fop.f_item->ri_ops = &borrow_ops;
	c2_rpc_post(&borrow->bo_out.ou_fop.f_item);

	return 0;

out:
	return -ENOMEM;
}

int c2_rm_revoke_out(struct c2_rm_incoming *in,
		     struct c2_rm_loan *loan,
		     struct c2_rm_right *right)
{
	struct rm_canoke	*revoke;
	struct c2_rm_outgoing	*outreq;
	struct c2_fop_rm_borrow *rfop = &borrow->bo_data;
	char			*right_addr = (char *)rfop->cr_right.ri_opaque;
	c2_bcount_t		 right_nr = 1;
	struct c2_bufvec	 right_buf = C2_BUFVEC_INIT_BUF(&right_addr,
								&right_nr);

	C2_PRE(in->rin_type == RIT_REVOKE);

	C2_ALLOC_PTR(revoke);
	if (revoke == NULL)
		goto out;

	/* Store the incoming request pointer */
	revoke->bo_out.ou_incoming = in;

	/* Store the outgoing request information */
	outreq = &borrow->bo_out.ou_req;
	outreq->rog_type  = ROT_REVOKE;
	outreq->rog_owner = right->ri_owner;
	right_copy(&outreq->rog_want.rl_right, right);
	outreq->rog_want.rl_other = right->ri_owner->ro_creditor;

	c2_fop_init(&borrow->bo_out.ou_fop, &c2_fop_rm_canoke_fopt, NULL);
	/*
	 * pin_add should return pin, otherwise it's difficult to remove it.
	 * Or outgoing should store pointer to incoming. The outgoing request
	 * is generated as a result of incoming request.
	 */
	pin_add(in, &outreq->rog_want.rl_right, RPF_TRACK);

	/* Fill in the REVOKE FOP. */
	rfop = &revoke->bo_data;

	rfop->cr_op = RIT_REVOKE;
	c2_rm_owner_cookie(in->rin_want.ri_owner, &rfop->cr_owner.ow_cookie);
	/* Sending owner cookie is not necessary. Think of getting rid of it */
	c2_rm_loan_cookie(outreq->rog_want, &rfop->cr_loan.ow_cookie);
	/*
	 * Encode rights data into REVOKE FOP. This is valuable for
	 * partial revoke? Otherwise loan-id should suffice?
	 */
	right->ri_ops->rro_encode(right, &right_buf);
	borrow->bo_out.ou_fop.f_item->ri_ops = &revoke_ops;
	c2_rpc_post(&revoke->bo_out.ou_fop.f_item);

	return 0;

out:
	return -ENOMEM;
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
			c2_fop_object_init(&c2_fop_rm_cookie_tfmt);
			c2_fop_object_init(&c2_fop_rm_opaque_tfmt);
			c2_fop_object_init(&c2_fop_rm_owner_tfmt);
			c2_fop_object_init(&c2_fop_rm_loan_tfmt);
			c2_fop_object_init(&c2_fop_rm_right_tfmt);
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
