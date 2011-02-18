/* -*- C -*- */

#ifndef __COLIBRI_REQH_REQH_H__
#define __COLIBRI_REQH_REQH_H__

#include <sm/sm.h>

/* import */
struct c2_fop;
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
	struct c2_rpcmachine  *rh_rpc;
	struct c2_dtm         *rh_dtm;
	/**
	   @todo for now simply use storage object domain. In the future, this
	   will be replaced with "stores".
	 */
	struct c2_stob_domain *rh_dom;
};

int  c2_reqh_init(struct c2_reqh *reqh,
		  struct c2_rpcmachine *rpc, struct c2_dtm *dtm,
		  struct c2_stob_domain *rh_stob_dom);
void c2_reqh_fini(struct c2_reqh *reqh);

/**
   Sort-key determining global fop processing order.

   A sort-key is assigned to a fop when it enters NRS (Network Request
   Scheduler) incoming queue. NRS processes fops in sort-key order.
 */
struct c2_fop_sortkey {
};

/**
   Submit fop for request handler processing.

   fop processing results are reported by other means (ADDB, reply fops, error
   messages, etc.) so this function returns nothing.
 */
void c2_reqh_fop_handle(struct c2_reqh *reqh, struct c2_fop *fop);

/**
   Assign a sort-key to a fop.

   This function is called by NRS to order fops in its incoming queue.
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
