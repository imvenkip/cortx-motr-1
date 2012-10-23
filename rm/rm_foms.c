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

#include "lib/chan.h"
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
static int rm_borrow_fom_create(struct c2_fop *fop, struct c2_fom **out);
static void rm_borrow_fom_fini(struct c2_fom *fom);
static int rm_revoke_fom_create(struct c2_fop *fop, struct c2_fom **out);
static void rm_revoke_fom_fini(struct c2_fom *fom);
static int rm_borrow_fom_tick(struct c2_fom *);
static int rm_revoke_fom_tick(struct c2_fom *);
static size_t rm_locality(const struct c2_fom *fom);

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
	.fo_fini          = rm_borrow_fom_fini,
	.fo_tick          = rm_borrow_fom_tick,
	.fo_home_locality = rm_locality,
};

const struct c2_fom_type_ops rm_borrow_fom_type_ops = {
	.fto_create = rm_borrow_fom_create,
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
	.fo_fini          = rm_revoke_fom_fini,
	.fo_tick          = rm_revoke_fom_tick,
	.fo_home_locality = rm_locality,
};

const struct c2_fom_type_ops rm_revoke_fom_type_ops = {
	.fto_create = rm_revoke_fom_create,
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
	struct c2_fop	      *rep_fop;
	struct c2_fop_type    *fopt;
	struct c2_fom_ops     *fom_ops;

	C2_PRE(fop != NULL);
	C2_PRE(fop->f_type != NULL);
	C2_PRE(out != NULL);

	C2_ALLOC_PTR(rqfom);
	if (rqfom == NULL)
		return -ENOMEM;


	switch (type) {
	case C2_RIT_BORROW:
		/*
		 * The loan may be consumed by c2_rm_borrow_commit().
		 * It's de-allocated otherwise by the FOM.
		 */
		C2_ALLOC_PTR(rqfom->rf_in.ri_loan);
		if (rqfom->rf_in.ri_loan == NULL) {
			c2_free(rqfom);
			return -ENOMEM;
		}
		fopt = &c2_fop_rm_borrow_rep_fopt;
		fom_ops = &rm_fom_borrow_ops;
		break;
	case C2_RIT_REVOKE:
		fopt = &c2_fop_rm_revoke_rep_fopt;
		fom_ops = &rm_fom_revoke_ops;
		break;
	default:
		C2_IMPOSSIBLE("Unrecognised RM request");
		break;
	}

	rep_fop = c2_fop_alloc(fopt, NULL);
	if (rep_fop == NULL) {
		c2_free(rqfom->rf_in.ri_loan);
		c2_free(rqfom);
		return -ENOMEM;
	}

	c2_fom_init(&rqfom->rf_fom, &fop->f_type->ft_fom_type,
		    fom_ops, fop, rep_fop);
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
	/*
	 * c2_free() does check for NULL pointer. We are checking for
	 * NULL pointer because we are calling c2_rm_remote_fini().
	 */
	if (rfom->rf_in.ri_loan != NULL &&
	    rfom->rf_in.ri_loan->rl_other != NULL) {
		c2_rm_remote_fini(rfom->rf_in.ri_loan->rl_other);
		c2_free(rfom->rf_in.ri_loan->rl_other);
	}
	c2_free(rfom->rf_in.ri_loan);
	c2_free(rfom);
}

static size_t rm_locality(const struct c2_fom *fom)
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
		 * The loan is consumed by c2_rm_borrow_commit().
		 * Get the loan pointer for processing reply from the cookie.
		 */
		loan = c2_cookie_of(&rfom->rf_in.ri_loan_cookie,
				    struct c2_rm_loan, rl_id);

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
	struct c2_fop_rm_revoke_rep *rfop;

	switch (type) {
	case C2_RIT_BORROW:
		bfop = c2_fop_data(fom->fo_rep_fop);
		bfop->br_rc = rc;
		break;
	case C2_RIT_REVOKE:
		rfop = c2_fop_data(fom->fo_rep_fop);
		rfop->re_rc = rc;
		break;
	default:
		C2_IMPOSSIBLE("Unrecognized RM request");
		break;
	}
	c2_fom_phase_set(fom, rc ? C2_FOPH_FAILURE : C2_FOPH_SUCCESS);
}

static int loan_setup(struct c2_rm_loan *loan,
		       struct c2_rm_incoming *in,
		       struct c2_fop_rm_borrow *bfop,
		       struct c2_cookie *loan_cookie)
{
	int rc;
	/*
	 * Populate the owner cookie for debtor (remote end).
	 * (for loan->rl_other->rm_cookie).
	 * We have got debtor cookie, call ...net_locate()??
	 * @todo : Once we figure out the RM configuration interfaces,
	 *         this code will change.
	 */

