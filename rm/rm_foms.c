/* -*- C -*- */
#ifndef __COLIBRI_RM_FOM_H__
#define __COLIBRI_RM_FOM_H__

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

#include "fop/fop.h"
#include "fop/fop_format.h"
#include "rm_fop.h"

#ifndef __KERNEL__
/**
   @addtogroup rm

   This file includes data structures and function used by RM:fop layer.

   @{

/**
 * FOM ops vector
 */

static struct c2_fom_ops c2_rm_fom_borrow_ops = {
	.fo_fini = NULL,
	.fo_state = c2_rm_fom_borrow_state,
};

static struct c2_fom_ops c2_rm_fom_borrow_reply_ops = {
	.fo_fini = NULL,
	.fo_state = c2_rm_fom_borrow_reply_state,
};

static struct c2_fom_ops c2_rm_fom_revoke_ops = {
	.fo_fini = NULL,
	.fo_state = c2_rm_fom_revoke_state,
};

static struct c2_fom_ops c2_rm_fom_revoke_reply_ops = {
	.fo_fini = NULL,
	.fo_state = c2_rm_fom_revoke_reply_state,
};

static struct c2_fom_ops c2_rm_fom_cancel_ops = {
	.fo_fini = NULL,
	.fo_state = c2_rm_fom_cancel_state,
};

/**
 * FOM type ops vector.
 */
static struct c2_fom_type_ops c2_rm_fom_borrow_ops = {
	.fto_create = NULL,
};

#endif /* __KERNEL__ */
 */
enum c2_rm_fom_phases {
	FOPH_RM_RIGHT_BORROW = FOPH_NR + 1,
	FOPH_RM_RIGHT_REVOKE = FOPH_NR + 1,
	FOPH_RM_RIGHT_BORROW_REPLY = FOPH_NR + 1,
	FOPH_RM_RIGHT_REVOKE_REPLY = FOPH_NR + 1,
	FOPH_RM_RIGHT_CANCEL = FOPH_NR + 1,
	FOPH_RM_RIGHT_BORROW_WAIT = FOPH_NR + 2,
	FOPH_RM_RIGHT_REVOKE_WAIT = FOPH_NR + 2
};

/**
  * FOM to execute resource right borrow request.
  */
struct c2_rm_fom_right_borrow {
	/** Generic c2_fom object */
	struct c2_fom	frb_gen;
	/** Generic c2_fom object */
	struct c2_fop	*frb_fop;
	/** Generic c2_fom object */
	struct c2_fop	*frb_reply_fop;
};

/**
  * FOM to execute resource revoke borrow request.
  */
struct c2_rm_fom_right_revoke {
	/** Generic c2_fom object */
	struct c2_fom	frr_gen;
	/** Generic c2_fom object */
	struct c2_fop	*frr_fop;
	/** Generic c2_fom object */
	struct c2_fop	*frr_reply_fop;
};

/**
   This function handles the request to borrow a right to a resource on
   a server ("creditor").

   Pseudo code:
   if (fom_state == FOPH_RM_RIGHT_BORROW) {
   1. in = c2_alloc(c2_rm_incoming); Allocate c2_rm_incoming.
      1.a. in->rin_type = RIT_REVOKE;
      1.b. in->rin_state = RI_CHECK;
      1.c. in->rin_policy = RIT_NONE;
      1.d. in->rin_flags = RIF_MAY_REVOKE;
      1.f. in->rin_priority = incoming_fop->priority;
      1.g. c2_list_init(&in->rin_pins);
      1.f. in->rin_owner = get_owner(incoming_fop->res_type,
                                     icoming_fop->res_id);;

   2. rc = c2_rm_right_get(in->rin_owner, in);
      Now there are few cases
      Case 1: Resource is under use.
      Case 2 : There is failure.
      if (rc != 0) {
	if (rc != -EWOULDBLOCK) {
		send_error_fop;
	}
	set FOM state to FOPH_RM_RIGHT_BORROW_WAIT
	Suspend FOP and wait.
      } else {
	if (in->rin_state == RI_WAIT) {
		set FOM state to FOPH_RM_RIGHT_BORROW_WAIT
		Suspend FOP.
	} else {
		Mark FOM completion
	}
      }
     }
     if (fom_state == FOPH_RM_RIGHT_BORROW_WAIT) {
	if (in->rin_state == RI_FAILURE) {
		send_error_fop;
	}
	Mark FOM completion
     }

   @param fom -> fom processing the RIGHT_BORROW request on the server

   @retval 

 */
int c2_rm_fom_right_borrow_state(struct c2_fom *fom);
int c2_rm_fom_right_borrow_reply_state(struct c2_fom *fom);
/**
   This function handles the request to reovke a right to a resource on
   a client ("debtor").

   Pseudo code:
   if (fom_state == FOPH_RM_RIGHT_REVOKE) {
   1. in = c2_alloc(c2_rm_incoming); Allocate c2_rm_incoming.
      1.a. in->rin_type = RIT_LOAN;
      1.b. in->rin_state = RI_CHECK;
      1.c. in->rin_policy = incoming_fop->policy;
      1.d. in->rin_flags = RIF_MAY_BORROW;
      1.e. in->rin_want = incoming_fop->right_params;
      1.f. in->rin_priority = incoming_fop->priority;
      1.g. c2_list_init(&in->rin_pins);
      1.f. in->rin_owner = get_owner(incoming_fop->res_type,
                                     icoming_fop->res_id);;

   2. rc = c2_rm_right_revoke(in->rin_owner, in);
      Now there are few cases
      Case 1: Right is granted and sent via c2_rm_send_fop() (go_out ())
      Case 2: Revoke request is sent to another client and it's in wait state
      Case 3 : There is failure
      if (rc != 0) {
	if (rc != -EWOULDBLOCK) {
		send_error_fop;
	}
	set FOM state to FOPH_RM_RIGHT_REVOKE_WAIT
	Suspend FOP and wait.
      } else {
	if (in->rin_state == RI_WAIT) {
		set FOM state to FOPH_RM_RIGHT_REVOKE_WAIT
		Suspend FOP.
	} else {
		Mark FOM completion
	}
      }
     }
     if (fom_state == FOPH_RM_RIGHT_REVOKE_WAIT) {
	if (in->rin_state == RI_FAILURE) {
		send_error_fop;
	}
	Mark FOM completion
     }

   @param fom -> fom processing the RIGHT_BORROW request on the server

   @retval 

 */
int c2_rm_fom_right_revoke_state(struct c2_fom *fom);
int c2_rm_fom_right_cancel_state(struct c2_fom *fom);
int c2_rm_send_fop()

/* __COLIBRI_RM_FOM_H__ */
#endif

/**
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

