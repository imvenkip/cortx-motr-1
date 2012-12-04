/* -*- C -*- */
/*
 * COPYCREDIT 2012 XYRATEX TECHNOLOGY LIMITED
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
#include "lib/list.h"
#include "lib/memory.h"
#include "lib/misc.h"   /* M0_IN */
#include "fop/fom_generic.h"
#include "rm/rm_fops.h"
#include "rm/rm_foms.h"

/**
   @addtogroup rm

   This file includes data structures and function used by RM:fop layer.

   @{
 */

/**
 * Forward declaration
 */
static int borrow_fom_create(struct m0_fop *fop, struct m0_fom **out);
static void borrow_fom_fini(struct m0_fom *fom);
static int revoke_fom_create(struct m0_fop *fop, struct m0_fom **out);
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

/*
 * Borrow FOM ops.
 */
static struct m0_fom_ops rm_fom_borrow_ops = {
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
		.sd_allowed   = M0_BITS(FOPH_RM_REQ_WAIT, FOPH_RM_REQ_FINISH, M0_FOPH_FAILURE)
	},
	[FOPH_RM_REQ_WAIT] = {
		.sd_name      = "RM Request Wait",
		.sd_allowed   = M0_BITS(FOPH_RM_REQ_FINISH, M0_FOPH_SUCCESS, M0_FOPH_FAILURE)
	},
	[FOPH_RM_REQ_FINISH] = {
		.sd_name      = "RM Request Completion",
		.sd_allowed   = M0_BITS(M0_FOPH_SUCCESS, M0_FOPH_FAILURE)
	},
};

const struct m0_sm_conf borrow_sm_conf = {
	.scf_name      = "Borrow FOM conf",
	.scf_nr_states = ARRAY_SIZE(rm_req_phases),
	.scf_state     = rm_req_phases
};

const struct m0_sm_conf revoke_sm_conf = {
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
			      struct m0_fop *fop, struct m0_fom **out)
{
	struct rm_request_fom *rqfom;
	struct m0_fop_type    *fopt;
	struct m0_fom_ops     *fom_ops;
	struct m0_fop	      *reply_fop;

	M0_PRE(fop != NULL);
	M0_PRE(fop->f_type != NULL);
	M0_PRE(out != NULL);

	M0_ALLOC_PTR(rqfom);
	if (rqfom == NULL)
		return -ENOMEM;

	switch (type) {
	case M0_RIT_BORROW:
		fopt = &m0_fop_rm_borrow_rep_fopt;
		fom_ops = &rm_fom_borrow_ops;
		break;
	case M0_RIT_REVOKE:
		fopt = &m0_fom_error_rep_fopt;
		fom_ops = &rm_fom_revoke_ops;
		break;
	default:
		M0_IMPOSSIBLE("Unrecognised RM request");
		break;
	}

	reply_fop = m0_fop_alloc(fopt, NULL);
	if (reply_fop == NULL) {
		m0_free(rqfom);
		return -ENOMEM;
	}

	m0_fom_init(&rqfom->rf_fom, &fop->f_type->ft_fom_type,
		    fom_ops, fop, reply_fop);
	*out = &rqfom->rf_fom;
	return 0;
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
	struct m0_fop_rm_borrow_rep *bfop;
	struct rm_request_fom       *rfom;
	struct m0_rm_loan	    *loan;
	int			     rc = 0;

	rfom = container_of(fom, struct rm_request_fom, rf_fom);