	rc = c2_rm_loan_init(loan, &in->rin_want);
	if (rc == 0) {
		C2_ALLOC_PTR(loan->rl_other);
		if (loan->rl_other == NULL)
			rc = -ENOMEM;
		else {
			c2_rm_remote_init(loan->rl_other,
					  in->rin_want.ri_owner->ro_resource);
			loan->rl_other->rem_state = REM_SERVICE_LOCATED;
			loan->rl_other->rem_cookie =
				bfop->bo_base.rrq_debtor.ow_cookie;
			/*
			 * Store loan cookie for reply processing.
			 */
			c2_cookie_init(loan_cookie, &loan->rl_id);
		}
	}
	return rc;
}

/*
 * Build an remote-incoming structure using remote request information.
 */
static int incoming_prepare(enum c2_rm_incoming_type type, struct c2_fom *fom)
{
	struct c2_fop_rm_borrow     *bfop;
	struct c2_fop_rm_revoke     *rfop;
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
		policy = bfop->bo_base.rrq_policy;
		flags = bfop->bo_base.rrq_flags;
		buf = &bfop->bo_base.rrq_right.ri_opaque;
		/*
		 * Populate the owner cookie for creditor (local)
		 * This is used later by rm_locality().
		 */
		rfom->rf_in.ri_owner_cookie = bfop->bo_creditor.ow_cookie;
		break;

	case C2_RIT_REVOKE:
		rfop = c2_fop_data(fom->fo_fop);
		policy = rfop->rr_base.rrq_policy;
		flags = rfop->rr_base.rrq_flags;
		buf = &rfop->rr_base.rrq_right.ri_opaque;
		/*
		 * Populate the owner cookie for debtor (local)
		 * This server is debtor; hence it received REVOKE request.
		 * This is used later by rm_locality().
		 */
		rfom->rf_in.ri_owner_cookie =
			rfop->rr_base.rrq_debtor.ow_cookie;

		/*
		 * Populate the loan cookie.
		 */
		rfom->rf_in.ri_loan_cookie = rfop->rr_loan.lo_cookie;
		/*
		 * Check if the loan cookie is stale. If the cookie is stale
		 * don't proceed with the reovke processing.
		 */
		rfom->rf_in.ri_loan =
			c2_cookie_of(&rfop->rr_loan.lo_cookie,
				     struct c2_rm_loan, rl_id);
		rc = rfom->rf_in.ri_loan ? 0: -EPROTO;
		break;

	default:
		C2_IMPOSSIBLE("Unrecognized RM request");
		break;
	}

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
		} else
			if (type == C2_RIT_BORROW) {
				c2_rm_right_init(&rfom->rf_in.ri_loan->rl_right,
						 owner);
				rc = loan_setup(rfom->rf_in.ri_loan, in, bfop,
						&rfom->rf_in.ri_loan_cookie);

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
	} else
		/*
		 * This will happen if request fails for a valid loan.
		 * We should not free up the loan in this case.
		 */
		if (in->rin_type == C2_RIT_REVOKE)
			rfom->rf_in.ri_loan = NULL;

	reply_err_set(in->rin_type, fom, rc);
	c2_rm_right_fini(&in->rin_want);

	return C2_FSO_AGAIN;
}

/**
 * This function handles the request to borrow a right to a resource on
 * a server ("creditor").
 *
 * @param fom -> fom processing the RIGHT_BORROW request on the server
 *
 */
static int rm_borrow_fom_tick(struct c2_fom *fom)
{
	int rc;

	if (c2_fom_phase(fom) < C2_FOPH_NR)
		rc = c2_fom_tick_generic(fom);
	else {
		C2_PRE(C2_IN(c2_fom_phase(fom), (FOPH_RM_BORROW,
						 FOPH_RM_BORROW_WAIT)));

		if (c2_fom_phase(fom) == FOPH_RM_BORROW)
			rc = request_pre_process(fom, C2_RIT_BORROW,
						 FOPH_RM_BORROW_WAIT);
		else
			rc = request_post_process(fom);

	}/* else - process RM phases */

	return rc;
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
static int rm_revoke_fom_tick(struct c2_fom *fom)
{
	int rc;

	if (c2_fom_phase(fom) < C2_FOPH_NR)
		rc = c2_fom_tick_generic(fom);
	else {
		C2_PRE(C2_IN(c2_fom_phase(fom), (FOPH_RM_REVOKE,
						 FOPH_RM_REVOKE_WAIT)));

		if (c2_fom_phase(fom) == FOPH_RM_REVOKE)
			rc = request_pre_process(fom, C2_RIT_REVOKE,
						 FOPH_RM_REVOKE_WAIT);
		else
			rc = request_post_process(fom);

	}/* else - process RM phases */

	return rc;
}

/*
 * A borrow FOM constructor.
 */
static int rm_borrow_fom_create(struct c2_fop *fop, struct c2_fom **out)
{
	return request_fom_create(C2_RIT_BORROW, fop, out);
}

/*
 * A borrow FOM destructor.
 */
static void rm_borrow_fom_fini(struct c2_fom *fom)
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
static int rm_revoke_fom_create(struct c2_fop *fop, struct c2_fom **out)
{
	return request_fom_create(C2_RIT_REVOKE, fop, out);
}

/*
 * A revoke FOM destructor.
 */
static void rm_revoke_fom_fini(struct c2_fom *fom)
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
