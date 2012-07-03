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
#include "fop/fop_item_type.h"
#include "fop/fop_iterator.h"
#include "rm_fops.h"
#include "xcode/bufvec_xcode.h"
#include "rpc/rpc_base.h"
#include "rpc/rpc_opcodes.h"
#include "rpc/rpc2.h"
#include "rm/rm.h"
#include "rm/rm_internal.h"

#include "rm/rm.ff"

/*
 * Data structures.
 */
/*
 * Tracking structure for outgoing request.
 */
struct rm_out {
	struct c2_rm_outgoing  ou_req;
	struct c2_fop	       ou_fop;
};

/**
 * Forward declaration.
 */
static void borrow_reply(struct c2_rpc_item *);
static void outreq_free(struct c2_rpc_item *);
static void revoke_reply(struct c2_rpc_item *);

/**
 * FOP operation vector for right borrow.
 */
static const struct c2_fop_type_ops rm_borrow_fop_ops = {
	.fto_size_get = c2_xcode_fop_size_get,
};

const struct c2_rpc_item_ops rm_borrow_rpc_ops = {
	.rio_replied = borrow_reply,
	.rio_free = outreq_free,
};

/**
 * FOP operation vector for right revoke.
 */
static const struct c2_fop_type_ops rm_revoke_fop_ops = {
	.fto_size_get = c2_xcode_fop_size_get,
};

const struct c2_rpc_item_ops rm_revoke_rpc_ops = {
	.rio_replied = revoke_reply,
	.rio_free = outreq_free,
};

static struct c2_fop_type *fops[] = {
	&c2_fop_rm_borrow_fopt,
	&c2_fop_rm_borrow_rep_fopt,
	&c2_fop_rm_revoke_fopt,
	&c2_fop_rm_revoke_rep_fopt,
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
C2_FOP_TYPE_DECLARE(c2_fop_rm_borrow, "Right Borrow", &rm_borrow_fop_ops,
		    C2_RM_FOP_BORROW, C2_RPC_ITEM_TYPE_REQUEST);
C2_FOP_TYPE_DECLARE(c2_fop_rm_borrow_rep, "Right Borrow Reply",
		    &rm_borrow_fop_ops, C2_RM_FOP_BORROW_REPLY,
		    C2_RPC_ITEM_TYPE_REPLY);

/**
 * FOP definitions for resource-right revoke,cancel request and reply.
 */
C2_FOP_TYPE_DECLARE(c2_fop_rm_revoke, "Right Revoke", &rm_revoke_fop_ops,
		    C2_RM_FOP_REVOKE, C2_RPC_ITEM_TYPE_REQUEST);
C2_FOP_TYPE_DECLARE(c2_fop_rm_revoke_rep, "Right Revoke Reply",
		    &rm_revoke_fop_ops, C2_RM_FOP_REVOKE_REPLY,
		    C2_RPC_ITEM_TYPE_REPLY);

int c2_rm_borrow_out(struct c2_rm_incoming *in,
		     struct c2_rm_right *right)
{
	struct rm_out		*outreq;
	struct c2_fop_rm_borrow *bfop;
	struct c2_fop		*fop = &outreq->ou_fop;
	struct c2_rm_cookie	*cookie;
	struct c2_rm_cookie	 dcookie;
	int			 rc;

	C2_PRE(in->rin_type == RIT_BORROW);

	C2_ALLOC_PTR(outreq);
	if (outreq == NULL)
		goto out;

	c2_rm_outgoing_init(&outreq->ou_req, in->rin_type, right);

	c2_fop_init(fop, &c2_fop_rm_borrow_fopt, NULL);
	pin_add(in, &outreq->ou_req.rog_want.rl_right, RPF_TRACK);

	bfop = c2_fop_data(fop);

	/* Fill up the BORROW FOP. */
	bfop->bo_policy = in->rin_policy;
	bfop->bo_flags = in->rin_flags;

	/* Copy creditor cookie */
	cookie = &in->rin_want.ri_owner->ro_creditor->rem_cookie;
	bfop->bo_creditor.ow_cookie.co_hi = cookie->cv.u_hi;
	bfop->bo_creditor.ow_cookie.co_lo = cookie->cv.u_lo;

	/* Copy debtor cookie */
	c2_rm_owner_cookie_get(in->rin_want.ri_owner, &dcookie);
	bfop->bo_debtor.ow_cookie.co_hi = dcookie.cv.u_hi;
	bfop->bo_debtor.ow_cookie.co_lo = dcookie.cv.u_lo;

	//c2_rm_loan_cookie_get(outreq->rog_want, &bfop->bo_loan.ow_cookie);
	/*
	 * Encode rights data into BORROW FOP
	 */
	rc = c2_rm_rdatum2buf(right,
			      (void **)&bfop->bo_right.ri_opaque.op_bytes, 
			      &bfop->bo_right.ri_opaque.op_nr);
	if (rc != 0)
		return rc;

