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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation date: 8-Mar-2013
 */
#pragma once
#ifndef __MERO_REQH_REQH_FP_H__
#define __MERO_REQH_REQH_FP_H__

#include "sm/sm.h"

/* externs */
struct m0_fop;
struct m0_reqh;
struct m0_reqh_service;

/**
   @defgroup reqh_fp Request handler FOP acceptance policy
   @ingroup reqh

   This module defines the policy used to determine how FOPs are
   accepted through the m0_reqh_fop_handle() subroutine.

   @see @ref MGMT-SVC-DLD-lspec-fap "FOP Acceptance Policy Design"

   @{
 */

/**
   States of the FOP acceptance policy state machine.
 */
enum m0_reqh_fop_policy_states {
	M0_REQH_FP_INIT = 0,
	M0_REQH_FP_MGMT_START,
	M0_REQH_FP_SVCS_START,
	M0_REQH_FP_NORMAL,
	M0_REQH_FP_DRAIN,
	M0_REQH_FP_SVCS_STOP,
	M0_REQH_FP_MGMT_STOP,
	M0_REQH_FP_STOPPED,

	M0_REQH_FP_NR
};

/**
   FOP acceptance policy object.
 */
struct m0_reqh_fop_policy {
	uint64_t                rfp_magic;
	/** State machine. */
	struct m0_sm            rfp_sm;
	/** Pointer to the management service */
	struct m0_reqh_service *rfp_mgmt_svc;
};

/**
   Initialize a FOP acceptance policy object.
   @post fp->rfp_mgmt_svc == NULL
 */
M0_INTERNAL int m0_reqh_fp_init(struct m0_reqh_fop_policy *fp);

/**
   Check if a FOP acceptance policy object is initialized.
 */
M0_INTERNAL bool m0_reqh_fp_is_initalized(const struct m0_reqh_fop_policy *fp);

/**
   Finalize a FOP acceptance policy object.
   @pre m0_reqh_fp_is_initialized(fp)
   @post !m0_reqh_fp_is_initialized(fp)
 */
M0_INTERNAL void m0_reqh_fp_fini(struct m0_reqh_fop_policy *fp);

/**
   Get the current state of the FOP acceptance policy object state machine.
   @pre m0_reqh_fp_is_initialized(fp)
 */
M0_INTERNAL int m0_reqh_fp_state_get(struct m0_reqh_fop_policy *fp);

/**
   Set the state of the FOP acceptance policy object state machine.
   @pre m0_reqh_fp_is_initialized(fp)
   @pre ergo(state > M0_REQH_FP_MGMT_START, fp->rfp_mgmt_svc != NULL)
 */
M0_INTERNAL void m0_reqh_fp_state_set(struct m0_reqh_fop_policy *fp,
				      enum m0_reqh_fop_policy_states state);

/**
   Identify the management service to the FOP acceptance policy.
   @pre m0_reqh_fp_is_initialized(fp)
   @pre fp->rfp_mgmt_svc == NULL
   @pre m0_reqh_fp_state_get(fp) == M0_REQH_FP_MGMT_START
   @post fp->rfp_mgmt_svc != NULL
 */
M0_INTERNAL void m0_reqh_fp_mgmt_service_set(struct m0_reqh_fop_policy *fp,
					     struct m0_reqh_service *mgmt_svc);

/**
   Decide if an incoming FOP should be accepted by the request handler or
   not.
   This subroutine is expected to be invoked from m0_reqh_fop_handle().
   @pre m0_reqh_fp_is_initialized(fp)
 */
M0_INTERNAL int m0_reqh_fp_accept(struct m0_reqh_fop_policy *fp,
				  struct m0_reqh *reqh,
				  struct m0_fop *fop);

/** @} end reqh_fp group */
#endif /* __MERO_REQH_REQH_FP_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
