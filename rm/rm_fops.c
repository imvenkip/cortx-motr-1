/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
#include "rpc/item.h"
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
 * FOP definitions for resource-right revoke request and reply.
 */
C2_FOP_TYPE_DECLARE(c2_fop_rm_revoke, "Right Revoke", &rm_revoke_fop_ops,
		    C2_RM_FOP_REVOKE, C2_RPC_ITEM_TYPE_REQUEST);
C2_FOP_TYPE_DECLARE(c2_fop_rm_revoke_rep, "Right Revoke Reply",
		    &rm_revoke_fop_ops, C2_RM_FOP_REVOKE_REPLY,
		    C2_RPC_ITEM_TYPE_REPLY);

/*
 * Allocate and initialize remote request tracking structure.
 */
static int rm_out_create(struct rm_out **out, struct c2_rm_incoming *in,
			 struct c2_rm_loan *loan, struct c2_rm_right *right)
{
	struct c2_rm_loan *outloan;
	struct rm_out	  *outreq;
	int		   rc = 0;

	C2_PRE (out != NULL);

	C2_ALLOC_PTR(outreq);
	if (outreq == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	/*
	 * In case of BORROW, there is no existing loan. Allocate it here.
	 * If BORROW request completes successfully, the loan structure will
	 * be added to the borrowed and owned lists.
	 *
	 * Whereas REVOKE request has a loan (to be revoked). After successful
	 * completion of REVOKE, move the loan to the owned list.
	 */
	if (loan == NULL) {
		C2_ALLOC_PTR(outloan);
		if (outloan == NULL) {
			c2_free(outreq);
			rc = -ENOMEM;
			goto out;
		}
		rc = c2_rm_loan_init(loan, right);
		if (rc != 0) {
			c2_free(outloan);
			c2_free(outreq);
			rc = -ENOMEM;
			goto out;
		};
	} else
		outloan = loan;

	c2_rm_outgoing_init(&outreq->ou_req, in->rin_type, right->ri_owner);
	outreq->ou_req.rog_want = outloan;
	*out = outreq;

out:
	return rc;
}

/*
 * De-allocate the remote request tracking structure.
 */
static void rm_out_fini(struct rm_out *out)
{
	c2_free(out->ou_req.rog_want);
	c2_free(out);
}

int c2_rm_borrow_out(struct c2_rm_incoming *in,
		     struct c2_rm_right *right)
{
	struct c2_fop_rm_borrow *bfop;
	struct rm_out		*outreq;
	struct c2_fop		*fop;
	struct c2_cookie	*cookie;
	struct c2_cookie	 dcookie;
	int			 rc;

	C2_PRE(in->rin_type == C2_RIT_BORROW);

	rc = rm_out_create(&outreq, in, NULL, right);
	if (rc != 0)
		goto out;

	fop = &outreq->ou_fop;
	c2_fop_init(fop, &c2_fop_rm_borrow_fopt, NULL);
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

	/*
	 * Encode rights data (datum) into BORROW FOP.
	 */
	rc = c2_rm_rdatum2buf(right,
			      (void **)&bfop->bo_right.ri_opaque.op_bytes,
			      &bfop->bo_right.ri_opaque.op_nr);
	if (rc != 0) {
		c2_fop_fini(fop);
		rm_out_fini(outreq);
		goto out;
	}

	pin_add(in, &outreq->ou_req.rog_want->rl_right, C2_RPF_TRACK);
	outreq->ou_fop.f_item.ri_ops = &rm_borrow_rpc_ops;
	c2_rpc_post(&outreq->ou_fop.f_item);

out:
	return rc;
}
C2_EXPORTED(c2_rm_borrow_out);

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
		og->rog_want->rl_id = borrow_reply->br_loan.lo_id;
		og->rog_want->rl_cookie.cv.u_hi =
			borrow_reply->br_loan.lo_cookie.co_hi;
		og->rog_want->rl_cookie.cv.u_lo =
			borrow_reply->br_loan.lo_cookie.co_lo;
		c2_rm_buf2rdatum(&og->rog_want->rl_right,
				 borrow_reply->br_right.ri_opaque.op_bytes,
				 borrow_reply->br_right.ri_opaque.op_nr);

