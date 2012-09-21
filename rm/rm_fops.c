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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/finject.h"
#include "rm_fops.h"
#include "rpc/item.h"
#include "rpc/rpc_opcodes.h"
#include "rpc/rpc2.h"
#include "rm/rm.h"
#include "rm/rm_internal.h"
#include "rm/rm_xc.h"

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

const struct c2_rpc_item_ops rm_borrow_rpc_ops = {
	.rio_replied = borrow_reply,
	.rio_free = outreq_free,
};

const struct c2_rpc_item_ops rm_revoke_rpc_ops = {
	.rio_replied = revoke_reply,
	.rio_free = outreq_free,
};

struct c2_fop_type *fops[] = {
	&c2_fop_rm_borrow_fopt,
	&c2_fop_rm_borrow_rep_fopt,
	&c2_fop_rm_revoke_fopt,
	&c2_fop_rm_revoke_rep_fopt,
};

/**
 * FOP definitions for resource-right borrow request and reply.
 */
struct c2_fop_type c2_fop_rm_borrow_fopt;
struct c2_fop_type c2_fop_rm_borrow_rep_fopt;

/**
 * FOP definitions for resource-right revoke request and reply.
 */
struct c2_fop_type c2_fop_rm_revoke_fopt;
struct c2_fop_type c2_fop_rm_revoke_rep_fopt;

/*
 * Allocate and initialise remote request tracking structure.
 */
static int rm_out_create(struct rm_out **out,
			 struct c2_rm_incoming *in,
			 struct c2_rm_right *right)
{
	struct rm_out *outreq;
	int	       rc;

	C2_PRE (out != NULL);

	C2_ALLOC_PTR(outreq);
	if (outreq == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	rc = c2_rm_loan_init(&outreq->ou_req.rog_want, right);
	if (rc != 0) {
		c2_free(outreq);
		goto out;
	};

	c2_rm_outgoing_init(&outreq->ou_req, in->rin_type);
	*out = outreq;

out:
	return rc;
}

static int borrow_fop_fill(struct rm_out *outreq,
			   struct c2_rm_incoming *in,
			   struct c2_rm_right *right)
{
	struct c2_fop_rm_borrow *bfop;
	struct c2_fop		*fop;
	struct c2_cookie	*cookie;
	struct c2_cookie	 dcookie;
	struct c2_buf		 buf;
	int			 rc;

	fop = &outreq->ou_fop;
	c2_fop_init(fop, &c2_fop_rm_borrow_fopt, NULL);
	rc = c2_fop_data_alloc(fop);
	if (rc != 0)
		return rc;

	bfop = c2_fop_data(fop);
	/* Fill up the BORROW FOP. */
	bfop->bo_policy = in->rin_policy;
	bfop->bo_flags = in->rin_flags;

	/* Copy creditor cookie */
	cookie = &in->rin_want.ri_owner->ro_creditor->rem_cookie;
	bfop->bo_creditor.ow_cookie = cookie;

	/* Copy debtor cookie */
	c2_cookie_init(&dcookie, &in->rin_want.ri_owner->ro_id);
	bfop->bo_debtor.ow_cookie.co_addr = dcookie;

	/*
	 * Encode right into the BORROW FOP.
	 */
	rc = c2_rm_right_encode(right, &buf);
	if (rc == 0) {
		bfop->bo_right.ri_opaque.op_bytes = buf.b_addr;
		bfop->bo_right.ri_opaque.op_nr = buf.b_nob;
	}

	return rc;
}

static int revoke_fop_fill(struct rm_out *outreq,
			   struct c2_rm_incoming *in,
			   struct c2_rm_loan *loan,
			   struct c2_rm_right *right)
{
	struct c2_fop_rm_revoke *rfop;
	struct c2_fop		*fop;
	struct c2_cookie	*ocookie;
	struct c2_cookie	 lcookie;
	struct c2_buf		 buf;
	int			 rc;

	fop = &outreq->ou_fop;
	c2_fop_init(fop, &c2_fop_rm_revoke_fopt, NULL);
	rc = c2_fop_data_alloc(fop);
	if (rc != 0)
		return rc;

