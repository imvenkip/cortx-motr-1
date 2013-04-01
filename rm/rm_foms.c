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
#include "lib/list.h"
#include "lib/memory.h"
#include "lib/misc.h"   /* M0_IN */
#include "lib/trace.h"
#include "fop/fom_generic.h"
#include "rm/rm_fops.h"
#include "rm/rm_foms.h"
#include "rm/rm_service.h"
#include "rm/ut/rings.h"
/**
   @addtogroup rm

   This file includes data structures and function used by RM:fop layer.

   @{
 */

/**
 * Forward declaration
 */
static int borrow_fom_create(struct m0_fop *fop, struct m0_fom **out,
			     struct m0_reqh *reqh);
static void borrow_fom_fini(struct m0_fom *fom);
static int revoke_fom_create(struct m0_fop *fop, struct m0_fom **out,
			     struct m0_reqh *reqh);
static void revoke_fom_fini(struct m0_fom *fom);
static int borrow_fom_tick(struct m0_fom *);
static int revoke_fom_tick(struct m0_fom *);
static size_t locality(const struct m0_fom *fom);

static void remote_incoming_complete(struct m0_rm_incoming *in, int32_t rc);
static void remote_incoming_conflict(struct m0_rm_incoming *in);

/*
 * As part of of incoming_complete(), call remote_incoming complete.
 * This will call request specific functions.
 */
static struct m0_rm_incoming_ops remote_incoming_ops = {
	.rio_complete = remote_incoming_complete,
	.rio_conflict = remote_incoming_conflict,
};

static void rm_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
	/**
	 * @todo: Do the actual impl, need to set MAGIC, so that
	 * m0_fom_init() can pass
	 */
	fom->fo_addb_ctx.ac_magic = M0_ADDB_CTX_MAGIC;
}

/*
 * Borrow FOM ops.
 */
static struct m0_fom_ops rm_fom_borrow_ops = {
	.fo_addb_init     = rm_fom_addb_init,
	.fo_fini          = borrow_fom_fini,
	.fo_tick          = borrow_fom_tick,
	.fo_home_locality = locality,
};

const struct m0_fom_type_ops rm_borrow_fom_type_ops = {
	.fto_create = borrow_fom_create,
};

/*
 * Revoke FOM ops.
 */
static struct m0_fom_ops rm_fom_revoke_ops = {
	.fo_addb_init     = rm_fom_addb_init,
	.fo_fini          = revoke_fom_fini,
	.fo_tick          = revoke_fom_tick,
	.fo_home_locality = locality,
};

const struct m0_fom_type_ops rm_revoke_fom_type_ops = {
	.fto_create = revoke_fom_create,
};

struct m0_sm_state_descr rm_req_phases[] = {
	[FOPH_RM_REQ_START] = {
		.sd_name      = "RM Request Begin",
		.sd_allowed   = M0_BITS(FOPH_RM_REQ_WAIT, FOPH_RM_REQ_FINISH,
					M0_FOPH_FAILURE)
	},
	[FOPH_RM_REQ_WAIT] = {
		.sd_name      = "RM Request Wait",
		.sd_allowed   = M0_BITS(FOPH_RM_REQ_FINISH, M0_FOPH_SUCCESS,
					M0_FOPH_FAILURE)
	},
	[FOPH_RM_REQ_FINISH] = {
		.sd_name      = "RM Request Completion",
		.sd_allowed   = M0_BITS(M0_FOPH_SUCCESS, M0_FOPH_FAILURE)
	},
};

struct m0_sm_conf borrow_sm_conf = {
	.scf_name      = "Borrow FOM conf",
	.scf_nr_states = ARRAY_SIZE(rm_req_phases),
	.scf_state     = rm_req_phases
};

struct m0_sm_conf revoke_sm_conf = {
	.scf_name      = "Revoke FOM conf",
	.scf_nr_states = ARRAY_SIZE(rm_req_phases),
	.scf_state     = rm_req_phases
};

static void remote_incoming_complete(struct m0_rm_incoming *in, int32_t rc)
{
	struct m0_rm_remote_incoming *rem_in;
	struct rm_request_fom	     *rqfom;
	enum m0_rm_fom_phases	      phase;

	rem_in = container_of(in, struct m0_rm_remote_incoming, ri_incoming);
	rqfom = container_of(rem_in, struct rm_request_fom, rf_in);
	phase = m0_fom_phase(&rqfom->rf_fom);
	M0_ASSERT(M0_IN(phase, (FOPH_RM_REQ_START, FOPH_RM_REQ_WAIT)));

	switch (in->rin_type) {
	case M0_RIT_BORROW:
		rc = rc ?: m0_rm_borrow_commit(rem_in);
		break;
	case M0_RIT_REVOKE:
		rc = rc ?: m0_rm_revoke_commit(rem_in);
		break;
	default:
		M0_IMPOSSIBLE("Unrecognized RM request");
		break;
	}

	/*
	 * Override the rc.
	 */
	in->rin_rc = rc;

	if (phase == FOPH_RM_REQ_WAIT)
		m0_fom_wakeup(&rqfom->rf_fom);
}

