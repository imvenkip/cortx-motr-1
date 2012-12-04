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
#include "fop/fom_generic.h"
#include "rm_fops.h"
#include "rpc/item.h"
#include "rpc/rpc_opcodes.h"
#include "rpc/rpc.h"
#include "rm/rm.h"
#include "rm/rm_internal.h"

/*
 * Data structures.
 */
/*
 * Tracking structure for outgoing request.
 */
struct rm_out {
	struct m0_rm_outgoing  ou_req;
	struct m0_fop	       ou_fop;
};

/**
 * Forward declaration.
 */
static void borrow_reply(struct m0_rpc_item *);
static void outreq_free(struct m0_rpc_item *);
static void revoke_reply(struct m0_rpc_item *);

const struct m0_rpc_item_ops rm_borrow_rpc_ops = {
	.rio_replied = borrow_reply,
	.rio_free = outreq_free,
};

const struct m0_rpc_item_ops rm_revoke_rpc_ops = {
	.rio_replied = revoke_reply,
	.rio_free = outreq_free,
};

/**
 * FOP definitions for resource-credit borrow request and reply.
 */
struct m0_fop_type m0_fop_rm_borrow_fopt;
struct m0_fop_type m0_fop_rm_borrow_rep_fopt;
extern struct m0_sm_state_descr rm_req_phases[];

/**
 * FOP definitions for resource-credit revoke request and reply.
 */
struct m0_fop_type m0_fop_rm_revoke_fopt;
struct m0_fop_type m0_fom_error_rep_fopt;

/*
 * Extern FOM params
 */
extern const struct m0_fom_type_ops rm_borrow_fom_type_ops;
extern const struct m0_sm_conf borrow_sm_conf;

extern const struct m0_fom_type_ops rm_revoke_fom_type_ops;
extern const struct m0_sm_conf borrow_sm_conf;

extern const struct m0_fom_type_ops rm_revoke_fom_type_ops;
extern const struct m0_sm_conf revoke_sm_conf;

/*
 * Allocate and initialise remote request tracking structure.
 */
static int rm_out_create(struct rm_out **out,
			 struct m0_rm_incoming *in,
			 struct m0_rm_credit *credit)
{
	struct rm_out *outreq;
	int	       rc;

	C2_PRE (out != NULL);

	C2_ALLOC_PTR(outreq);
	if (outreq == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	rc = m0_rm_loan_init(&outreq->ou_req.rog_want, credit,
			     in->rin_want.cr_owner->ro_creditor);
	if (rc != 0) {
		m0_free(outreq);
		goto out;
	}

	m0_rm_outgoing_init(&outreq->ou_req, in->rin_type);
	*out = outreq;

out:
	return rc;
}

static int fop_common_fill(struct rm_out *outreq,
			   struct m0_rm_incoming *in,
			   struct m0_rm_credit *credit,
			   struct m0_cookie *cookie,
			   struct m0_fop_type *fopt,
			   size_t offset, void **data)
{
	struct m0_fop_rm_req *req;
	struct m0_fop	     *fop;
	int		      rc;

	fop = &outreq->ou_fop;
	m0_fop_init(fop, fopt, NULL);
	rc = m0_fop_data_alloc(fop);
	if (rc == 0) {
		*data  = m0_fop_data(fop);
		req = (struct m0_fop_rm_req *) (char *)*data + offset;
		req->rrq_policy = in->rin_policy;
		req->rrq_flags = in->rin_flags;
		req->rrq_owner.ow_cookie = *cookie;
		rc = m0_rm_credit_encode(credit, &req->rrq_credit.cr_opaque);
		if (rc != 0)
			m0_fop_fini(fop);
	}
	return rc;
}

static int borrow_fop_fill(struct rm_out *outreq,
			   struct m0_rm_incoming *in,
			   struct m0_rm_credit *credit)
{
	struct m0_fop_rm_borrow *bfop;
	struct m0_cookie	cookie;
	int			 rc;

	m0_cookie_init(&cookie, &in->rin_want.cr_owner->ro_id);
	rc = fop_common_fill(outreq, in, credit, &cookie, &m0_fop_rm_borrow_fopt,
			     offsetof(struct m0_fop_rm_borrow, bo_base),
			     (void **)&bfop);

	if (rc == 0) {
		/* Copy creditor cookie */
		bfop->bo_creditor.ow_cookie =
			in->rin_want.cr_owner->ro_creditor->rem_cookie;
	}
	return rc;
}

static int revoke_fop_fill(struct rm_out *outreq,
			   struct m0_rm_incoming *in,
			   struct m0_rm_loan *loan,
			   struct m0_rm_credit *credit)
{
	struct m0_fop_rm_revoke *rfop;
	int			 rc;