	switch (type) {
	case M0_RIT_BORROW:
		bfop = m0_fop_data(fom->fo_rep_fop);
		bfop->br_loan.lo_cookie = rfom->rf_in.ri_loan_cookie;

		/*
		 * Get the loan pointer for processing reply from the cookie.
		 * It's safe to access loan as this get called when
		 * m0_credit_get() succeeds. Hence loan cookie is valid.
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
	return rc;
}

/*
 * Set RM reply FOP error code.
 */
static void reply_err_set(enum m0_rm_incoming_type type,
			 struct m0_fom *fom, int rc)
{
	struct m0_fop_rm_borrow_rep *bfop;
	struct m0_fom_error_rep *rfop;

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
	rfop->rerr_rc = rc;
	m0_fom_phase_move(fom, rc, rc ? M0_FOPH_FAILURE : M0_FOPH_SUCCESS);
}

/*
 * Build an remote-incoming structure using remote request information.
 */
static int incoming_prepare(enum m0_rm_incoming_type type, struct m0_fom *fom)
{
	struct m0_fop_rm_borrow     *bfop;
	struct m0_fop_rm_revoke     *rfop;
	struct m0_fop_rm_req	    *basefop;
	struct m0_rm_incoming	    *in;
	struct m0_rm_owner	    *owner;
	struct rm_request_fom	    *rfom;
	struct m0_buf		    *buf;
	enum m0_rm_incoming_policy   policy;
	uint64_t		     flags;
	int			     rc = 0;

	rfom = container_of(fom, struct rm_request_fom, rf_fom);
	switch (type) {
	case M0_RIT_BORROW:
		bfop = m0_fop_data(fom->fo_fop);
		basefop = &bfop->bo_base;
		rfom->rf_in.ri_rem_owner_cookie = basefop->rrq_owner.ow_cookie;
		/*
		 * @todo Figure out how to find local session that can
		 * send RPC back to the debtor.
		   rfom->rf_in.ri_rem_session = NULL;
		 */
		/*
		 * Populate the owner cookie for creditor (local)
		 * This is used later by locality().
		 */
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

	if (rc != 0)
		return rc;

	owner = m0_cookie_of(&rfom->rf_in.ri_owner_cookie, struct m0_rm_owner,
			     ro_id);
	rc = owner ? 0 : -EPROTO;
	if (rc == 0) {
		in = &rfom->rf_in.ri_incoming;
		m0_rm_incoming_init(in, owner, type, policy, flags);
		in->rin_ops = &remote_incoming_ops;
		m0_rm_credit_init(&in->rin_want, owner);
		rc = m0_rm_credit_decode(&in->rin_want, buf);
		if (rc != 0) {
			m0_rm_credit_fini(&in->rin_want);
			m0_rm_incoming_fini(in);
		}
	}
	return rc;
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

	M0_PRE(fom != NULL);

	rfom = container_of(fom, struct rm_request_fom, rf_fom);

	rc = incoming_prepare(type, fom);
	if (rc != 0) {
		/*
		 * This will happen if owner cookie is stale or
		 * copying of credit data fails.
		 */
		reply_err_set(type, fom, rc);
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
	return incoming_state(in) == RI_WAIT ? M0_FSO_WAIT : M0_FSO_AGAIN;
}

static int request_post_process(struct m0_fom *fom)
{
	struct rm_request_fom *rfom;
	struct m0_rm_incoming *in;
	int		       rc;

	M0_PRE(fom != NULL);

	rfom = container_of(fom, struct rm_request_fom, rf_fom);
	in = &rfom->rf_in.ri_incoming;

	rc = in->rin_rc;
	if (incoming_state(in) == RI_SUCCESS) {
		M0_ASSERT(rc == 0);
		rc = reply_prepare(in->rin_type, fom);
		m0_rm_credit_put(in);
	}

	reply_err_set(in->rin_type, fom, rc);
	m0_rm_credit_fini(&in->rin_want);

	return M0_FSO_AGAIN;
}

static int request_fom_tick(struct m0_fom *fom,
			    enum m0_rm_incoming_type type)
{
	int rc;

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
	return rc;
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
static int borrow_fom_create(struct m0_fop *fop, struct m0_fom **out)
{
	return request_fom_create(M0_RIT_BORROW, fop, out);
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
static int revoke_fom_create(struct m0_fop *fop, struct m0_fom **out)
{
	return request_fom_create(M0_RIT_REVOKE, fop, out);
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
