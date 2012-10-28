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
#include "lib/list.h"
#include "lib/memory.h"
#include "lib/misc.h"   /* C2_IN */
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
static int borrow_fom_create(struct c2_fop *fop, struct c2_fom **out);
static void borrow_fom_fini(struct c2_fom *fom);
static int revoke_fom_create(struct c2_fop *fop, struct c2_fom **out);
static void revoke_fom_fini(struct c2_fom *fom);
static int borrow_fom_tick(struct c2_fom *);
static int reovke_fom_tick(struct c2_fom *);
static size_t locality(const struct c2_fom *fom);

static void remote_incoming_complete(struct c2_rm_incoming *in, int32_t rc);
static void remote_incoming_conflict(struct c2_rm_incoming *in);

/*
 * As part of of incoming_complete(), call remote_incoming complete.
 * This will call request specific functions.
 */
static struct c2_rm_incoming_ops remote_incoming_ops = {
	.rio_complete = remote_incoming_complete,
	.rio_conflict = remote_incoming_conflict,
};

/*
 * Borrow FOM ops.
 */
static struct c2_fom_ops rm_fom_borrow_ops = {
	.fo_fini          = borrow_fom_fini,
	.fo_tick          = borrow_fom_tick,
	.fo_home_locality = locality,
};

const struct c2_fom_type_ops rm_borrow_fom_type_ops = {
	.fto_create = borrow_fom_create,
};

const struct c2_sm_state_descr borrow_states[] = {
	[FOPH_RM_BORROW] = {
		.sd_name      = "Borrow Begin",
		.sd_allowed   = C2_BITS(FOPH_RM_BORROW_WAIT, C2_FOPH_FAILURE)
	},
	[FOPH_RM_BORROW_WAIT] = {
		.sd_name      = "Borrow Completion Wait",
		.sd_allowed   = C2_BITS(C2_FOPH_SUCCESS, C2_FOPH_FAILURE)
	}
};

const struct c2_sm_conf borrow_sm_conf = {
	.scf_name      = "Borrow FOM conf",
	.scf_nr_states = ARRAY_SIZE(borrow_states),
	.scf_state     = borrow_states
};

/*
 * Revoke FOM ops.
 */
static struct c2_fom_ops rm_fom_revoke_ops = {
	.fo_fini          = revoke_fom_fini,
	.fo_tick          = reovke_fom_tick,
	.fo_home_locality = locality,
};

const struct c2_fom_type_ops rm_revoke_fom_type_ops = {
	.fto_create = revoke_fom_create,
};

const struct c2_sm_state_descr revoke_states[] = {
	[FOPH_RM_REVOKE] = {
		.sd_name      = "Revoke Begin",
		.sd_allowed   = C2_BITS(FOPH_RM_REVOKE_WAIT, C2_FOPH_FAILURE)
	},
	[FOPH_RM_REVOKE_WAIT] = {
		.sd_name      = "Revoke Completion Wait",
		.sd_allowed   = C2_BITS(C2_FOPH_SUCCESS, C2_FOPH_FAILURE)
	}
};

const struct c2_sm_conf revoke_sm_conf = {
	.scf_name      = "Revoke FOM conf",
	.scf_nr_states = ARRAY_SIZE(revoke_states),
	.scf_state     = revoke_states
};

static void remote_incoming_complete(struct c2_rm_incoming *in, int32_t rc)
{
	struct c2_rm_remote_incoming *rem_in;

	if (rc != 0)
		return;

	rem_in = container_of(in, struct c2_rm_remote_incoming, ri_incoming);
	switch (in->rin_type) {
	case C2_RIT_BORROW:
		rc = c2_rm_borrow_commit(rem_in);
		break;
	case C2_RIT_REVOKE:
		rc = c2_rm_revoke_commit(rem_in);
		break;
	default:
		C2_IMPOSSIBLE("Unrecognized RM request");
		break;
	}
	/*
	 * Override the rc.
	 */
	in->rin_sm.sm_rc = rc;
}

