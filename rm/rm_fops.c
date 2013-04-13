/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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

#undef M0_TRACE_SUBSYSTEM
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RM

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/trace.h"
#include "lib/finject.h"

#include "rm/rm_fops.h"
#include "rm/rm_foms.h"
#include "rm/rm_fops_xc.h"
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
	struct m0_rm_outgoing ou_req;
	struct m0_sm_ast      ou_ast;
	struct m0_fop	      ou_fop;
};

/**
 * Forward declaration.
 */
static void reply_process(struct m0_rpc_item *);
static void borrow_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast);
static void revoke_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast);

const struct m0_rpc_item_ops rm_request_rpc_ops = {
	.rio_replied = reply_process,
};

/**
 * FOP definitions for resource-credit borrow request and reply.
 */
struct m0_fop_type m0_rm_fop_borrow_fopt;
struct m0_fop_type m0_rm_fop_borrow_rep_fopt;
extern struct m0_sm_state_descr rm_req_phases[];
extern struct m0_reqh_service_type m0_rpc_service_type;
extern struct m0_reqh_service_type m0_rms_type;

/**
 * FOP definitions for resource-credit revoke request and reply.
 */
struct m0_fop_type m0_rm_fop_revoke_fopt;

/*
 * Extern FOM params
 */
extern const struct m0_fom_type_ops rm_borrow_fom_type_ops;
extern struct m0_sm_conf borrow_sm_conf;

extern const struct m0_fom_type_ops rm_revoke_fom_type_ops;
extern struct m0_sm_conf revoke_sm_conf;

/*
 * Allocate and initialise remote request tracking structure.
 */
static int rm_out_create(struct rm_out **out,
			 struct m0_rm_incoming *in,
			 struct m0_rm_credit *credit)
{
	struct rm_out *outreq;
	int	       rc;

	M0_ENTRY();
	M0_PRE (out != NULL);

	M0_ALLOC_PTR(outreq);
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
	M0_RETURN(rc);
}

/*
 * Finalises and de-allocates the remote request tracking structure.
 */
static void rm_out_release(struct rm_out *out)
{
	m0_rm_outgoing_fini(&out->ou_req);
	m0_free(out);
}

static void rm_fop_release(struct m0_ref *ref)
{
	struct m0_fop *fop;
	struct rm_out *out;

	M0_ENTRY();
	M0_PRE(ref != NULL);
	fop = container_of(ref, struct m0_fop, f_ref);
	out = container_of(fop, struct rm_out, ou_fop);

	m0_fop_fini(fop);
	rm_out_release(out);
	M0_LEAVE();
}

static int fop_common_fill(struct rm_out         *outreq,
			   struct m0_rm_incoming *in,
			   struct m0_rm_credit   *credit,
			   struct m0_cookie      *cookie,
			   struct m0_fop_type    *fopt,
			   size_t                 offset,
			   void                 **data)
{
	struct m0_rm_fop_req *req;
	struct m0_fop        *fop;
	int                   rc;

	fop = &outreq->ou_fop;
	m0_fop_init(fop, fopt, NULL, rm_fop_release);
	rc = m0_fop_data_alloc(fop);
	if (rc == 0) {
		struct m0_rm_resource *resource;

		*data  = m0_fop_data(fop);
		req = (struct m0_rm_fop_req *) (char *)*data + offset;
		req->rrq_policy = in->rin_policy;
		req->rrq_flags = in->rin_flags;
		req->rrq_owner.ow_cookie = *cookie;

		resource = in->rin_want.cr_owner->ro_resource;
		rc = m0_rm_resource_encode(resource,
					   &req->rrq_owner.ow_resource) ?:
			m0_rm_credit_encode(credit, &req->rrq_credit.cr_opaque);
	}
	return rc;
}

static int borrow_fop_fill(struct rm_out         *outreq,
			   struct m0_rm_incoming *in,
			   struct m0_rm_credit   *credit)
{
	struct m0_rm_fop_borrow *bfop;
	struct m0_cookie	 cookie;
	int			 rc;