		c2_mutex_lock(&og->rog_owner->ro_lock);
		/* Add loan to the borrowed list. */
		c2_rm_ur_tlist_add(&og->rog_owner->ro_borrowed,
				   &og->rog_want->rl_right);

		/* Add loan to the CACHED list. */
		c2_rm_ur_tlist_add(&og->rog_owner->ro_owned[OWOS_CACHED],
				   &og->rog_want->rl_right);
		og->rog_want = NULL;
		c2_mutex_unlock(&og->rog_owner->ro_lock);
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

	c2_fop_fini(fop);
	rm_out_fini(out);
}

int c2_rm_revoke_out(struct c2_rm_incoming *in,
		     struct c2_rm_loan *loan,
		     struct c2_rm_right *right)
{
	struct c2_fop_rm_revoke *rfop;
	struct rm_out		*outreq;
	struct c2_fop		*fop;
	struct c2_cookie	*ocookie;
	struct c2_cookie	 lcookie;
	int			 rc;

	C2_PRE(in->rin_type == C2_RIT_REVOKE);

	rc = rm_out_create(&outreq, in, loan, right);
	if (rc != 0)
		goto out;

	fop = &outreq->ou_fop;
	c2_fop_init(fop, &c2_fop_rm_revoke_fopt, NULL);
	rfop = c2_fop_data(fop);

	/* Fill up the REVOKE FOP. */
	rfop->rr_loan.lo_id = loan->rl_id;

	/* Fetch the loan cookie and then copy it into the FOP */
	c2_rm_loan_cookie_get(loan, &lcookie);
	rfop->rr_loan.lo_cookie.co_hi = lcookie.cv.u_hi;
	rfop->rr_loan.lo_cookie.co_lo = lcookie.cv.u_lo;

	/*
	 * Fill up the debtor cookie so that the other end can identify
	 * its owner structure.
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
	if (rc != 0) {
		c2_fop_fini(fop);
		rm_out_fini(outreq);
		goto out;
	}

	pin_add(in, &outreq->ou_req.rog_want->rl_right, C2_RPF_TRACK);
	outreq->ou_fop.f_item.ri_ops = &rm_revoke_rpc_ops;
	c2_rpc_post(&outreq->ou_fop.f_item);

out:
	return rc;
}
C2_EXPORTED(c2_rm_revoke_out);

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

	og->rog_rc = item->ri_error ? item->ri_error : revoke_reply->re_rc;
	if (og->rog_rc == 0) {
		c2_mutex_lock(&og->rog_owner->ro_lock);
		/* Move the loan from the sublet list to the CACHED list. */
		c2_rm_ur_tlist_move(&og->rog_owner->ro_owned[OWOS_CACHED],
				    &og->rog_want->rl_right);
		og->rog_want = NULL;
		c2_mutex_unlock(&og->rog_owner->ro_lock);
	}
	c2_rm_outgoing_complete(og);
}

void c2_rm_fop_fini(void)
{
	c2_fop_type_fini_nr(fops, ARRAY_SIZE(fops));
	c2_fop_type_format_fini_nr(rm_fmts, ARRAY_SIZE(rm_fmts));
}
C2_EXPORTED(c2_rm_fop_fini);

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
		if (rc != 0)
			c2_fop_type_format_fini_nr(rm_fmts,
						   ARRAY_SIZE(rm_fmts));
	}

	return rc;
}
C2_EXPORTED(c2_rm_fop_init);

/*
 * @todo - Stubs. Remove later.
 */
int c2_cookie_remote_build(void *obj_ptr, struct c2_cookie *out)
{
	out->cv.u_hi = (uint64_t) obj_ptr;
	return 0;
}

int c2_cookie_dereference(const struct c2_cookie *cookie, void **out)
{
	C2_PRE(out != NULL);

	*out = (void *)cookie->cv.u_hi;
	return 0;
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
