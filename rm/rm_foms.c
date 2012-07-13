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
#include "fop/fom.h"
#include "rm/rm_fops.h"
#include "rm/rm_foms.h"
#include "reqh/reqh.h"

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
static int rm_borrow_fom_state(struct c2_fom *);
static int rm_revoke_fom_state(struct c2_fom *);
static size_t rm_locality(const struct c2_fom *fom);

/*
 * Borrow FOM ops.
 */
static struct c2_fom_ops rm_fom_borrow_ops = {
	.fo_fini = rm_borrow_fom_fini,
	.fo_state = rm_borrow_fom_state,
	.fo_home_locality = rm_locality,
};

static const struct c2_fom_type_ops rm_borrow_fom_type_ops = {
	.fto_create = rm_borrow_fom_create,
};

struct c2_fom_type rm_borow_fom_type = {
	.ft_ops = &rm_borrow_fom_type_ops,
};

/*
 * Revoke/Cancel FOM ops.
 */
static struct c2_fom_ops rm_fom_revoke_ops = {
	.fo_fini = rm_revoke_fom_fini,
	.fo_state = rm_revoke_fom_state,
	.fo_home_locality = rm_locality,
};

static const struct c2_fom_type_ops rm_revoke_fom_type_ops = {
	.fto_create = rm_revoke_fom_create,
};

struct c2_fom_type rm_revoke_fom_type = {
	.ft_ops = &rm_revoke_fom_type_ops,
};

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

	/*
	 * The loan may be consumed by c2_rm_borrow_commit() or
	 * c2_rm_revoke_commit(). It's de-allocated otherwise by the FOM.
	 */
	C2_ALLOC_PTR(rqfom->rf_in.ri_loan);
	if (rqfom->rf_in.ri_loan == NULL) {
		c2_free(rqfom);
		return -ENOMEM;
	}

	switch (type) {
	case RIT_BORROW:
		fopt = &c2_fop_rm_borrow_rep_fopt;
		fom_ops = &rm_fom_borrow_ops;
		break;
	case RIT_REVOKE:
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
	c2_free(rfom->rf_in.ri_loan);
	c2_free(rfom);
}

static size_t rm_locality(const struct c2_fom *fom)
{
	struct rm_request_fom *rfom;

	rfom = container_of(fom, struct rm_request_fom, rf_fom);
	return (size_t)(rfom->rf_in.ri_owner_cookie.cv.u_lo >> 32);
}

/*
 * This function will fill reply FOP data.
 * However, it will not set the return code.
 *
 * @see reply_err_set()
 */
