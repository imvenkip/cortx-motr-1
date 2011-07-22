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

#include "fop/fop.h"
#include "fop/fop_format.h"
#include "rm_fop.h"

/**
   @addtogroup rm

   This file includes data structures and function used by RM:fop layer.

   @{
 */

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
 * FOM type ops vector. These are constructors of specific type FOM objects.
 * We will create the FOMs during FOM initialization routines.
 *
 * These constructors are not used currently. Instead the FOM object is
 * instantiated during fom_init routines.
 */
static struct c2_fom_type_ops c2_rm_fom_borrow_ops = {
	.fto_create = NULL,
};

static struct c2_fom_type_ops c2_rm_fom_borrow_reply_ops = {
	.fto_create = NULL,
};

static struct c2_fom_type_ops c2_rm_fom_revoke_ops = {
	.fto_create = NULL,
};

static struct c2_fom_type_ops c2_rm_fom_revoke_reply_ops = {
	.fto_create = NULL,
};

static struct c2_fom_type_ops c2_rm_fom_cancel_ops = {
	.fto_create = NULL,
};

/**
   This function handles the request to borrow a right to a resource on
   a server ("creditor").

   Pseudo code:
   if (fom_state == FOPH_RM_RIGHT_BORROW) {
   1. in = container_of(fom, struct c2_rm_incoming)
      1.a. in->rin_type = RIT_REVOKE;
      1.b. in->rin_state = RI_CHECK;
      1.c. in->rin_policy = RIT_NONE;
      1.d. in->rin_flags = RIF_MAY_REVOKE;
      1.f. in->rin_priority = incoming_fop->priority;
      1.g. c2_list_init(&in->rin_pins);
      1.f. in->rin_owner = get_owner(incoming_fop->res_type,
                                     icoming_fop->res_id);

   2. rc = c2_rm_right_get(in->rin_owner, in);
      Now there are few cases
      Case 1: Resource is under use.
      Case 2 : There is failure.
      if (rc != 0) {
	if (rc != -EWOULDBLOCK) {
		set_error_fop(fom);
		Mark FOM failure.
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
		set_error_fop(fom);
		Mark FOM failure.
	}
	Mark FOM completion
     }

   @param fom -> fom processing the RIGHT_BORROW request on the server

   @retval  0 - on success
            non-zero - if there is failure.

 */
int c2_rm_fom_right_borrow_state(struct c2_fom *fom);

/**
 * FOM processing function that processes a right borrow reply.
 *
 * @param *fom - fom instance
 *
 * @retval 0 - on success
 *         non-zero - if there is a failure
 */
int c2_rm_fom_right_borrow_reply_state(struct c2_fom *fom);

/**
   This function handles the request to reovke a right to a resource on
   a client ("debtor"). This request is sent by the creditor to the debtor.

   Pseudo code:
   if (fom_state == FOPH_RM_RIGHT_REVOKE) {
   1. in = container_of(fom)
      1.a. in->rin_type = RIT_REVOKE;
      1.b. in->rin_state = RI_CHECK;
      1.c. in->rin_policy = RIP_NONE;
      1.d. in->rin_flags = RIF_MAY_REVOKE;
      1.e. in->rin_want = 0;
      1.g. c2_list_init(&in->rin_pins);
      1.f. in->rin_owner = get_owner(incoming_fop->res_type,
                                     icoming_fop->res_id);

   2. rc = c2_rm_right_get(in->rin_owner, in);
      Now there are few cases
      Case 1: Right is granted
      Case 2: Revoke request is sent to another client and it's in wait state
      Case 3 : There is failure
      if (rc != 0) {
	if (rc != -EWOULDBLOCK) {
		set_error_fop(fom);
		Mark FOM failure.
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
		set_error_fop(fom);
		Mark FOM failure.
	}
	Mark FOM completion
     }

   @param fom -> fom processing the RIGHT_BORROW request on the server

   @retval  0 - on success
            non-zero - if there is failure.

 */
int c2_rm_fom_right_revoke_state(struct c2_fom *fom);

/**
 * FOM processing function that processes a right revoke reply.
 *
 * @param *fom - fom instance
 *
 * @retval 0 - on success
 *         non-zero - if there is a failure
 */
int c2_rm_fom_right_revoke_state(struct c2_fom *fom);

/**
 * FOM processing function that processes a right cancel
 *
   This function handles the request to reovke a right to a resource on
   a server ("creditor"). This request is sent by the debtor to the creditor.
   The debtor gives up the right to a resource.

   Pseudo code:
   if (fom_state == FOPH_RM_RIGHT_CANCEL) {
   1. in = container_of(fom)
      1.a. in->rin_type = RIT_REVOKE;
      1.b. in->rin_state = RI_CHECK;
      1.c. in->rin_policy = RIP_NONE;
      1.d. in->rin_flags = RIF_MAY_REVOKE;
      1.e. in->rin_want = 0;
      1.f. in->rin_priority = incoming_fop->priority;
      1.g. c2_list_init(&in->rin_pins);
      1.f. in->rin_owner = get_owner(incoming_fop->res_type,
                                     icoming_fop->res_id);

   2. rc = c2_rm_right_get(in->rin_owner, in);
      Mark FOM completion

 * @param *fom - fom instance
 *
 * @retval 0 - on success
 *         non-zero - if there is a failure
 */
int c2_rm_fom_right_cancel_state(struct c2_fom *fom);

/**
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