	outreq->ou_fop.f_item.ri_ops = &rm_borrow_rpc_ops;
	c2_rpc_post(&outreq->ou_fop.f_item);

	return 0;

out:
	return -ENOMEM;
}

static void borrow_reply(struct c2_rpc_item *item)
{
	struct c2_fop_rm_borrow_rep *borrow_reply;
	struct c2_rm_outgoing	    *og;
	struct rm_out		    *outreq;
	struct c2_fop		    *reply_fop;
	struct c2_fop		    *fop;

	C2_PRE(item != NULL);
	C2_PRE(item->ri_reply != NULL);

	reply_fop = c2_rpc_item_to_fop(item->ri_reply);
	borrow_reply = c2_fop_data(reply_fop);
	fop = c2_rpc_item_to_fop(item);
	outreq = container_of(fop, struct rm_out, ou_fop);
	og = &outreq->ou_req;

	C2_ASSERT(og != NULL);

	og->rog_rc = item->ri_error ? item->ri_error : borrow_reply->br_rc;

	if (og->rog_rc == 0) {
		og->rog_want.rl_id = borrow_reply->br_loan.lo_id;
	//	TODO - Check reply processing.
	//	og->rog_want.rl_other = owner->ro_creditor;
		c2_rm_buf2rdatum(&og->rog_want.rl_right,
				 borrow_reply->br_right.ri_opaque.op_bytes,
				 borrow_reply->br_right.ri_opaque.op_nr);
	}
	c2_rm_outgoing_complete(og);
}

static void outreq_free(struct c2_rpc_item *item)
{
	struct rm_out *out;
	struct c2_fop *fop;

	C2_PRE(item != NULL);

	fop =  c2_rpc_item_to_fop(item);
	out = container_of(fop, struct rm_out, ou_fop);
	c2_fop_item_free(item);
	c2_free(out);
}

int c2_rm_revoke_out(struct c2_rm_incoming *in,
		     struct c2_rm_loan *loan,
		     struct c2_rm_right *right)
{
	struct rm_out		*outreq;
	struct c2_fop_rm_revoke *rfop;
	struct c2_fop		*fop = &outreq->ou_fop;
	struct c2_rm_cookie	*ocookie;
	struct c2_rm_cookie	 lcookie;
	int			 rc;

	C2_PRE(in->rin_type == RIT_REVOKE);

	C2_ALLOC_PTR(outreq);
	if (outreq == NULL)
		goto out;

	c2_rm_outgoing_init(&outreq->ou_req, ROT_REVOKE, right);

	c2_fop_init(fop, &c2_fop_rm_revoke_fopt, NULL);
	pin_add(in, &outreq->ou_req.rog_want.rl_right, RPF_TRACK);

	rfop = c2_fop_data(fop);

	/* Fill up the REVOKE FOP. */
	rfop->rr_op = in->rin_type;
	rfop->rr_loan.lo_id = loan->rl_id;

	/* Get the loan cookie and tehn copy it */
	c2_rm_loan_cookie_get(loan, &lcookie);
	rfop->rr_loan.lo_cookie.co_hi = lcookie.cv.u_hi;
	rfop->rr_loan.lo_cookie.co_lo = lcookie.cv.u_lo;

	/*
	 * This will help the other end to identify the owner structure.
	 */
	ocookie = &loan->rl_other->rem_cookie;
	rfop->rr_debtor.ow_cookie.co_hi = ocookie->cv.u_hi;
	rfop->rr_debtor.ow_cookie.co_lo = ocookie->cv.u_lo;

	/*
	 * Encode rights data into REVOKE FOP
	 */
	rc = c2_rm_rdatum2buf(right,
			      (void **)&rfop->rr_right.ri_opaque.op_bytes, 
			      &rfop->rr_right.ri_opaque.op_nr);
	if (rc != 0)
		return rc;

	outreq->ou_fop.f_item.ri_ops = &rm_revoke_rpc_ops;
	c2_rpc_post(&outreq->ou_fop.f_item);

	return 0;

out:
	return -ENOMEM;
}

static void revoke_reply(struct c2_rpc_item *item)
{
	struct c2_fop_rm_revoke_rep *revoke_reply;
	struct c2_rm_outgoing	    *og;
	struct rm_out		    *outreq;
	struct c2_fop		    *reply_fop;
	struct c2_fop		    *fop;

	C2_PRE(item != NULL);
	C2_PRE(item->ri_reply != NULL);

	reply_fop = c2_rpc_item_to_fop(item->ri_reply);
	revoke_reply = c2_fop_data(reply_fop);
	fop = c2_rpc_item_to_fop(item);
	outreq = container_of(fop, struct rm_out, ou_fop);
	og = &outreq->ou_req;

	C2_ASSERT(og != NULL);


	/*
	 * Is there a partial revoke? If yes, we have to copy the remaining
	 * right.
	 */
	og->rog_rc = item->ri_error ? item->ri_error : revoke_reply->re_rc;
	c2_rm_outgoing_complete(og);
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