static int reply_prepare(enum c2_rm_incoming_type type, struct c2_fom *fom)
{
	struct c2_fop_rm_borrow_rep  *bfop;
	struct rm_request_fom        *rfom;
	struct c2_cookie	      cookie;
	void			    **buf;
	c2_bcount_t		     *buflen;
	int			      rc = 0;

	rfom = container_of(fom, struct rm_request_fom, rf_fom);

	switch (type) {
	case RIT_BORROW:
		bfop = c2_fop_data(fom->fo_rep_fop);
		c2_rm_loan_cookie_get(rfom->rf_in.ri_loan, &cookie);
		bfop->br_loan.lo_cookie.co_hi = cookie.cv.u_hi;
		bfop->br_loan.lo_cookie.co_lo = cookie.cv.u_lo;

		bfop->br_loan.lo_id = rfom->rf_in.ri_loan->rl_id;

		buf = (void **)&bfop->br_right.ri_opaque.op_bytes;
		buflen = &bfop->br_right.ri_opaque.op_nr;
		/*
		 * Memory for the function is allocated by the function.
		 */
		rc = c2_rm_rdatum2buf(&rfom->rf_in.ri_loan->rl_right,
				      buf, buflen);
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
	case RIT_BORROW:
		bfop = c2_fop_data(fom->fo_rep_fop);
		bfop->br_rc = rc;
		break;
	case RIT_REVOKE:
		rfop = c2_fop_data(fom->fo_rep_fop);
		rfop->re_rc = rc;
		break;
	default:
		C2_IMPOSSIBLE("Unrecognized RM request");
		break;
	}
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
	struct c2_rm_loan	    *loan;
	enum c2_rm_incoming_policy   policy;
	struct c2_cookie	    *ccookie;
	struct c2_cookie	    *dcookie;
	struct c2_cookie	    *lcookie;
	void			    *datum_buf;
	c2_bcount_t		     datum_len;
	uint64_t		     flags;
	int			     rc = 0;

	rfom = container_of(fom, struct rm_request_fom, rf_fom);
	switch (type) {
	case RIT_BORROW:
		bfop = c2_fop_data(fom->fo_fop);
		policy = bfop->bo_policy;
		flags = bfop->bo_flags;
		datum_buf = bfop->bo_right.ri_opaque.op_bytes;
		datum_len = bfop->bo_right.ri_opaque.op_nr;
		/*
		 * Populate the owner cookie for creditor (local)
		 * This is used later by rm_locality().
		 */
		ccookie = &rfom->rf_in.ri_owner_cookie;
		ccookie->cv.u_hi = bfop->bo_creditor.ow_cookie.co_hi;
		ccookie->cv.u_lo = bfop->bo_creditor.ow_cookie.co_lo;
		/*
		 * Populate the owner cookie for debtor (remote end).
		 * (for loan->rl_other->rm_cookie).
		 * We have got debtor cookie, call ...net_locate()??
		 * @todo - How do you set up rl_other??
		 */

		break;

	case RIT_REVOKE:
		rfop = c2_fop_data(fom->fo_fop);
		policy = rfop->rr_policy;
		flags = rfop->rr_flags;
		datum_buf = rfop->rr_right.ri_opaque.op_bytes;
		datum_len = rfop->rr_right.ri_opaque.op_nr;
		/*
		 * Populate the owner cookie for debtor (local)
		 * This server is debtor; hence it received REVOKE reuest.
		 * This is used later by rm_locality().
		 */
		dcookie = &rfom->rf_in.ri_owner_cookie;
		dcookie->cv.u_hi = rfop->rr_debtor.ow_cookie.co_hi;
		dcookie->cv.u_lo = rfop->rr_debtor.ow_cookie.co_lo;
		/*
		 * Populate the loan cookie.
		 */
		lcookie = &rfom->rf_in.ri_loan_cookie;
		lcookie->cv.u_hi = rfop->rr_loan.lo_cookie.co_hi;
		lcookie->cv.u_lo = rfop->rr_loan.lo_cookie.co_lo;
		/*
		 * Check if the loan cookie stale. If the cookie is stale
		 * don't do further processing.
		 */
		loan = c2_rm_loan_find(lcookie);
		rc = loan ? 0: -EPROTO;
		break;

	default:
		C2_IMPOSSIBLE("Unrecognized RM request");
		break;
	}

	if (rc != 0)
		return rc;

	owner = c2_rm_owner_find(&rfom->rf_in.ri_owner_cookie);
	rc = owner ? 0 : -EPROTO;
	if (rc == 0) {
		in = &rfom->rf_in.ri_incoming;
		c2_rm_incoming_init(in, owner, type, policy, flags);
		c2_rm_right_init(&in->rin_want, owner);
		c2_rm_right_init(&rfom->rf_in.ri_loan->rl_right, owner);
		rc = c2_rm_buf2rdatum(&in->rin_want, datum_buf, datum_len);
		if (rc != 0) {
			c2_rm_right_fini(&rfom->rf_in.ri_loan->rl_right);
			c2_rm_right_fini(&in->rin_want);
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
		 * copying of datum fails.
		 */
		reply_err_set(type, fom, rc);
		return C2_FOPH_SUCCESS;
	}

	in = &rfom->rf_in.ri_incoming;
	c2_rm_right_get(in);

	/*
	 * If the request either succeeds or fails, follow with the next phase.
	 * If request is waiting, it will enter the next phase after wake-up.
	 */
	fom->fo_phase = next_phase;
	if (in->rin_state == RI_WAIT) {
		c2_fom_wait_on(fom, &in->rin_signal, &fom->fo_cb);
	}
	return C2_FSO_WAIT;
}

static int request_post_process(struct c2_fom *fom)
{
	struct rm_request_fom *rfom;
	struct c2_rm_incoming *in;
	struct c2_rm_owner    *owner;
	int		       rc;

	C2_PRE(fom != NULL);

	rfom = container_of(fom, struct rm_request_fom, rf_fom);
	in = &rfom->rf_in.ri_incoming;
	owner = in->rin_want.ri_owner;

	C2_ASSERT(in->rin_state == RI_SUCCESS || in->rin_state == RI_FAILURE);

	rc = in->rin_rc;
	if (in->rin_state == RI_SUCCESS) {
		C2_ASSERT(rc == 0);

		rc = reply_prepare(in->rin_type, fom);

		c2_mutex_lock(&owner->ro_lock);
		switch (in->rin_type) {
		case RIT_BORROW:
			rc = rc ?: c2_rm_borrow_commit(&rfom->rf_in);
			break;
		case RIT_REVOKE:
			rc = rc ?: c2_rm_revoke_commit(&rfom->rf_in);
			break;
		default:
			C2_IMPOSSIBLE("Unrecognized RM request");
			break;
		}
		c2_mutex_unlock(&owner->ro_lock);
		c2_rm_right_put(in);
	}

	reply_err_set(in->rin_type, fom, rc);
	c2_rm_right_fini(&rfom->rf_in.ri_loan->rl_right);
	c2_rm_right_fini(&in->rin_want);

	return C2_FOPH_SUCCESS;
}

/**
 * This function handles the request to borrow a right to a resource on
 * a server ("creditor").
 *
 * @param fom -> fom processing the RIGHT_BORROW request on the server
 *
 */
static int rm_borrow_fom_state(struct c2_fom *fom)
{
	int rc;

	if (fom->fo_phase < C2_FOPH_NR)
		rc = c2_fom_state_generic(fom);
	else {
		C2_PRE(fom->fo_phase == FOPH_RM_BORROW ||
		       fom->fo_phase == FOPH_RM_BORROW_WAIT);

		if (fom->fo_phase == FOPH_RM_BORROW)
			rc = request_pre_process(fom, RIT_BORROW,
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
static int rm_revoke_fom_state(struct c2_fom *fom)
{
	int rc;

	if (fom->fo_phase < C2_FOPH_NR)
		rc = c2_fom_state_generic(fom);
	else {
		C2_PRE(fom->fo_phase == FOPH_RM_REVOKE ||
		       fom->fo_phase == FOPH_RM_REVOKE_WAIT);

		if (fom->fo_phase == FOPH_RM_REVOKE)
			rc = request_pre_process(fom, RIT_REVOKE,
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
	return request_fom_create(RIT_BORROW, fop, out);
}

/*
 * A borrow FOM destructor.
 */
static void rm_borrow_fom_fini(struct c2_fom *fom)
{
	struct c2_fop_rm_borrow_rep *rply_fop;

	/*
	 * Free memory allocated by c2_rm_rdatum2buf().
	 */
	rply_fop = c2_fop_data(fom->fo_rep_fop);
	c2_free(rply_fop->br_right.ri_opaque.op_bytes);

	request_fom_fini(fom);
}

/*
 * A revoke FOM constructor.
 */
static int rm_revoke_fom_create(struct c2_fop *fop, struct c2_fom **out)
{
	return request_fom_create(RIT_REVOKE, fop, out);
}

/*
 * A revoke FOM destructor.
 */
static void rm_revoke_fom_fini(struct c2_fom *fom)
{
	request_fom_fini(fom);
}

/**
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