	rfop = c2_fop_data(fop);
	/* Fill up the REVOKE FOP. */
	rfop->rr_policy = in->rin_policy;
	rfop->rr_flags = in->rin_flags;

	/* Generate the loan cookie and then copy it into the FOP */
	c2_cookie_init(&lcookie, &loan->rl_id);
	rfop->rr_loan.lo_cookie = lcookie;

	/*
	 * Fill up the debtor cookie so that the other end can identify
	 * its owner structure.
	 */
	ocookie = &loan->rl_other->rem_cookie;
	rfop->rr_debtor.ow_cookie = ocookie;

	/*
	 * Encode rights data into REVOKE FOP
	 */
	rc = c2_rm_right_encode(right, &buf);
	if (rc == 0) {
		rfop->rr_right.ri_opaque.op_bytes = buf.b_addr;
		rfop->rr_right.ri_opaque.op_nr = buf.b_nob;
	}

	return rc;
}

/*
 * De-allocate the remote request tracking structure.
 */
static void rm_out_fini(struct rm_out *out)
{
	c2_free(out);
}

int c2_rm_request_out(struct c2_rm_incoming *in,
		     struct c2_rm_loan *loan,
		     struct c2_rm_right *right)
{
	struct rm_out		*outreq;
	int			 rc;

	C2_PRE(C2_IN(in->rin_type, (C2_RIT_BORROW, C2_RIT_REVOKE)));

	rc = rm_out_create(&outreq, in, right);
	if (rc != 0)
		goto out;

	switch (in->rin_type) {
	case C2_RIT_BORROW:
		C2_ASSERT(loan == NULL);
		rc = borrow_fop_fill(outreq, in, right);
		break;
	case C2_RIT_REVOKE:
		rc = revoke_fop_fill(outreq, in, loan, right);
		break;
	default:
		break;
	}

	if (rc != 0) {
		c2_fop_fini(&outreq->ou_fop);
		rm_out_fini(outreq);
		goto out;
	}

	pin_add(in, &outreq->ou_req.rog_want.rl_right, C2_RPF_TRACK);
	++in->rin_out_req;
	outreq->ou_fop.f_item.ri_ops = &rm_borrow_rpc_ops;
	if (C2_FI_ENABLED("no-rpc"))
		goto out;

	c2_rpc_post(&outreq->ou_fop.f_item);

out:
	return rc;
}
C2_EXPORTED(c2_rm_borrow_out);

static int right_dup(const struct c2_rm_right *src_right,
		     struct c2_rm_right **dest_right)
{
	C2_PRE(dest_right != NULL);

	C2_ALLOC_PTR(*dest_right);
	if (*dest_right == NULL)
		return -ENOMEM;

