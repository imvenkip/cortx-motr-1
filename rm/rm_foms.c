/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
#include "fop/fop.h"
#include "fop/fop_format.h"
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
int rm_borrow_fom_state(struct c2_fom *);
int rm_canoke_fom_state(struct c2_fom *);

/*
 * Borrow FOM ops.
 */
static struct c2_fom_ops rm_fom_borrow_ops = {
	.fo_fini = rm_borrow_fom_fini,
	.fo_state = rm_borrow_fom_state,
	.fo_home_locality = ??,
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
static struct c2_fom_ops rm_fom_canoke_ops = {
	.fo_fini = rm_canoke_fom_fini,
	.fo_state = rm_canoke_fom_state,
	.fo_home_locality = ??,
};

static const struct c2_fom_type_ops rm_canoke_fom_type_ops = {
	.fto_create = rm_canoke_fom_create,
};

struct c2_fom_type rm_borow_fom_type = {
	.ft_ops = &rm_canoke_fom_type_ops,
};

/*
 * A borrow FOM constructor.
 */
static int rm_borrow_fom_create(struct c2_fop *fop, struct c2_fom **out)
{
	struct rm_borrow_fom *bfom;
	struct c2_fop	     *rfop;

	C2_PRE(fop != NULL);
	C2_PRE(fop->f_type != NULL);
	C2_PRE(out != NULL);

	C2_ALLOC_PTR(bfom);
	if (bfom == NULL) {
		return -ENOMEM;
	}
	*out = &bfom->bom_fom;

	/*
	 * The loan is either consumed or de-alloacted in
	 * c2_rm_borrow_commit()
	 */
	C2_ALLOC_PTR(bfom->bom_incoming.bi_loan);
	if (rfop == NULL) {
		c2_free(bfom);
		return -ENOMEM;
	}

	rfop = c2_fop_alloc(&c2_fop_rm_borrow_rep_fopt, NULL);
	if (rfop == NULL) {
		c2_free(bfom->bom_incoming.bi_loan);
		c2_free(bfom);
		return -ENOMEM;
	}
	c2_fom_init(fom, &fop->f_type->ft_fom_type, rm_fom_borrow_ops,
		    fop, rfop);
}

/**
 * This function handles the request to borrow a right to a resource on
 * a server ("creditor").
 *
 * @param fom -> fom processing the RIGHT_BORROW request on the server
 *
 */
int c2_rm_fom_borrow_state(struct c2_fom *fom)
{
	int rc;

	if (fom->fo_phase < FOPH_NR) {
		rc = c2_fom_state_generic(fom);
	} else {
		C2_PRE(fom->fo_phase == FOPH_RM_BORROW ||
		       fom->fo_phase == FOPH_RM_BORROW_WAIT)	

		if (fom->fo_phase == FOPH_RM_BORROW)
			rc = borrow_state(fom);
		else
			rc = post_borrow_state(fom);

	}/* else - process RM phases */

	return rc;
}

static int borrow_state(struct c2_fom *fom)
{
	struct c2_fop_rm_borrow *bfop;
	struct c2_rm_incoming	*in;
	struct rm_borrow_fom	*bfom;
	struct c2_rm_owner	*owner;
	struct c2_rm_right	 right;
	int			 rc;

	C2_PRE(fom != NULL);

	bfom = container_of(fom, struct rm_borrow_fom, bom_fom);
	bfop = c2_fop_data(fom.fo_fop);
	rc = c2_rm_find_owner(&bfop->bo_owner.ow_cookie, &owner);
	if (rc != 0) {
		/*
		 * This will happen only if the cookie is stale.
		 */
		rep->br_rc = rc;
		return FSO_DONE;
	}

	in = &bfom->bom_incoming.bi_incoming;
	c2_rm_incoming_init(in, owner, RIT_BORROW,
			    bfop->bo_flags, bfop->bo_flags);

	/*
	 * TODO - Copy right into ri_datum from fop
	 */

	c2_rm_right_get(in);
	/*
	 * If the request either succeeds or fails, follow with the next phase.
	 * If request is waiting, it will enter the next phase after wake-up.
	 */
	fom->fo_phase = FOPH_RM_BORROW_WAIT;
	if (in->rin_state == RI_WAIT) {
		c2_fom_block_at(fom, &in->rin_signal);
	}
	return FSO_WAIT;
}

static int post_borrow_state(struct c2_fom *fom)
{
	struct rm_borrow_fom  *bfom;
	struct c2_rm_incoming *in;
	int		       rc;

	C2_PRE(fom != NULL);

	bfom = container_of(fom, struct rm_borrow_fom, bom_fom);
	in = &bfom->bom_in.bi_incoming;

	C2_ASSERT(in->rin_state == RI_SUCCESS || in->rin_state == RI_FAILURE);

	rep->br_rc = in->rin_rc;
	if (in->rin_state == RI_SUCCESS) {
		C2_ASSERT(rep->br_rc == 0);

		c2_mutex_lock(&owner->ro_lock);
		rc = c2_rm_borrow_commit(bfom->bom_in);
		c2_mutex_unlock(&owner->ro_lock);

		if (rc == 0) {
			c2_rm_owner_cookie(in->rin_want.ri_owner,
					   &rep->br_owner.ow_cookie);
			c2_rm_loan_cookie(bfom->bom_incoming.bi_loan,
					  &rep->br_loan.ow_cookie);
			/* TODO - Copy datum */
		} else
			rep->br_rc = rc;
		c2_rm_right_put(in);
	} else {
		c2_free(bfom->bom_incoming.bi_loan);
	}

	return FSO_DONE;
}

static int revoke_state(struct c2_fom *fom)
{
	struct c2_fop_rm_canoke *cfop;
	struct c2_rm_incoming	*in;
	struct rm_canoke_fom	*cfom;
	struct c2_rm_owner	*owner;
	struct c2_rm_loan	*loan;
	struct c2_rm_right	 right;
	int			 rc;

	C2_PRE(fom != NULL);

	cfom = container_of(fom, struct rm_canoke_fom, bom_fom);
	cfop = c2_fop_data(fom.fo_fop);

	rc = c2_rm_loan_find(&cfop->cr_loan.lo_cookie, &loan);
	if (rc != 0) {
		/*
		 * This will happen only if the cookie is stale.
		 */
		rep->br_rc = rc;
		return FSO_DONE;
	}

	in = &cfom->ck_incoming.bi_incoming;
	owner = loan->rl_right.ri_owner;

	c2_rm_incoming_init(in, owner, RIT_REVOKE,
			    cfop->bo_flags, cfop->bo_flags);

	/*
	 * TODO - Copy right into ri_datum from fop
	 */

	c2_rm_right_get(in);
	/*
	 * If the request either succeeds or fails, follow with the next phase.
	 * If request is waiting, it will enter the next phase after wake-up.
	 */
	fom->fo_phase = FOPH_RM_RIGHT_REVOKE_WAIT;
	if (in->rin_state == RI_WAIT) {
		c2_fom_block_at(fom, &in->rin_signal);
	}
	return FSO_WAIT;
}

static int post_revoke_state(struct c2_fom *fom)
{
	struct rm_canoke_from	*cfom;
	struct c2_rm_incoming	*in;

	C2_PRE(fom != NULL);

	cfom = container_of(fom, struct rm_canoke_fom, ck_fom);
	in = &cfom->rv_incoming.bi_incoming;

	C2_ASSERT(in->rin_state == RI_SUCCESS || in->rin_state == RI_FAILURE);

	rep->br_rc = in->rin_rc;

	return FSO_DONE;
}

/**
 * This function handles the request to borrow a right to a resource on
 * a server ("creditor").
 *
 * @param fom -> fom processing the RIGHT_BORROW request on the server
 *
 */
int c2_rm_fom_canoke_state(struct c2_fom *fom)
{
	int rc;

	if (fom->fo_phase < FOPH_NR) {
		rc = c2_fom_state_generic(fom);
	} else {
		C2_PRE(fom->fo_phase == FOPH_RM_REVOKE ||
		       fom->fo_phase == FOPH_RM_REVOKE_WAIT)	

		if (fom->fo_phase == FOPH_RM_REVOKE)
			rc = revoke_state(fom);
		else
			rc = post_revoke_state(fom);

	}/* else - process RM phases */

	return rc;
}

/*
 * A revoke or cancel FOM constructor. In a client-server mode, only cancel
 * requests will arrive at server. In other complex cases, reovke will arrive
 * at server too.
 */
static int rm_canoke_fom_create(struct c2_fop *fop, struct c2_fom **out)
{
	struct rm_canoke_fom *cfom;
	struct c2_fop	     *rfop;

	C2_PRE(fop != NULL);
	C2_PRE(fop->f_type != NULL);
	C2_PRE(out != NULL);

	C2_ALLOC_PTR(cfom);
	if (cfom == NULL) {
		return -ENOMEM;
	}
	*out = &cfom->bom_fom;

	rfop = c2_fop_alloc(&c2_fop_rm_canoke_rep_fopt, NULL);
	if (rfop == NULL) {
		c2_free(cfom);
		return -ENOMEM;
	}
	c2_fom_init(fom, &fop->f_type->ft_fom_type, rm_fom_canoke_ops,
		    fop, rfop);
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
