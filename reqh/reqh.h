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
 * Original author: Mandar Sawant <Mandar_Sawant@xyratex.com>
 * Original creation date: 05/04/2011
 */

#ifndef __COLIBRI_REQH_REQH_H__
#define __COLIBRI_REQH_REQH_H__

#include <sm/sm.h>
#include "fol/fol.h"

/* import */
struct c2_fop;
struct c2_fom;
struct c2_stob_domain;

/**
   @defgroup reqh Request handler

   @{
 */

struct c2_reqh;
struct c2_fop_sortkey;

/**
   Request handler instance.
 */
struct c2_reqh {
	struct c2_rpcmachine  	*rh_rpc;
	struct c2_dtm         	*rh_dtm;
	/**
	   @todo for now simply use storage object domain. In the future, this
	   will be replaced with "stores".
	 */
	struct c2_stob_domain	*rh_stdom;
	/** service this request hander belongs to */
	struct c2_service	*rh_serv;
	/** fol pointer for this request handler */
	struct c2_fol		*rh_fol;
	/** fom domain for this request handler */
	struct c2_fom_domain	*rh_fom_dom;
};

/**
 * Initialises request handler and fom domain.
 *
 * @param reqh -> c2_reqh, request handler.
 * @param rpc -> c2_rpc_machine, rpc machine (to required for future use).
 * @param dtm -> c2_dtm database transaction manager, (to be required future use).
 * @param stdom -> c2_stob_domain, storage object domain for fom io.
 * @param fol -> c2_fol, reqh fol.
 * @param serv -> c2_service, service to which reqh belongs.
 *
 * @retval int -> returns 0, if succeeds.
 *              returns -errno, on failure.
 * 
 * @pre reqh != NULL && stdom != NULL && fol != NULL && serv != NULL
 */
int  c2_reqh_init(struct c2_reqh *reqh,
		struct c2_rpcmachine *rpc, struct c2_dtm *dtm,
		struct c2_stob_domain *stdom,
		struct c2_fol *fol, struct c2_service *serv);

/**
 * Cleans request handler.
 *
 * @param reqh -> c2_reqh.
 *
 * @pre reqh != NULL
 * @pre reqh->rh_fom_dom != NULL
 */
void c2_reqh_fini(struct c2_reqh *reqh);

/**
   Sort-key determining global fop processing order.

   A sort-key is assigned to a fop when it enters NRS (Network Request
   Scheduler) incoming queue. NRS processes fops in sort-key order.

   @todo sort key definition.
 */
struct c2_fop_sortkey {
};

/**
   Submit fop for request handler processing.

   fop processing results are reported by other means (ADDB, reply fops, error
   messages, etc.) so this function returns nothing.

   @param reqh -> c2_reqh.
   @param fom -> c2_fom.
   @param cookie -> void reference provided by client.

   @pre reqh != null.
   @pre fom != null.
 */
void c2_reqh_fop_handle(struct c2_reqh *reqh, struct c2_fop *fop, void *cookie);

/**
   Assign a sort-key to a fop.

   This function is called by NRS to order fops in its incoming queue.

   @todo -> to decide upon sort key generation parameters as required by
   nrs scheduler.
 */
void c2_reqh_fop_sortkey_get(struct c2_reqh *reqh, struct c2_fop *fop,
			     struct c2_fop_sortkey *key);


/**
   Standard fom state transition function.

   This function handles standard fom phases from enum c2_fom_phase.

   First do "standard actions":

   - authenticity checks: reqh verifies that protected state in the fop is
     authentic. Various bits of information in C2 are protected by cryptographic
     signatures made by a node that issued this information: object identifiers
     (including container identifiers and fids), capabilities, locks, layout
     identifiers, other resources identifiers, etc. reqh verifies authenticity
     of such information by fetching corresponding node keys, re-computing the
     signature locally and checking it with one in the fop;

   - resource limits: reqh estimates local resources (memory, cpu cycles,
     storage and network bandwidths) necessary for operation execution. The
     execution of operation is delayed if it would overload the server or
     exhaust resource quotas associated with operation source (client, group of
     clients, user, group of users, job, etc.);

   - resource usage and conflict resolution: reqh determines what distributed
     resources will be consumed by the operation execution and call resource
     management infrastructure to request the resources and deal with resource
     usage conflicts (by calling DLM if necessary);

   - object existence: reqh extracts identities of file system objects affected
     by the fop and requests appropriate stores to load object representations
     together with their basic attributes;

   - authorization control: reqh extracts the identity of a user (or users) on
     whose behalf the operation is executed. reqh then uses enterprise user data
     base to map user identities into internal form. Resulting internal user
     identifiers are matched against protection and authorization information
     stored in the file system objects (loaded on the previous step);

   - distributed transactions: for operations mutating file system state, reqh
     sets up local transaction context where the rest of the operation is
     executed.

   Once the standard actions are performed successfully, request handler
   delegates the rest of operation execution to the fom type specific state
   transition function.

   @param fom -> c2_fom object.

   @retval int -> returns FSO_AGAIN, if succeeds.
	returns FSO_WAIT, if operation blocks or fom execution ends.

 */
int c2_fom_state_generic(struct c2_fom *fom);

/** @} endgroup reqh */

/* __COLIBRI_REQH_REQH_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