	rc = fop_common_fill(outreq, in, credit, &loan->rl_other->rem_cookie,
			     &m0_fop_rm_revoke_fopt,
			     offsetof(struct m0_fop_rm_revoke, rr_base),
			     (void **)&rfop);

	if (rc == 0)
		rfop->rr_loan.lo_cookie = loan->rl_cookie;

	return rc;
}

/*
 * De-allocate the remote request tracking structure.
 */
static void rm_out_fini(struct rm_out *out)
{
	m0_rm_outgoing_fini(&out->ou_req);
	/* @todo
 	 * Uncomment following line once RPC has mechniasm to free RPC-items.
	m0_free(out);
	 */
}

int m0_rm_request_out(enum m0_rm_outgoing_type otype,
		      struct m0_rm_incoming *in,
		      struct m0_rm_loan *loan,
		      struct m0_rm_credit *credit)
{
	const struct m0_rpc_item_ops *ri_ops;
	struct m0_rpc_session	     *session = NULL;
	struct rm_out		     *outreq;
	int			      rc;

	C2_PRE(C2_IN(otype, (C2_ROT_BORROW, C2_ROT_REVOKE)));

	rc = rm_out_create(&outreq, in, credit);
	if (rc != 0)
		goto out;

	switch (otype) {
	case C2_ROT_BORROW:
		rc = borrow_fop_fill(outreq, in, credit);
		session = in->rin_want.cr_owner->ro_creditor->rem_session;
		ri_ops = &rm_borrow_rpc_ops;
		break;
	case C2_ROT_REVOKE:
		C2_ASSERT(loan != NULL);
		C2_ASSERT(loan->rl_other != NULL);
		rc = revoke_fop_fill(outreq, in, loan, credit);
		session = loan->rl_other->rem_session;
		ri_ops = &rm_revoke_rpc_ops;
		break;
	default:
		C2_IMPOSSIBLE("No such RM outgoing request type");
		break;
	}

	if (rc != 0) {
		m0_fop_fini(&outreq->ou_fop);
		rm_out_fini(outreq);
		goto out;
	}

	pin_add(in, &outreq->ou_req.rog_want.rl_credit, C2_RPF_TRACK);
	m0_rm_ur_tlist_add(&in->rin_want.cr_owner->ro_outgoing[OQS_GROUND],
			   &outreq->ou_req.rog_want.rl_credit);
	if (C2_FI_ENABLED("no-rpc"))
		goto out;