	M0_ENTRY("creating borrow fop for incoming: %p credit value: %llu",
		 in, (long long unsigned) credit->cr_datum);
	m0_cookie_init(&cookie, &in->rin_want.cr_owner->ro_id);
	rc = fop_common_fill(outreq, in, credit, &cookie,
			     &m0_rm_fop_borrow_fopt,
			     offsetof(struct m0_rm_fop_borrow, bo_base),
			     (void **)&bfop);

	if (rc == 0) {
		struct m0_rm_remote *rem = in->rin_want.cr_owner->ro_creditor;

		/* Copy creditor cookie */
		bfop->bo_creditor.ow_cookie = rem ? rem->rem_cookie :
			M0_COOKIE_NULL;
	}
	M0_RETURN(rc);
}

static int revoke_fop_fill(struct rm_out *outreq,
			   struct m0_rm_incoming *in,
			   struct m0_rm_loan *loan,
			   struct m0_rm_credit *credit)
{
	struct m0_rm_fop_revoke *rfop;
	int			 rc;

	M0_ENTRY("creating revoke fop for incoming: %p credit value: %llu",
		 in, (long long unsigned) credit->cr_datum);
	rc = fop_common_fill(outreq, in, credit, &loan->rl_other->rem_cookie,
			     &m0_rm_fop_revoke_fopt,
			     offsetof(struct m0_rm_fop_revoke, rr_base),
			     (void **)&rfop);

	if (rc == 0)
		rfop->rr_loan.lo_cookie = loan->rl_cookie;

	M0_RETURN(rc);
}

int m0_rm_request_out(enum m0_rm_outgoing_type otype,
		      struct m0_rm_incoming   *in,
		      struct m0_rm_loan       *loan,
		      struct m0_rm_credit     *credit)
{
	struct m0_rpc_session *session = NULL;
	struct rm_out	      *outreq;
	int		       rc;

	M0_ENTRY("sending request type: %d for incoming: %p credit value: %llu",
		 otype, in, (long long unsigned) credit->cr_datum);
	M0_PRE(M0_IN(otype, (M0_ROT_BORROW, M0_ROT_REVOKE)));

	rc = rm_out_create(&outreq, in, credit);
	if (rc != 0)
		goto out;

	switch (otype) {
	case M0_ROT_BORROW:
		rc = borrow_fop_fill(outreq, in, credit);
		session = in->rin_want.cr_owner->ro_creditor->rem_session;
		outreq->ou_ast.sa_cb = &borrow_ast;
		break;
	case M0_ROT_REVOKE: {
		M0_ASSERT(loan != NULL);
		M0_ASSERT(loan->rl_other != NULL);
		/*
		 * Check whether remote session is established or not.
		 * If a situation comes when we have to send revoke
		 * request even before the session is established,
		 * we need to wait till the session is established.
		 */
		if (loan->rl_other->rem_rev_sess_wait != NULL) {
			struct m0_chan *chan;
			m0_chan_wait(loan->rl_other->rem_rev_sess_wait);
			chan = loan->rl_other->rem_rev_sess_wait->cl_chan;
			m0_clink_del_lock(loan->rl_other->rem_rev_sess_wait);
			m0_clink_fini(loan->rl_other->rem_rev_sess_wait);
			m0_free(loan->rl_other->rem_rev_sess_wait);
			loan->rl_other->rem_rev_sess_wait = NULL;
		}
		rc = revoke_fop_fill(outreq, in, loan, credit);
		session = loan->rl_other->rem_session;
		outreq->ou_ast.sa_cb = &revoke_ast;
		break;
	}
	default:
		M0_IMPOSSIBLE("No such RM outgoing request type");
		break;
	}

	if (rc != 0) {
		M0_LOG(M0_ERROR, "filling fop failed: rc [%d]\n", rc);
		m0_fop_put(&outreq->ou_fop);
		goto out;
	}

	pin_add(in, &outreq->ou_req.rog_want.rl_credit, M0_RPF_TRACK);
	m0_rm_ur_tlist_add(&in->rin_want.cr_owner->ro_outgoing[OQS_GROUND],
			   &outreq->ou_req.rog_want.rl_credit);
	if (M0_FI_ENABLED("no-rpc"))
		goto out;

