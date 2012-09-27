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
	int	       rc = 0;

	C2_PRE (out != NULL);

	C2_ALLOC_PTR(outreq);
	if (outreq == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	C2_SET0(outreq);

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
	bfop->bo_creditor.ow_cookie =
		in->rin_want.ri_owner->ro_creditor->rem_cookie;

	/* Copy debtor cookie */
	c2_cookie_init(&bfop->bo_debtor.ow_cookie,
		       &in->rin_want.ri_owner->ro_id);

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
	c2_cookie_init(&rfop->rr_loan.lo_cookie, &loan->rl_id);

	/*
	 * Fill up the debtor cookie so that the other end can identify
	 * its owner structure.
	 */
	rfop->rr_debtor.ow_cookie = loan->rl_other->rem_cookie;

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

static void borrow_reply(struct c2_rpc_item *item)
{
	struct c2_fop_rm_borrow_rep *borrow_reply;
	struct c2_rm_owner	    *owner;
	struct c2_rm_outgoing	    *og;
	struct c2_rm_loan	    *loan;
	struct c2_rm_right	    *right;
	struct c2_rm_right	    *bright;
	struct rm_out		    *outreq;
	struct c2_fop		    *reply_fop;
	struct c2_fop		    *fop;
	struct c2_buf		     buf;
	int			     rc;

	C2_PRE(item != NULL);
	C2_PRE(item->ri_reply != NULL);

	reply_fop = c2_rpc_item_to_fop(item->ri_reply);
	borrow_reply = c2_fop_data(reply_fop);
	fop = c2_rpc_item_to_fop(item);
	outreq = container_of(fop, struct rm_out, ou_fop);
	og = &outreq->ou_req;

	C2_ASSERT(og != NULL);

	rc = item->ri_error ? item->ri_error : borrow_reply->br_rc;
	bright = &og->rog_want.rl_right;
	owner = bright->ri_owner;

	if (rc == 0) {
		/*
		 * Allocate loan and right for the borrowed right.
		 * loan goes to the borrowed list. Right goes to the CACHED
		 * list.
		 */
		C2_ALLOC_PTR(loan);
		if (loan == NULL) {
			rc = -ENOMEM;
			goto out;
		}
		C2_ALLOC_PTR(right);
		if (loan == NULL) {
			rc = -ENOMEM;
			goto out;
		}
		/* Get the data for a right from the FOP */
		c2_buf_init(&buf, borrow_reply->br_right.ri_opaque.op_bytes,
				  borrow_reply->br_right.ri_opaque.op_nr);
		rc = c2_rm_right_decode(bright, &buf);
		if (rc != 0)
			goto out;

		/* Copy the "right" data */
		c2_rm_right_init(right, owner);
		rc = c2_rm_loan_init(loan, bright);
		rc = rc ?: bright->ri_ops->rro_copy(bright, right);

		loan->rl_cookie = borrow_reply->br_loan.lo_cookie;

		if (rc == 0) {
			c2_mutex_lock(&owner->ro_lock);
			/* Add loan to the borrowed list. */
			c2_rm_ur_tlist_add(&owner->ro_borrowed,
					   &loan->rl_right);

			/* Add right to the CACHED list. */
			c2_rm_ur_tlist_add(&owner->ro_owned[OWOS_CACHED],
					   right);
			c2_mutex_unlock(&owner->ro_lock);
		}
	}
out:
	og->rog_rc = rc;
	c2_mutex_lock(&owner->ro_lock);
	c2_rm_outgoing_complete(og);
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

static void revoke_reply(struct c2_rpc_item *item)
{
	struct c2_fop_rm_revoke_rep *revoke_reply;
	struct c2_rm_outgoing	    *og;
	struct c2_rm_owner	    *owner;
	struct c2_rm_right	    *right = NULL;
	struct c2_rm_right	    *out_right;
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

	out_right = &og->rog_want.rl_right;
	owner = out_right->ri_owner;
	og->rog_rc = item->ri_error ? item->ri_error : revoke_reply->re_rc;

	if (og->rog_rc == 0) {
		C2_ALLOC_PTR(right);
		if (right == NULL)
			og->rog_rc = -ENOMEM;
		else {
			c2_rm_right_init(right, owner);
			og->rog_rc = out_right->ri_ops->rro_copy(right,
								 out_right);
			if (og->rog_rc != 0)
				c2_free(right);
		}
	}
	c2_mutex_lock(&owner->ro_lock);
	if (og->rog_rc == 0) {
		og->rog_rc = c2_rm_sublet_remove(out_right);
		if (og->rog_rc == 0)
			c2_rm_ur_tlist_add(&owner->ro_owned[OWOS_CACHED],
					   right);
	}
	c2_rm_outgoing_complete(og);
	c2_mutex_unlock(&owner->ro_lock);
}

void c2_rm_fop_fini(void)
{
	c2_fop_type_fini(&c2_fop_rm_revoke_rep_fopt);
	c2_fop_type_fini(&c2_fop_rm_revoke_fopt);
	c2_fop_type_fini(&c2_fop_rm_borrow_rep_fopt);
	c2_fop_type_fini(&c2_fop_rm_borrow_fopt);
	c2_xc_rm_xc_fini();
//	c2_xc_cookie_fini();
}
C2_EXPORTED(c2_rm_fop_fini);

/**
 * Initialises RM fops.
 * @see rm_fop_fini()
 */
int c2_rm_fop_init(void)
{
//	c2_xc_cookie_init();
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
