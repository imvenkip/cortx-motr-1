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

/**
   @addtogroup rm

   This file includes data structures and function used by RM:fop layer.

   @{
 */

/**
 * Forward declaration
 */
int c2_rm_fom_right_borrow_state(struct c2_fom *);
int c2_rm_fom_right_revoke_state(struct c2_fom *);
int c2_rm_fom_right_cancel_state(struct c2_fom *);

/**
 * FOM ops vector
 */

struct c2_fom_ops c2_rm_fom_borrow_ops = {
	.fo_fini = NULL,
	.fo_state = c2_rm_fom_right_borrow_state,
};

struct c2_fom_ops c2_rm_fom_revoke_ops = {
	.fo_fini = NULL,
	.fo_state = c2_rm_fom_right_revoke_state,
};

struct c2_fom_ops c2_rm_fom_cancel_ops = {
	.fo_fini = NULL,
	.fo_state = c2_rm_fom_right_cancel_state,
};

/**
 * FOM type ops vector. These are constructors of specific type FOM objects.
 * We will create the FOMs during FOM initialization routines.
 *
 * These constructors are not used currently. Instead the FOM object is
 * instantiated during fom_init routines.
 */
static const struct c2_fom_type_ops c2_rm_fom_borrow_type_ops = {
	.fto_create = NULL,
};

static const struct c2_fom_type_ops c2_rm_fom_revoke_type_ops = {
	.fto_create = NULL,
};

static const struct c2_fom_type_ops c2_rm_fom_cancel_type_ops = {
	.fto_create = NULL,
};

struct c2_fom_type c2_rm_fom_borrow_type = {
	.ft_ops = &c2_rm_fom_borrow_type_ops,
};

struct c2_fom_type c2_rm_fom_revoke_type = {
	.ft_ops = &c2_rm_fom_revoke_type_ops,
};

struct c2_fom_type c2_rm_fom_cancel_type = {
	.ft_ops = &c2_rm_fom_cancel_type_ops,
};

/*
 * Stub to be removed later.
 */
struct c2_rm_owner* c2_rm_find_owner(uint64_t res_type, uint64_t res_id)
{
	return NULL;
}

static inline void mark_borrow_fail(struct c2_fom *fom,
				struct c2_fop_rm_right_borrow_reply *reply_fop,
				int err)
{
	reply_fop->right_resp = err;
	fom->fo_rc = err;
	fom->fo_phase = FOPH_FAILED;
}

static inline void mark_borrow_success(struct c2_fom *fom,
				struct c2_fop_rm_right_borrow_reply *reply_fop)
{
	/* TODO - Fill in appropriate fields */
	reply_fop->right_resp = 0;
	fom->fo_rc = 0;
	fom->fo_phase = FOPH_DONE;
}

/**
 * This function handles the request to borrow a right to a resource on
 * a server ("creditor").
 *
 * @param fom -> fom processing the RIGHT_BORROW request on the server
 *
 * @retval  0 - on success
 *          non-zero - if there is failure.
 *
 */
int c2_rm_fom_right_borrow_state(struct c2_fom *fom)
{
	int rc = FSO_AGAIN;
	struct c2_fop_rm_right_borrow *req_fop;
	struct c2_fop_rm_right_borrow_reply *reply_fop;
	struct c2_rm_fom_right_request *rm_fom;
	struct c2_rm_incoming *rm_in;

	if (fom->fo_phase < FOPH_NR) {
		rc = c2_fom_state_generic(fom);
	} else {
		rm_fom
		= container_of(fom, struct c2_rm_fom_right_request, frr_gen);

		req_fop = c2_fop_data(fom->fo_fop);
		reply_fop = c2_fop_data(fom->fo_rep_fop);

		if (fom->fo_phase == FOPH_RM_RIGHT_BORROW) {
			rm_in = &rm_fom->frr_in;
			
			/* Prepare incoming request for RM generic layer */
			rm_in->rin_type = RIT_LOAN;
			rm_in->rin_state = RI_CHECK;
			rm_in->rin_flags = RIF_MAY_BORROW;
			c2_list_init(&rm_in->rin_pins);
			c2_chan_init(&rm_in->rin_signal);
			rm_in->rin_policy = req_fop->res_policy_id;
			rm_in->rin_priority = req_fop->res_priority;

			/* TODO - find/code this function */
			rm_in->rin_owner
			= c2_rm_find_owner(req_fop->res_type, req_fop->res_id);


			/* Attempt to borrow the right */
			rc = c2_rm_right_get(rm_in->rin_owner, rm_in);
			if (rc != 0) {
				if (rc != -EWOULDBLOCK) {
					mark_borrow_fail(fom, reply_fop, rc);
				} else {
					fom->fo_phase
					= FOPH_RM_RIGHT_BORROW_WAIT;
					c2_fom_block_at(fom, &rm_in->rin_signal);
				}
			} else {
				if (rm_in->rin_state == RI_WAIT) {
					fom->fo_phase
					= FOPH_RM_RIGHT_BORROW_WAIT;
					/* TODO - Prepare condition variable */
					c2_fom_block_at(fom, &rm_in->rin_signal);
				} else {
					mark_borrow_success(fom, reply_fop);
				}
			}
			rc = FSO_AGAIN;

		} else if (fom->fo_phase == FOPH_RM_RIGHT_BORROW_WAIT) {
			/* TODO - Need to decide failure code */
			if (rm_in->rin_state == RI_FAILURE) {
				mark_borrow_fail(fom, reply_fop, rc);
			} else {
				mark_borrow_success(fom, reply_fop);
			}
		}
		rc = FSO_AGAIN;

	}/* else - process FOM phase */

	return rc;
}