	M0_LOG(M0_DEBUG, "sending request:%p over session: %p",
			 outreq, session);
	outreq->ou_fop.f_item.ri_session = session;
	outreq->ou_fop.f_item.ri_ops = &rm_request_rpc_ops;
	rc = m0_rpc_post(&outreq->ou_fop.f_item);
	M0_ASSERT(rc == 0);

out:
	M0_RETURN(rc);
}

static void borrow_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct m0_rm_fop_borrow_rep *borrow_reply;
	struct m0_rm_owner	    *owner;
	struct m0_rm_loan	    *loan = NULL; /* for m0_free(loan) */
	struct m0_rm_credit	    *credit;
	struct m0_rm_credit	    *bcredit;
	struct rm_out		    *outreq;
	struct m0_rpc_item	    *item;
	struct m0_rpc_item	    *item_rep;
	struct m0_buf		     buf;
	int			     rc;

	M0_ENTRY();

	borrow_reply = NULL;
	outreq   = container_of(ast, struct rm_out, ou_ast);
	item     = &outreq->ou_fop.f_item;
	item_rep = item->ri_reply;
	rc       = item->ri_error ?: m0_rpc_item_generic_reply_rc(item_rep);
	if (rc == 0) {
		borrow_reply = m0_fop_data(m0_rpc_item_to_fop(item_rep));
		rc = borrow_reply->br_rc.gr_rc;
	}
	M0_ASSERT(m0_mutex_is_locked(&grp->s_lock));
	if (rc == 0) {
		M0_ASSERT(borrow_reply != NULL);
		bcredit = &outreq->ou_req.rog_want.rl_credit;
		owner   = bcredit->cr_owner;
		/* Get the data for a credit from the FOP */
		m0_buf_init(&buf, borrow_reply->br_credit.cr_opaque.b_addr,
				  borrow_reply->br_credit.cr_opaque.b_nob);
		rc = m0_rm_credit_decode(bcredit, &buf);
		if (rc != 0) {
			M0_LOG(M0_ERROR, "credit decode for request: %p"
					 " failed: rc [%d]\n", outreq, rc);
			goto out;
		}

		rc = m0_rm_credit_dup(bcredit, &credit) ?:
			m0_rm_loan_alloc(&loan, bcredit, owner->ro_creditor);
		if (rc == 0) {
			loan->rl_cookie = borrow_reply->br_loan.lo_cookie;
			/* Add loan to the borrowed list. */
			m0_rm_ur_tlist_add(&owner->ro_borrowed,
					   &loan->rl_credit);

			/* Add credit to the CACHED list. */
			m0_rm_ur_tlist_add(&owner->ro_owned[OWOS_CACHED],
					   credit);
			M0_LOG(M0_INFO, "borrow request:%p successful "
			       "credit value: %llu",
			       outreq, (long long unsigned) credit->cr_datum);
		} else {
			M0_LOG(M0_ERROR, "borrowed loan/credit allocation"
					 " request: %p failed: rc [%d]\n",
					 outreq, rc);
			m0_free(loan);
			m0_free(credit);
		}
	} else
		M0_LOG(M0_ERROR, "Borrow request:%p failed: rc [%d]\n",
				 outreq, rc);
out:
	outreq->ou_req.rog_rc = rc;
	m0_rm_outgoing_complete(&outreq->ou_req);
	m0_fop_put(&outreq->ou_fop);
	M0_LEAVE();
}