static void remote_incoming_conflict(struct m0_rm_incoming *in)
{
	in->rin_sm.sm_rc = -EACCES;
}

/*
 * Generic RM request-FOM constructor.
 */
static int request_fom_create(enum m0_rm_incoming_type type,
			      struct m0_fop *fop, struct m0_fom **out,
			      struct m0_reqh *reqh)
{
	struct rm_request_fom *rqfom;
	struct m0_fop_type    *fopt    = NULL;
	struct m0_fom_ops     *fom_ops = NULL;
	struct m0_fop	      *reply_fop;

	M0_ENTRY("creating FOM for request: %d", type);

	M0_PRE(fop != NULL);
	M0_PRE(fop->f_type != NULL);
	M0_PRE(out != NULL);

	M0_ALLOC_PTR(rqfom);
	if (rqfom == NULL)
		M0_RETURN(-ENOMEM);

	switch (type) {
	case M0_RIT_BORROW:
		fopt = &m0_rm_fop_borrow_rep_fopt;
		fom_ops = &rm_fom_borrow_ops;
		break;
	case M0_RIT_REVOKE:
		fopt = &m0_fop_generic_reply_fopt;
		fom_ops = &rm_fom_revoke_ops;
		break;
	default:
		M0_IMPOSSIBLE("Unrecognised RM request");
		break;
	}

	reply_fop = m0_fop_alloc(fopt, NULL);
	if (reply_fop == NULL) {
		m0_free(rqfom);
		M0_RETURN(-ENOMEM);
	}

	m0_fom_init(&rqfom->rf_fom, &fop->f_type->ft_fom_type,
		    fom_ops, fop, reply_fop, reqh,
		    fop->f_type->ft_fom_type.ft_rstype);
	/*
	 * m0_fop_alloc() holds a reference. m0_fom_init() holds additional
	 * reference to reply_fop. Hence use m0_fop_put() to release extra
	 * reference.
	 */
	m0_fop_put(reply_fop);
	*out = &rqfom->rf_fom;
	M0_RETURN(0);
}

/*
 * Generic RM request-FOM destructor.
 */
static void request_fom_fini(struct m0_fom *fom)
{
	struct rm_request_fom *rfom;

	M0_PRE(fom != NULL);

	rfom = container_of(fom, struct rm_request_fom, rf_fom);
	m0_fom_fini(fom);
	m0_free(rfom);
}

static size_t locality(const struct m0_fom *fom)
{
	struct rm_request_fom *rfom;

	rfom = container_of(fom, struct rm_request_fom, rf_fom);
	return (size_t)(rfom->rf_in.ri_owner_cookie.co_generation >> 32);
}

/*
 * This function will fill reply FOP data.
 * However, it will not set the return code.
 *
 * @see reply_err_set()
 */
static int reply_prepare(const enum m0_rm_incoming_type type,
			 struct m0_fom *fom)
{
	struct m0_rm_fop_borrow_rep *bfop;
	struct rm_request_fom       *rfom;
	struct m0_rm_loan	    *loan;
	int			     rc = 0;

	M0_ENTRY("reply for fom: %p", fom);
	rfom = container_of(fom, struct rm_request_fom, rf_fom);

	switch (type) {
	case M0_RIT_BORROW:
		bfop = m0_fop_data(fom->fo_rep_fop);
		bfop->br_loan.lo_cookie = rfom->rf_in.ri_loan_cookie;

		/*
		 * Get the loan pointer, for processing reply, from the cookie.
		 * It's safe to access loan as this get called when
		 * m0_credit_get() succeeds. Hence the loan cookie is valid.
		 */
		loan = m0_cookie_of(&rfom->rf_in.ri_loan_cookie,
				    struct m0_rm_loan, rl_id);

		M0_ASSERT(loan != NULL);
		/*
		 * Memory for the buffer is allocated by the function.
		 */
		rc = m0_rm_credit_encode(&loan->rl_credit,
					 &bfop->br_credit.cr_opaque);
		break;
	default:
		break;
	}
	M0_RETURN(rc);
}