	c2_rm_right_init(*dest_right, src_right->ri_owner);
	return src_right->ri_ops->rro_copy(*dest_right, src_right);
}

static int loan_create(struct c2_rm_loan **loan,
		       const struct c2_rm_right *right)
{
	C2_PRE(loan != NULL);

	C2_ALLOC_PTR(*loan);
	return *loan ? c2_rm_loan_init(*loan, right) : -ENOMEM;
}

static void borrow_reply(struct c2_rpc_item *item)
{
	struct c2_fop_rm_borrow_rep *borrow_reply;
	struct c2_rm_owner	    *owner;
	struct c2_rm_loan	    *loan = NULL; /* for c2_free(loan) */
	struct c2_rm_right	    *right;
	struct c2_rm_right	    *bright;
	struct rm_out		    *outreq;
	struct c2_buf		     buf;
	int			     rc;

	C2_PRE(item != NULL);
	C2_PRE(item->ri_reply != NULL);

	borrow_reply = c2_fop_data(c2_rpc_item_to_fop(item->ri_reply));
	outreq = container_of(c2_rpc_item_to_fop(item), struct rm_out, ou_fop);
	bright = outreq->ou_req.rog_want.rl_right;
	owner = bright->ri_owner;
	rc = item->ri_error ?: borrow_reply->br_rc;

	if (rc == 0) {
		/* Get the data for a right from the FOP */
		c2_buf_init(&buf, borrow_reply->br_right.ri_opaque.op_bytes,
				  borrow_reply->br_right.ri_opaque.op_nr);
		rc = c2_rm_right_decode(bright, &buf);
		if (rc != 0)
			goto out;

		rc = right_dup(bright, &right) ?: loan_create(&loan, bright);
		if (rc == 0) {
			loan->rl_cookie = borrow_reply->br_loan.lo_cookie;
			c2_mutex_lock(&owner->ro_lock);
			/* Add loan to the borrowed list. */
			c2_rm_ur_tlist_add(&owner->ro_borrowed,
					   &loan->rl_right);

			/* Add right to the CACHED list. */
			c2_rm_ur_tlist_add(&owner->ro_owned[OWOS_CACHED],
					   right);
			c2_mutex_unlock(&owner->ro_lock);
		} else {
			c2_free(loan);
			c2_free(right);
		}
	}
out:
	outreq->ou_req.rog_rc = rc;
	c2_mutex_lock(&owner->ro_lock);
	c2_rm_outgoing_complete(&outreq->ou_req);
	c2_mutex_unlock(&owner->ro_lock);
}

static void revoke_reply(struct c2_rpc_item *item)
{
	struct c2_fop_rm_revoke_rep *revoke_reply;
	struct c2_rm_outgoing	    *og;
	struct c2_rm_right	    *right;
	struct c2_rm_right	    *out_right;
	struct rm_out		    *outreq;

	C2_PRE(item != NULL);
	C2_PRE(item->ri_reply != NULL);

	revoke_reply = c2_fop_data(c2_rpc_item_to_fop(item->ri_reply));
	outreq = container_of(c2_rpc_item_to_fop(item), struct rm_out, ou_fop);
	out_right = outreq->ou_req.rog_want.rl_right;
	owner = out_right->ri_owner;
	rc = item->ri_error ?: revoke_reply->re_rc;

	rc = rc ?: right_dup(out_right, &right);
	if (rc == 0) {
		c2_mutex_lock(&owner->ro_lock);
		rc = c2_rm_sublet_remove(out_right);
		if (rc == 0) {
			c2_rm_ur_tlist_add(&owner->ro_owned[OWOS_CACHED],
					   right);
			c2_mutex_unlock(&owner->ro_lock);
		} else {
			c2_mutex_unlock(&owner->ro_lock);
			c2_free(right);
		}
	}
	outreq->ou_req.rog_rc = rc;
	c2_mutex_lock(&owner->ro_lock);
	c2_rm_outgoing_complete(&outreq->ou_req);
	c2_mutex_unlock(&owner->ro_lock);
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

void c2_rm_fop_fini(void)
{
	c2_fop_type_fini(&c2_fop_rm_revoke_rep_fopt);
	c2_fop_type_fini(&c2_fop_rm_revoke_fopt);
	c2_fop_type_fini(&c2_fop_rm_borrow_rep_fopt);
	c2_fop_type_fini(&c2_fop_rm_borrow_fopt);
	c2_xc_rm_xc_fini();
}
C2_EXPORTED(c2_rm_fop_fini);

/**
 * Initialises RM fops.
 * @see rm_fop_fini()
 */
int c2_rm_fop_init(void)
{
	c2_xc_rm_xc_init();
	return  C2_FOP_TYPE_INIT(&c2_fop_rm_borrow_fopt,
				 .name      = "Right Borrow",
				 .opcode    = C2_RM_FOP_BORROW,
				 .xt        = c2_fop_rm_borrow_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST) ?:
		C2_FOP_TYPE_INIT(&c2_fop_rm_borrow_rep_fopt,
				 .name      = "Right Borrow Reply",
				 .opcode    = C2_RM_FOP_BORROW_REPLY,
				 .xt        = c2_fop_rm_borrow_rep_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY) ?:
		C2_FOP_TYPE_INIT(&c2_fop_rm_revoke_fopt,
				 .name      = "Right Revoke",
				 .opcode    = C2_RM_FOP_REVOKE,
				 .xt        = c2_fop_rm_revoke_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST) ?:
		C2_FOP_TYPE_INIT(&c2_fop_rm_revoke_rep_fopt,
				 .name      = "Right Revoke Reply",
				 .opcode    = C2_RM_FOP_REVOKE_REPLY,
				 .xt        = c2_fop_rm_revoke_rep_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY);
}
C2_EXPORTED(c2_rm_fop_init);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