static void revoke_ast(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct m0_fop_generic_reply *revoke_reply;
	struct m0_rm_owner	    *owner;
	struct m0_rm_credit	    *credit;
	struct m0_rm_credit	    *out_credit;
	struct rm_out		    *outreq;
	struct m0_rpc_item	    *item;
	int			     rc;

	M0_ENTRY();
	M0_ASSERT(m0_mutex_is_locked(&grp->s_lock));

	outreq     = container_of(ast, struct rm_out, ou_ast);
	out_credit = &outreq->ou_req.rog_want.rl_credit;
	owner      = out_credit->cr_owner;

	item = &outreq->ou_fop.f_item;
	rc   = item->ri_error;
	if (rc == 0) {
		M0_ASSERT(item->ri_reply != NULL);
		revoke_reply = m0_fop_data(m0_rpc_item_to_fop(item->ri_reply));
		rc = revoke_reply->gr_rc;
	}
	if (rc != 0) {
		M0_LOG(M0_ERROR, "revoke request:%p failed: rc [%d]\n",
				 outreq, rc);
		goto out;
	}
	rc = m0_rm_credit_dup(out_credit, &credit);
	if (rc == 0) {
		rc = m0_rm_sublet_remove(out_credit);
		if (rc == 0) {
			m0_rm_ur_tlist_add(&owner->ro_owned[OWOS_CACHED],
					   credit);
			M0_LOG(M0_INFO, "revoke request:%p sublet removed "
					"- credit cached\n", outreq);
		} else {
			m0_free(credit);
			M0_LOG(M0_ERROR, "revoke request:%p sublet removal "
					 "failed: rc [%d]\n", outreq, rc);
		}
	} else
		M0_LOG(M0_ERROR, "revoke request:%p credit allocation "
				 "failed: rc [%d]\n", outreq, rc);

out:
	outreq->ou_req.rog_rc = rc;
	m0_rm_outgoing_complete(&outreq->ou_req);
	m0_fop_put(&outreq->ou_fop);
	M0_LEAVE();
}

static void reply_process(struct m0_rpc_item *item)
{
	struct m0_rm_owner *owner;
	struct rm_out      *outreq;

	M0_ENTRY();
	M0_PRE(item != NULL);
	M0_PRE(item->ri_reply != NULL);

	outreq = container_of(m0_rpc_item_to_fop(item), struct rm_out, ou_fop);
	owner = outreq->ou_req.rog_want.rl_credit.cr_owner;

	m0_sm_ast_post(owner_grp(owner), &outreq->ou_ast);
	M0_LEAVE();
}

M0_INTERNAL void m0_rm_fop_fini(void)
{
	m0_fop_type_fini(&m0_rm_fop_revoke_fopt);
	m0_fop_type_fini(&m0_rm_fop_borrow_rep_fopt);
	m0_fop_type_fini(&m0_rm_fop_borrow_fopt);
	m0_xc_rm_fops_fini();
	m0_xc_fom_generic_fini();
	m0_xc_cookie_fini();
	m0_xc_buf_fini();
}
M0_EXPORTED(m0_rm_fop_fini);

/**
 * Initialises RM fops.
 * @see rm_fop_fini()
 */
M0_INTERNAL int m0_rm_fop_init(void)
{
	m0_xc_buf_init();
	m0_xc_cookie_init();
	m0_xc_fom_generic_init();
	m0_xc_rm_fops_init();
#ifndef __KERNEL__
	m0_sm_conf_extend(m0_generic_conf.scf_state, rm_req_phases,
			  m0_generic_conf.scf_nr_states);
#endif
	return  M0_FOP_TYPE_INIT(&m0_rm_fop_borrow_fopt,
				 .name      = "Credit Borrow",
				 .opcode    = M0_RM_FOP_BORROW,
				 .xt        = m0_rm_fop_borrow_xc,
#ifndef __KERNEL__
				 .sm	    = &borrow_sm_conf,
				 .fom_ops   = &rm_borrow_fom_type_ops,
#endif
				 .svc_type  = &m0_rms_type,
				 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST) ?:
		M0_FOP_TYPE_INIT(&m0_rm_fop_borrow_rep_fopt,
				 .name      = "Credit Borrow Reply",
				 .opcode    = M0_RM_FOP_BORROW_REPLY,
				 .xt        = m0_rm_fop_borrow_rep_xc,
				 .svc_type  = &m0_rpc_service_type,
				 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY) ?:
		M0_FOP_TYPE_INIT(&m0_rm_fop_revoke_fopt,
				 .name      = "Credit Revoke",
				 .opcode    = M0_RM_FOP_REVOKE,
				 .xt        = m0_rm_fop_revoke_xc,
#ifndef __KERNEL__
				 .sm	    = &revoke_sm_conf,
				 .fom_ops   = &rm_revoke_fom_type_ops,
#endif
				 .svc_type  = &m0_rms_type,
				 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST);
}
M0_EXPORTED(m0_rm_fop_init);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