/*
 * Set RM reply FOP error code.
 */
static void reply_err_set(enum m0_rm_incoming_type type,
			 struct m0_fom *fom, int rc)
{
	struct m0_fop_rm_borrow_rep *bfop;
	struct m0_fop_generic_reply *rfop = NULL;

	M0_ENTRY("reply for fom: %p type: %d error: %d", fom, type, rc);

	switch (type) {
	case M0_RIT_BORROW:
		bfop = m0_fop_data(fom->fo_rep_fop);
		rfop = &bfop->br_rc;
		break;
	case M0_RIT_REVOKE:
		rfop = m0_fop_data(fom->fo_rep_fop);
		break;
	default:
		M0_IMPOSSIBLE("Unrecognized RM request");
		break;
	}
	rfop->gr_rc = rc;
	m0_fom_phase_move(fom, rc, rc ? M0_FOPH_FAILURE : M0_FOPH_SUCCESS);
	M0_LEAVE();
}

/*
 * Build an remote-incoming structure using remote request information.
 */
static int incoming_prepare(enum m0_rm_incoming_type type, struct m0_fom *fom)
{
	struct m0_rm_fop_borrow     *bfop;
	struct m0_rm_fop_revoke     *rfop;
	struct m0_rm_fop_req	    *basefop;
	struct m0_rm_incoming	    *in;
	struct m0_rm_owner	    *owner;
	struct rm_request_fom	    *rfom;
	struct m0_buf		    *buf;
	enum m0_rm_incoming_policy   policy;
	uint64_t		     flags;
	int			     rc = 0;

	M0_ENTRY("prepare remote incoming request for fom: %p request: %d",
		 fom, type);

	rfom = container_of(fom, struct rm_request_fom, rf_fom);
	switch (type) {
	case M0_RIT_BORROW:
		bfop = m0_fop_data(fom->fo_fop);
		basefop = &bfop->bo_base;
		/* Remote owner (requester) cookie */
		rfom->rf_in.ri_rem_owner_cookie = basefop->rrq_owner.ow_cookie;
		/*
		 * @todo Figure out how to find local session that can
		 * send RPC back to the debtor.
		 * rfom->rf_in.ri_rem_session = NULL;
		 */
		/*
		 * Populate the owner cookie for creditor (local)
		 * This is used later by locality().
		 */
		/* Possibly M0_COOKIE_NULL */
		rfom->rf_in.ri_owner_cookie = bfop->bo_creditor.ow_cookie;
		break;

	case M0_RIT_REVOKE:
		rfop = m0_fop_data(fom->fo_fop);
		basefop = &rfop->rr_base;
		/*
		 * Populate the owner cookie for debtor (local)
		 * This server is debtor; hence it received REVOKE request.
		 * This is used later by locality().
		 */
		rfom->rf_in.ri_owner_cookie = basefop->rrq_owner.ow_cookie;

		/*
		 * Populate the loan cookie.
		 */
		rfom->rf_in.ri_loan_cookie = rfop->rr_loan.lo_cookie;
		break;

	default:
		M0_IMPOSSIBLE("Unrecognized RM request");
		break;
	}
	policy = basefop->rrq_policy;
	flags = basefop->rrq_flags;
	buf = &basefop->rrq_credit.cr_opaque;
	in = &rfom->rf_in.ri_incoming;
	owner = m0_cookie_of(&rfom->rf_in.ri_owner_cookie,
			     struct m0_rm_owner, ro_id);
	/*
	 * Owner is NULL, create owner for given resource.
	 * Resource description is provided in basefop->rrq_owner.ow_resource
	 */
	if (owner == NULL) {
		/* Owner cannot be NULL for a revoke request */
		M0_ASSERT(type != M0_RIT_REVOKE);
		M0_ALLOC_PTR(owner);
		if (owner == NULL)
			M0_RETURN(-ENOMEM);
		rc = m0_rm_svc_owner_create(fom->fo_service, &owner,
					    &basefop->rrq_owner.ow_resource);
		if (rc != 0) {
			m0_free(owner);
			M0_RETURN(rc);
		}
	}

	m0_rm_incoming_init(in, owner, type, policy, flags);
	in->rin_ops = &remote_incoming_ops;
	rc = m0_rm_credit_decode(&in->rin_want, buf);
	if (rc != 0)
		m0_rm_incoming_fini(in);

	M0_RETURN(rc);
}

/*
 * Prepare incoming request. Send request for the credits.
 */
static int request_pre_process(struct m0_fom *fom,
			       enum m0_rm_incoming_type type)
{
	struct rm_request_fom *rfom;
	struct m0_rm_incoming *in;
	int		       rc;

