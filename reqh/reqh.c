#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/assert.h"
#include "stob/stob.h"
#include "fop/fop.h"

#include "reqh.h"

/**
   @addtogroup reqh
   @{
 */

int  c2_reqh_init(struct c2_reqh *reqh, 
		  struct c2_rpcmachine *rpc, struct c2_dtm *dtm,
		  struct c2_stob_domain *dom)
{
	reqh->rh_rpc = rpc;
	reqh->rh_dtm = dtm;
	reqh->rh_dom = dom;
	return 0;
}

void c2_reqh_fini(struct c2_reqh *reqh)
{
}

/**
   First do "standard actions":

   @li authenticity checks: reqh verifies that protected state in the fop is
   authentic. Various bits of information in C2 are protected by cryptographic
   signatures made by a node that issued this information: object identifiers
   (including container identifiers and fids), capabilities, locks, layout
   identifiers, other resources identifiers, etc. reqh verifies authenticity of
   such information by fetching corresponding node keys, re-computing the
   signature locally and checking it with one in the fop;

   @li resource limits: reqh estimates local resources (memory, cpu cycles,
   storage and network bandwidths) necessary for operation execution. The
   execution of operation is delayed if it would overload the server or exhaust
   resource quotas associated with operation source (client, group of clients,
   user, group of users, job, etc.);

   @li resource usage and conflict resolution: reqh determines what distributed
   resources will be consumed by the operation execution and call resource
   management infrastructure to request the resources and deal with resource
   usage conflicts (by calling DLM if necessary);

   @li object existence: reqh extracts identities of file system objects
   affected by the fop and requests appropriate stores to load object
   representations together with their basic attributes;

   @li authorization control: reqh extracts the identity of a user (or users) on
   whose behalf the operation is executed. reqh then uses enterprise user data
   base to map user identities into internal form. Resulting internal user
   identifiers are matched against protection and authorization information
   stored in the file system objects (loaded on the previous step);

   @li distributed transactions: for operations mutating file system state, reqh
   sets up local transaction context where the rest of the operation is
   executed.

   Once the standard actions are performed successfully, request handler creates
   a fom for this fop type and delegates the rest of operation execution to the
   fom.
 */
void c2_reqh_fop_handle(struct c2_reqh *reqh, struct c2_fop *fop)
{
#if 0
	struct c2_fop_iterator    it;
	struct c2_fom            *fom;
	struct c2_fop_field_value val;
	struct c2_res_set         sack;
	struct c2_res            *res;
	struct c2_obj_set         bag;
	struct c2_obj            *obj;
	struct c2_principal      *usr;

	c2_fop_iterator_init(&it, fop);

	/*
	 * Iterate over all protected state in the fop and verify signatures.
	 */
	c2_fop_for_each(&it, &val, PROTECTED_STATE) {
		if (!c2_sec_is_valid(&val)) {
			fop_error(-EPERM);
			return;
		}
	}

	/*
	 * Iterate over local resources that fop execution might need at wait
	 * until resources are available.
	 */
	c2_fop_for_each(&it, &val, LOCAL_RESOURCES) {
		res = c2_fop_field_as_res(&val);
		while (!c2_res_can_grab(res))
			c2_res_wait(res);
	}

	/*
	 * Iterate over global resources that fop execution might need...
	 */
	c2_fop_for_each(&it, &val, GLOBAL_RESOURCES) {
		res = c2_fop_field_as_res(&val);
		c2_res_portfolio_add(&sack, res);
	}

	/*
	 * ... sort required resource.
	 */
	c2_res_portfolio_sort(&sack);

	/*
	 * ... enqueue resources in the sorted order to avoid dead-locks.
	 */
	c2_res_portfolio_for_each(&sack, res) {
		if (c2_res_has_conflict(res))
			c2_res_enqueue(res);
	}

	/*
	 * ... and wait for resources.
	 */
	c2_res_portfolio_for_each(&sack, res) {
		while (!c2_res_available(res))
			c2_res_wait(res);
	}

	/*
	 * Iterate over all file system objects that the operation will
	 * affect...
	 */
	c2_fop_for_each(&it, &val, OBJECT) {
		obj = c2_fop_field_as_obj(&val);
		c2_obj_set_add(&bag, obj);
	}

	/*
	 * ... sort objects
	 */
	c2_obj_set_sort(&bag);

	/*
	 * ... load object attributes in the sorted order to optimize storage
	 * IO.
	 */
	c2_obj_set_load(&bag);

	/*
	 * ... check that objects exist or do not exist as required.
	 */
	c2_obj_set_for_each(&bag, obj) {
		bool needed;

		needed = c2_fop_obj_must_exist(fop, obj);
		if (c2_obj_exists(obj) != needed) {
			fop_error(needed ? -ENOENT : -EEXIST);
			return;
		}
	}

	/*
	 * Iterate over all user identities in the operation, map user
	 * identities to internal identifiers by calling userdb_map() and check
	 * that users are authorised to perform the operation requested.
	 */
	c2_fop_for_each(&it, &val, PRINCIPAL) {
		usr = c2_fop_field_as_principal(&val);
		c2_userdb_map(usr);
		if (!c2_sec_authorised(usr, fop)) {
			fop_error(-EPERM);
			return;
		}
	}

	/*
	 * Start a transaction until the operation is read-only.
	 */
	if (c2_fop_is_update(fop))
		reqh->rh_dtm->dtx_start(fop);

	c2_fop_iterator_fini(&it);

	/*
	 * Create a fom and delegate the rest of operation execution to it.
	 */
	fop->f_type->ft_ops->fto_fom_init(fop, &fom);
	fom->fm_ops->fmo_run(fom);
#endif
}

void c2_reqh_fop_sortkey_get(struct c2_reqh *reqh, struct c2_fop *fop,
			     struct c2_fop_sortkey *key)
{
}

/** @} endgroup reqh */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