/*
 * Not used by base-RM code yet.
 * @todo Revisit during inspection.
 */
static void remote_incoming_conflict(struct c2_rm_incoming *in)
{
}

/*
 * Generic RM request-FOM constructor.
 */
static int request_fom_create(enum c2_rm_incoming_type type,
			      struct c2_fop *fop, struct c2_fom **out)
{
	struct rm_request_fom *rqfom;
	struct c2_fop_type    *fopt;
	struct c2_fom_ops     *fom_ops;
	void		      *fop_data;

	C2_PRE(fop != NULL);
	C2_PRE(fop->f_type != NULL);
	C2_PRE(out != NULL);

	C2_ALLOC_PTR(rqfom);
	if (rqfom == NULL)
		return -ENOMEM;

	switch (type) {
	case C2_RIT_BORROW:
		fopt = &c2_fop_rm_borrow_rep_fopt;
		fom_ops = &rm_fom_borrow_ops;
		fop_data = &rqfom->rf_rep_fop_data.rr_borrow_rep;
		break;
	case C2_RIT_REVOKE:
		fopt = &c2_fom_error_rep_fopt;
		fom_ops = &rm_fom_revoke_ops;
		fop_data = &rqfom->rf_rep_fop_data.rr_req_rep;
		break;
	default:
		C2_IMPOSSIBLE("Unrecognised RM request");
		break;
	}

	c2_fop_init(&rqfom->rf_rep_fop, fopt, fop_data);
	c2_fom_init(&rqfom->rf_fom, &fop->f_type->ft_fom_type,
		    fom_ops, fop, &rqfom->rf_rep_fop);
	*out = &rqfom->rf_fom;
	return 0;
}

/*
 * Generic RM request-FOM destructor.
 */
static void request_fom_fini(struct c2_fom *fom)
{
	struct rm_request_fom *rfom;

	C2_PRE(fom != NULL);

	rfom = container_of(fom, struct rm_request_fom, rf_fom);
	c2_fom_fini(fom);
	c2_free(rfom);
}

static size_t locality(const struct c2_fom *fom)
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
static int reply_prepare(const enum c2_rm_incoming_type type,
			 struct c2_fom *fom)
{
	struct c2_fop_rm_borrow_rep *bfop;
	struct rm_request_fom       *rfom;
	struct c2_rm_loan	    *loan;
	int			     rc = 0;

	rfom = container_of(fom, struct rm_request_fom, rf_fom);

	switch (type) {
	case C2_RIT_BORROW:
		bfop = c2_fop_data(fom->fo_rep_fop);
		bfop->br_loan.lo_cookie = rfom->rf_in.ri_loan_cookie;

		/*
		 * Get the loan pointer for processing reply from the cookie.
		 * It's safe to access loan as this get called when
		 * c2_right_get() succeeds. Hence loan cookie is valid.
		 */
		loan = c2_cookie_of(&rfom->rf_in.ri_loan_cookie,
				    struct c2_rm_loan, rl_id);

		C2_ASSERT(loan != NULL);
		/*
		 * Memory for the buffer is allocated by the function.
		 */
		rc = c2_rm_right_encode(&loan->rl_right,
					&bfop->br_right.ri_opaque);
		break;
	default:
		break;
	}
	return rc;
}

/*
 * Set RM reply FOP error code.
 */
static void reply_err_set(enum c2_rm_incoming_type type,
			 struct c2_fom *fom, int rc)
{
	struct c2_fop_rm_borrow_rep *bfop;
	struct c2_fom_error_rep *rfop;