	M0_ENTRY("pre-processing fom: %p request : %d", fom, type);

	M0_PRE(fom != NULL);
	rfom = container_of(fom, struct rm_request_fom, rf_fom);

	rc = incoming_prepare(type, fom);
	if (rc != 0) {
		/*
		 * This will happen if owner cookie is stale or
		 * copying of credit data fails.
		 */
		reply_err_set(type, fom, rc);
		M0_LEAVE();
		return M0_FSO_AGAIN;
	}

	in = &rfom->rf_in.ri_incoming;
	m0_rm_credit_get(in);

	/*
	 * If m0_rm_incoming goes in WAIT state, then put the fom in wait
	 * queue otherwise proceed with the next (finish) phase.
	 */
	m0_fom_phase_set(fom, incoming_state(in) == RI_WAIT ?
			      FOPH_RM_REQ_WAIT : FOPH_RM_REQ_FINISH);
	M0_LEAVE();
	return incoming_state(in) == RI_WAIT ? M0_FSO_WAIT : M0_FSO_AGAIN;
}

static int request_post_process(struct m0_fom *fom)
{
	struct rm_request_fom *rfom;
	struct m0_rm_incoming *in;
	int		       rc;

	M0_ENTRY("post-processing fom: %p", fom);
	M0_PRE(fom != NULL);

	rfom = container_of(fom, struct rm_request_fom, rf_fom);
	in = &rfom->rf_in.ri_incoming;

	rc = in->rin_rc;
	M0_ASSERT(ergo(rc == 0, incoming_state(in) == RI_SUCCESS));
	M0_ASSERT(ergo(rc != 0, incoming_state(in) == RI_FAILURE));

	if (incoming_state(in) == RI_SUCCESS) {
		rc = reply_prepare(in->rin_type, fom);
		m0_rm_credit_put(in);
	}

	reply_err_set(in->rin_type, fom, rc);
	m0_rm_incoming_fini(in);

	M0_RETURN(M0_FSO_AGAIN);
}

static int request_fom_tick(struct m0_fom *fom,
			    enum m0_rm_incoming_type type)
{
	int rc = 0;

	M0_ENTRY("running fom: %p for request: %d", fom, type);

	if (m0_fom_phase(fom) < M0_FOPH_NR)
		rc = m0_fom_tick_generic(fom);
	else {
		switch (m0_fom_phase(fom)) {
		case FOPH_RM_REQ_START:
			rc = request_pre_process(fom, type);
			break;
		case FOPH_RM_REQ_WAIT:
			m0_fom_phase_set(fom, FOPH_RM_REQ_FINISH);
			rc = M0_FSO_AGAIN;
			break;
		case FOPH_RM_REQ_FINISH:
			rc = request_post_process(fom);
			break;
		default:
			M0_IMPOSSIBLE("Unrecognized RM FOM phase");
			break;
		}

	}/* else - process RM phases */
	M0_RETURN(rc);
}

/**
 * This function handles the request to borrow a credit to a resource on
 * a server ("creditor").
 *
 * @param fom -> fom processing the CREDIT_BORROW request on the server
 *
 */
static int borrow_fom_tick(struct m0_fom *fom)
{
	return request_fom_tick(fom, M0_RIT_BORROW);
}

/**
 * This function handles the request to revoke a credit to a resource on
 * a server ("debtor"). REVOKE is typically issued to the client. In Mero,
 * resources are arranged in hierarchy (chain). Hence a server can receive
 * REVOKE from another server.
 *
 * @param fom -> fom processing the CREDIT_REVOKE request on the server
 *
 */
static int revoke_fom_tick(struct m0_fom *fom)
{
	return request_fom_tick(fom, M0_RIT_REVOKE);
}

/*
 * A borrow FOM constructor.
 */
static int borrow_fom_create(struct m0_fop *fop, struct m0_fom **out,
			     struct m0_reqh *reqh)
{
	return request_fom_create(M0_RIT_BORROW, fop, out, reqh);
}

/*
 * A borrow FOM destructor.
 */
static void borrow_fom_fini(struct m0_fom *fom)
{
	request_fom_fini(fom);
}

/*
 * A revoke FOM constructor.
 */
static int revoke_fom_create(struct m0_fop *fop, struct m0_fom **out,
			     struct m0_reqh *reqh)
{
	return request_fom_create(M0_RIT_REVOKE, fop, out, reqh);
}

/*
 * A revoke FOM destructor.
 */
static void revoke_fom_fini(struct m0_fom *fom)
{
	request_fom_fini(fom);
}

/** @} */

/**
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