	outreq->ou_fop.f_item.ri_session = session;
	outreq->ou_fop.f_item.ri_ops = ri_ops;
	m0_rpc_post(&outreq->ou_fop.f_item);

out:
	return rc;
}
C2_EXPORTED(m0_rm_borrow_out);

static void borrow_reply(struct m0_rpc_item *item)
{
	struct m0_fop_rm_borrow_rep *borrow_reply;
	struct m0_rm_owner	    *owner;
	struct m0_rm_loan	    *loan = NULL; /* for m0_free(loan) */
	struct m0_rm_credit	    *credit;
	struct m0_rm_credit	    *bcredit;
	struct rm_out		    *outreq;
	struct m0_buf		     buf;
	int			     rc;

	C2_PRE(item != NULL);
	C2_PRE(item->ri_reply != NULL);

	borrow_reply = m0_fop_data(m0_rpc_item_to_fop(item->ri_reply));
	outreq = container_of(m0_rpc_item_to_fop(item), struct rm_out, ou_fop);
	bcredit = &outreq->ou_req.rog_want.rl_credit;
	owner = bcredit->cr_owner;
	rc = item->ri_error ?: borrow_reply->br_rc.rerr_rc;

	if (rc == 0) {
		/* Get the data for a credit from the FOP */
		m0_buf_init(&buf, borrow_reply->br_credit.cr_opaque.b_addr,
				  borrow_reply->br_credit.cr_opaque.b_nob);
		rc = m0_rm_credit_decode(bcredit, &buf);
		if (rc != 0)
			goto out;

		rc = m0_rm_credit_dup(bcredit, &credit) ?:
			m0_rm_loan_alloc(&loan, bcredit, owner->ro_creditor);
		if (rc == 0) {
			loan->rl_cookie = borrow_reply->br_loan.lo_cookie;
			m0_rm_owner_lock(owner);
			/* Add loan to the borrowed list. */
			m0_rm_ur_tlist_add(&owner->ro_borrowed,
					   &loan->rl_credit);

			/* Add credit to the CACHED list. */
			m0_rm_ur_tlist_add(&owner->ro_owned[OWOS_CACHED],
					   credit);
			m0_rm_owner_unlock(owner);
		} else {
			m0_free(loan);
			m0_free(credit);
		}
	}
out:
	outreq->ou_req.rog_rc = rc;
	m0_rm_owner_lock(owner);
	m0_rm_outgoing_complete(&outreq->ou_req);
	m0_rm_owner_unlock(owner);
}

static void revoke_reply(struct m0_rpc_item *item)
{
	struct m0_fom_error_rep *revoke_reply;
	struct m0_rm_owner	*owner;
	struct m0_rm_credit	*credit;
	struct m0_rm_credit	*out_credit;
	struct rm_out		*outreq;
	int			rc;

	C2_PRE(item != NULL);
	C2_PRE(item->ri_reply != NULL);

	revoke_reply = m0_fop_data(m0_rpc_item_to_fop(item->ri_reply));
	outreq = container_of(m0_rpc_item_to_fop(item), struct rm_out, ou_fop);
	out_credit = &outreq->ou_req.rog_want.rl_credit;
	owner = out_credit->cr_owner;
	rc = item->ri_error ?: revoke_reply->rerr_rc;

	rc = rc ?: m0_rm_credit_dup(out_credit, &credit);
	if (rc == 0) {
		m0_rm_owner_lock(owner);
		rc = m0_rm_sublet_remove(out_credit);
		if (rc == 0) {
			m0_rm_ur_tlist_add(&owner->ro_owned[OWOS_CACHED],
					   credit);
			m0_rm_owner_unlock(owner);
		} else {
			m0_rm_owner_unlock(owner);
			m0_free(credit);
		}
	}
	outreq->ou_req.rog_rc = rc;
	m0_rm_owner_lock(owner);
	m0_rm_outgoing_complete(&outreq->ou_req);
	m0_rm_owner_unlock(owner);
}

static void outreq_free(struct m0_rpc_item *item)
{
	struct rm_out *out;
	struct m0_fop *fop;

	C2_PRE(item != NULL);

	fop =  m0_rpc_item_to_fop(item);
	out = container_of(fop, struct rm_out, ou_fop);

	m0_fop_fini(fop);
	rm_out_fini(out);
}

C2_INTERNAL void m0_rm_fop_fini(void)
{
	m0_fop_type_fini(&m0_fop_rm_revoke_fopt);
	m0_fop_type_fini(&m0_fop_rm_borrow_rep_fopt);
	m0_fop_type_fini(&m0_fop_rm_borrow_fopt);
	m0_xc_rm_fini();
	m0_xc_fom_generic_fini();
	m0_xc_cookie_fini();
	m0_xc_buf_fini();
}
C2_EXPORTED(m0_rm_fop_fini);

/**
 * Initialises RM fops.
 * @see rm_fop_fini()
 */
C2_INTERNAL int m0_rm_fop_init(void)
{
	m0_xc_buf_init();
	m0_xc_cookie_init();
	m0_xc_fom_generic_init();
	m0_xc_rm_init();
#ifndef __KERNEL__
	m0_sm_conf_extend(m0_generic_conf.scf_state, rm_req_phases,
			  m0_generic_conf.scf_nr_states);
#endif
	return  C2_FOP_TYPE_INIT(&m0_fop_rm_borrow_fopt,
				 .name      = "Credit Borrow",
				 .opcode    = C2_RM_FOP_BORROW,
				 .xt        = m0_fop_rm_borrow_xc,
#ifndef __KERNEL__
				 .sm	    = &borrow_sm_conf,
				 .fom_ops   = &rm_borrow_fom_type_ops,
#endif
				 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST) ?:
		C2_FOP_TYPE_INIT(&m0_fop_rm_borrow_rep_fopt,
				 .name      = "Credit Borrow Reply",
				 .opcode    = C2_RM_FOP_BORROW_REPLY,
				 .xt        = m0_fop_rm_borrow_rep_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY) ?:
		C2_FOP_TYPE_INIT(&m0_fop_rm_revoke_fopt,
				 .name      = "Credit Revoke",
				 .opcode    = C2_RM_FOP_REVOKE,
				 .xt        = m0_fop_rm_revoke_xc,
#ifndef __KERNEL__
				 .sm	    = &revoke_sm_conf,
				 .fom_ops   = &rm_revoke_fom_type_ops,
#endif
				 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST);
}
C2_EXPORTED(m0_rm_fop_init);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