	switch (type) {
	case C2_RIT_BORROW:
		bfop = c2_fop_data(fom->fo_rep_fop);
		rfop = &bfop->br_rc;
		break;
	case C2_RIT_REVOKE:
		rfop = c2_fop_data(fom->fo_rep_fop);
		break;
	default:
		C2_IMPOSSIBLE("Unrecognized RM request");
		break;
	}
	rfop->rerr_rc = rc;
	c2_fom_phase_set(fom, rc ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
}

/*
 * Build an remote-incoming structure using remote request information.
 */
static int incoming_prepare(enum c2_rm_incoming_type type, struct c2_fom *fom)
{
	struct c2_fop_rm_borrow     *bfop;
	struct c2_fop_rm_revoke     *rfop;
	struct c2_fop_rm_req	    *basefop;
	struct c2_rm_incoming	    *in;
	struct c2_rm_owner	    *owner;
	struct rm_request_fom	    *rfom;
	struct c2_buf		    *buf;
	enum c2_rm_incoming_policy   policy;
	uint64_t		     flags;
	int			     rc = 0;

	rfom = container_of(fom, struct rm_request_fom, rf_fom);
	switch (type) {
	case C2_RIT_BORROW:
		bfop = c2_fop_data(fom->fo_fop);
		basefop = &bfop->bo_base;
		/*
		 * Populate the owner cookie for creditor (local)
		 * This is used later by locality().
		 */
		rfom->rf_in.ri_owner_cookie = bfop->bo_creditor.ow_cookie;
		break;

	case C2_RIT_REVOKE:
		rfop = c2_fop_data(fom->fo_fop);
		basefop = &rfop->rr_base;
		/*
		 * Populate the owner cookie for debtor (local)
		 * This server is debtor; hence it received REVOKE request.
		 * This is used later by locality().
		 */
		rfom->rf_in.ri_owner_cookie =
			rfop->rr_base.rrq_owner.ow_cookie;

		/*
		 * Populate the loan cookie.
		 */
		rfom->rf_in.ri_loan_cookie = rfop->rr_loan.lo_cookie;
		break;

	default:
		C2_IMPOSSIBLE("Unrecognized RM request");
		break;
	}
	policy = basefop->rrq_policy;
	flags = basefop->rrq_flags;
	buf = &basefop->rrq_right.ri_opaque;

	if (rc != 0)
		return rc;

	owner = c2_cookie_of(&rfom->rf_in.ri_owner_cookie, struct c2_rm_owner,
			     ro_id);
	rc = owner ? 0 : -EPROTO;
	if (rc == 0) {
		in = &rfom->rf_in.ri_incoming;
		c2_rm_incoming_init(in, owner, type, policy, flags);
		in->rin_ops = &remote_incoming_ops;
		c2_rm_right_init(&in->rin_want, owner);
		rc = c2_rm_right_decode(&in->rin_want, buf);
		if (rc != 0) {
			c2_rm_right_fini(&in->rin_want);
			c2_rm_incoming_fini(in);
		}
	}
	return rc;
}

/*
 * Prepare incoming request. Send request for the rights.
 */
static int request_pre_process(struct c2_fom *fom,
			       enum c2_rm_incoming_type type,
			       enum c2_rm_fom_phases next_phase)
{
	struct rm_request_fom *rfom;
	struct c2_rm_incoming *in;
	int		       rc;

	C2_PRE(fom != NULL);

	rfom = container_of(fom, struct rm_request_fom, rf_fom);

	rc = incoming_prepare(type, fom);
	if (rc != 0) {
		/*
		 * This will happen if owner cookie is stale or
		 * copying of right data fails.
		 */
		reply_err_set(type, fom, rc);
		return C2_FSO_AGAIN;
	}

	in = &rfom->rf_in.ri_incoming;
	c2_rm_right_get(in);

