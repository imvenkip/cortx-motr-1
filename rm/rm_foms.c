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

static inline void mark_borrow_fail(struct c2_fom *fom,
				struct c2_fop_rm_right_borrow_reply *reply_fop,
				int err)
{
	reply_fop->right_resp = err;
	fom->fo_rc = err;
	fom->fo_phase = FOPH_FAILURE;
}

static void mark_borrow_success(struct c2_fom *fom)
{
#if 0
	char	*right_addr;
	c2_bcount_t right_nr = 1;
#endif

	struct c2_fop_rm_right_borrow *req_fop;
	struct c2_fop_rm_right_borrow_reply *reply_fop;
	struct c2_rm_fom_right_request *rm_fom;
	struct c2_rm_incoming *rm_in;
	struct c2_rm_right *right;

	/*
	 * Get the FOM, FOP structures.
	 */
	rm_fom = container_of(fom, struct c2_rm_fom_right_request, frr_gen);

	req_fop = c2_fop_data(fom->fo_fop);
	reply_fop = c2_fop_data(fom->fo_rep_fop);
	rm_in = &rm_fom->frr_in;
	right = &rm_in->rin_want;

	/*
	 * @TODO: Need to work-out RM:generic interface.
	 * Set bufvec pointing to 'right' buffer inside reply FOP.
	 */
#if 0
	right_addr = &reply_fop->res_right;
	struct c2_bufvec right_buf = C2_BUFVEC_INIT_BUF(&right_addr, &right_nr);

	reply_fop->rem_id = rm_in->loan.rl_other.remid;
	reply_fop->res_type = req_fop->res_type;
	reply_fop->res_id = req_fop->res_id;
	reply_fop->loan_id = rm_in->loan.rl_other->rl_id;
	reply_fop->lease_time = 0;
	/*
	 * Encode right realted data into reply FOP
	 */
	rc = right->rro_encode(right, &right_buf);
	reply_fop->right_resp = rc;
#endif

	/*
	 * Fill in FOM fields.
	 */
	fom->fo_rc = 0;
	fom->fo_phase = FOPH_FINISH;

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
#if 0
			/*
			 * Find the resource type object
			 */
			c2_rm_res_type_get(req_fop->res_type);
			/*
			 * Find the resource object
			 */
			rtype_obj->rt_ops->rto_res_get(req_fop->res_id,
						       &resobj);
			/*
			 * Find the resource owner
			 */
			resobj->r_ops->rop_owner();
#endif

			rm_in = &rm_fom->frr_in;
			
			/* Prepare incoming request for RM generic layer */
			rm_in->rin_type = RIT_LOAN;
			rm_in->rin_state = RI_CHECK;
			rm_in->rin_flags = RIF_MAY_BORROW;
			c2_list_init(&rm_in->rin_pins);
			c2_chan_init(&rm_in->rin_signal);
			rm_in->rin_policy = req_fop->res_policy_id;
			rm_in->rin_priority = req_fop->res_priority;

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
					mark_borrow_success(fom);
				}
			}
			rc = FSO_AGAIN;

		} else if (fom->fo_phase == FOPH_RM_RIGHT_BORROW_WAIT) {
			/* TODO - Need to decide failure code */
			if (rm_in->rin_state == RI_FAILURE) {
				mark_borrow_fail(fom, reply_fop, rc);
			} else {
				mark_borrow_success(fom);
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
	fom->fo_phase = FOPH_FAILURE;
}

static inline void mark_revoke_success(struct c2_fom *fom,
				struct c2_fop_rm_right_revoke_reply *reply_fop)
{
	/* TODO - Fill in appropriate fields */
	reply_fop->right_resp = 0;
	fom->fo_rc = 0;
	fom->fo_phase = FOPH_FINISH;
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
#if 0
			rm_in->rin_owner
			= c2_rm_find_owner(req_fop->res_type, req_fop->res_id);
#endif


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
#if 0
		rm_in->rin_owner
		= c2_rm_find_owner(req_fop->res_type, req_fop->res_id);
#endif

		/**
		 * Cancel the right. Ignore the result.
		 * Reply is not communicated back.
		 */
		(void)c2_rm_right_get(rm_in->rin_owner, rm_in);

		fom->fo_phase = FOPH_FINISH;
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