static inline void mark_revoke_fail(struct c2_fom *fom,
				struct c2_fop_rm_right_revoke_reply *reply_fop,
				int err)
{
	reply_fop->right_resp = err;
	fom->fo_rc = err;
	fom->fo_phase = FOPH_FAILED;
}

static inline void mark_revoke_success(struct c2_fom *fom,
				struct c2_fop_rm_right_revoke_reply *reply_fop)
{
	/* TODO - Fill in appropriate fields */
	reply_fop->right_resp = 0;
	fom->fo_rc = 0;
	fom->fo_phase = FOPH_DONE;
}

/**
 * This function handles the request to reovke a right to a resource on
 * a client ("debtor"). This request is sent by the creditor to the debtor.
 *
 * @param fom -> fom processing the RIGHT_BORROW request on the server
 *
 * @retval  0 - on success
 *          non-zero - if there is failure.
 *
 */
int c2_rm_fom_right_revoke_state(struct c2_fom *fom)
{
	int rc = FSO_AGAIN;
	struct c2_fop_rm_right_revoke *req_fop;
	struct c2_fop_rm_right_revoke_reply *reply_fop;
	struct c2_rm_fom_right_request *rm_fom;
	struct c2_rm_incoming *rm_in;

	if (fom->fo_phase < FOPH_NR) {
		rc = c2_fom_state_generic(fom);
	} else {
		rm_fom
		= container_of(fom, struct c2_rm_fom_right_request, frr_gen);

		req_fop = c2_fop_data(fom->fo_fop);
		reply_fop = c2_fop_data(fom->fo_rep_fop);

		if (fom->fo_phase == FOPH_RM_RIGHT_REVOKE) {
			rm_in = &rm_fom->frr_in;
			
			/* Prepare incoming request for RM generic layer */
			rm_in->rin_type = RIT_REVOKE;
			/* TODO - Check if this can be RI_CHECK */
			rm_in->rin_state = RI_CHECK;
			rm_in->rin_flags = RIF_MAY_REVOKE;
			rm_in->rin_policy = RIP_NONE;

			c2_list_init(&rm_in->rin_pins);
			c2_chan_init(&rm_in->rin_signal);
			rm_in->rin_priority = req_fop->res_priority;

			/* TODO - find/code this function */
			rm_in->rin_owner
			= c2_rm_find_owner(req_fop->res_type, req_fop->res_id);


			/* Attempt to revoke the right */
			rc = c2_rm_right_get(rm_in->rin_owner, rm_in);
			if (rc != 0) {
				if (rc != -EWOULDBLOCK) {
					mark_revoke_fail(fom, reply_fop, rc);
				} else {
					fom->fo_phase
					= FOPH_RM_RIGHT_REVOKE_WAIT;
					c2_fom_block_at(fom, &rm_in->rin_signal);
				}
			} else {
				if (rm_in->rin_state == RI_WAIT) {
					fom->fo_phase
					= FOPH_RM_RIGHT_REVOKE_WAIT;
					/* TODO - Prepare condition variable */
					c2_fom_block_at(fom, &rm_in->rin_signal);
				} else {
					mark_revoke_success(fom, reply_fop);
				}
			}
			rc = FSO_AGAIN;

		} else if (fom->fo_phase == FOPH_RM_RIGHT_REVOKE_WAIT) {
			/* TODO - Need to decide failure code */
			if (rm_in->rin_state == RI_FAILURE) {
				mark_revoke_fail(fom, reply_fop, rc);
			} else {
				mark_revoke_success(fom, reply_fop);
			}
		}
		rc = FSO_AGAIN;

	}/* else - process FOM phase */

	return rc;
}

/**
 * FOM processing function that processes a right cancel
 *
 * @param *fom - fom instance
 *
 * @retval 0 - on success
 *         non-zero - if there is a failure
 */
int c2_rm_fom_right_cancel_state(struct c2_fom *fom)
{
	int rc = FSO_AGAIN;
	struct c2_fop_rm_right_cancel *req_fop;
	struct c2_rm_fom_right_request *rm_fom;
	struct c2_rm_incoming *rm_in;

	if (fom->fo_phase < FOPH_NR) {
		rc = c2_fom_state_generic(fom);
	} else {
		rm_fom
		= container_of(fom, struct c2_rm_fom_right_request, frr_gen);

		req_fop = c2_fop_data(fom->fo_fop);

		C2_ASSERT(fom->fo_phase == FOPH_RM_RIGHT_CANCEL);

		rm_in = &rm_fom->frr_in;
		
		/* Prepare incoming request for RM generic layer */
		rm_in->rin_type = RIT_REVOKE;
		/* TODO - Check if this can be RI_CHECK */
		rm_in->rin_state = RI_CHECK;
		rm_in->rin_flags = RIF_MAY_REVOKE;
		rm_in->rin_policy = RIP_NONE;

		c2_list_init(&rm_in->rin_pins);
		c2_chan_init(&rm_in->rin_signal);
		rm_in->rin_priority = req_fop->res_priority;

		/* TODO - find/code this function */
		rm_in->rin_owner
		= c2_rm_find_owner(req_fop->res_type, req_fop->res_id);


		/**
		 * Cancel the right. Ignore the result.
		 * Reply is not communicated back.
		 */
		(void)c2_rm_right_get(rm_in->rin_owner, rm_in);

		fom->fo_phase = FOPH_DONE;
		rc = FSO_AGAIN;

	}/* else - process FOM phase */

	return rc;
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