	c2_fom_phase_set(fom, next_phase);
	/*
	 * If c2_rm_incoming goes in WAIT state, the put the fom in wait
	 * queue otherwise proceed with the next phase.
	 */
	return incoming_state(in) == RI_WAIT ? C2_FSO_WAIT : C2_FSO_AGAIN;
}

static int request_post_process(struct c2_fom *fom)
{
	struct rm_request_fom *rfom;
	struct c2_rm_incoming *in;
	int		       rc;

	C2_PRE(fom != NULL);

	rfom = container_of(fom, struct rm_request_fom, rf_fom);
	in = &rfom->rf_in.ri_incoming;

	rc = in->rin_sm.sm_rc;
	if (incoming_state(in) == RI_SUCCESS) {
		C2_ASSERT(rc == 0);
		rc = reply_prepare(in->rin_type, fom);
		c2_rm_right_put(in);
	}

	reply_err_set(in->rin_type, fom, rc);
	c2_rm_right_fini(&in->rin_want);

	return C2_FSO_AGAIN;
}

static int request_fom_tick(struct c2_fom *fom,
			    enum c2_rm_incoming_type type,
			    enum c2_rm_fom_phases next_phase)
{
	int rc;

	if (c2_fom_phase(fom) < C2_FOPH_NR)
		rc = c2_fom_tick_generic(fom);
	else {
		/*
		 * The same code is executed for REVOKE. The case statements
		 * for REVOKE have not been added because, they have same
		 * phase values as BORROW. It causes compilation error.
		 * In future, if the phase values of REVOKE change, please
		 * add the appropriate case statements below.
		 */
		switch (c2_fom_phase(fom)) {
		case FOPH_RM_BORROW:
			rc = request_pre_process(fom, type, next_phase);
			break;
		case FOPH_RM_BORROW_WAIT:
			rc = request_post_process(fom);
			break;
		default:
			C2_IMPOSSIBLE("Unrecognized RM FOM phase");
			break;
		}

	}/* else - process RM phases */
	return rc;
}

/**
 * This function handles the request to borrow a right to a resource on
 * a server ("creditor").
 *
 * @param fom -> fom processing the RIGHT_BORROW request on the server
 *
 */
static int borrow_fom_tick(struct c2_fom *fom)
{
	C2_PRE(C2_IN(c2_fom_phase(fom), (FOPH_RM_BORROW, FOPH_RM_BORROW_WAIT)));
	return request_fom_tick(fom, C2_RIT_BORROW, FOPH_RM_BORROW_WAIT);
}

/**
 * This function handles the request to revoke a right to a resource on
 * a server ("debtor"). REVOKE is typically issued to the client. In Colibri,
 * resources are arranged in hierarchy (chain). Hence a server can receive
 * REVOKE from another server.
 *
 * @param fom -> fom processing the RIGHT_REVOKE request on the server
 *
 */
static int reovke_fom_tick(struct c2_fom *fom)
{
	C2_PRE(C2_IN(c2_fom_phase(fom), (FOPH_RM_REVOKE, FOPH_RM_REVOKE_WAIT)));
	return request_fom_tick(fom, C2_RIT_REVOKE, FOPH_RM_REVOKE_WAIT);
}

/*
 * A borrow FOM constructor.
 */
static int borrow_fom_create(struct c2_fop *fop, struct c2_fom **out)
{
	return request_fom_create(C2_RIT_BORROW, fop, out);
}

/*
 * A borrow FOM destructor.
 */
static void borrow_fom_fini(struct c2_fom *fom)
{
	struct c2_fop_rm_borrow_rep *rply_fop;

	/*
	 * Free memory allocated by c2_rm_right_encode().
	 */
	rply_fop = c2_fop_data(fom->fo_rep_fop);
	c2_buf_free(&rply_fop->br_right.ri_opaque);

	request_fom_fini(fom);
}

/*
 * A revoke FOM constructor.
 */
static int revoke_fom_create(struct c2_fop *fop, struct c2_fom **out)
{
	return request_fom_create(C2_RIT_REVOKE, fop, out);
}

/*
 * A revoke FOM destructor.
 */
static void revoke_fom_fini(struct c2_fom *fom)
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
