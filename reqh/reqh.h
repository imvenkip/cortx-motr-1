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
