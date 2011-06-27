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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "stob/stob.h"
#include "net/net.h"
#include "fop/fop.h"
#include "fop/fom.h"
#include "fop/fop_iterator.h"
#include "dtm/dtm.h"
#include "fop/fop_format_def.h"

#ifdef __KERNEL__
# include "reqh_fops_k.h"
#else

#include "reqh_fops_u.h"
#endif

#include "reqh.h"

/**
   @addtogroup reqh
   @{
 */

/**
 * Global c2_addb_loc object for logging addb context.
 */
const struct c2_addb_loc c2_reqh_addb_loc = {
	.al_name = "reqh"
};

/**
 * Global addb context type for logging addb context.
 */
const struct c2_addb_ctx_type c2_reqh_addb_ctx_type = {
	.act_name = "t1-reqh"
};

/**
 * Global addb context for addb logging.
 */
struct c2_addb_ctx c2_reqh_addb_ctx;

#define REQH_ADDB_ADD(addb_ctx, name, rc)  \
C2_ADDB_ADD(&addb_ctx, &c2_reqh_addb_loc, c2_addb_func_fail, (name), (rc))

/**
 * fop type object for a c2_reqh_error_rep fop.
 */
extern struct c2_fop_type c2_reqh_error_rep_fopt;

/**
 * function to initialize and build reqh fops.
 */
extern int reqh_fop_init(void);

/**
 * clean up function for reah fops.
 */
extern void reqh_fop_fini(void);

/**
 * fom phase table to hold function pointers, which execute
 * various fom phases.
 */
static struct c2_fom_phase_table fp_table[FOPH_NR];

/**
 * function to initialize fom phase table.
 */
void set_fom_phase_table(void);

/**
 * function to put fom on wait list.
 */
void c2_fom_wait(struct c2_fom *fom, struct c2_chan *chan);

/**
 * function to verify fom and fom members.
 */
extern bool c2_fom_invariant(const struct c2_fom *fom);

/**
 * function to verify locality.
 */
extern bool c2_locality_invariant(struct c2_fom_locality *loc);

/*
 * macro definition to set a fom phase in fom phase table.
 */
#define INIT_PHASE(curr_phase, act, np) \
	fp_table[curr_phase].action = act; \
	fp_table[curr_phase].next_phase = np; \

/**
 * Function to initialize request handler and fom domain
 * @param reqh -> c2_reqh structure pointer.
 * @param rpc -> c2_rpc_machine structure pointer.
 * @param dtm -> c2_dtm structure pointer.
 * @param dom -> c2_stob_domain strutcure pointer.
 * @param fol -> c2_fol structure pointer.
 * @param serv -> c2_service structure pointer.
 * @retval int -> returns 0, on success.
 *		returns -1, on failure.
 */
int  c2_reqh_init(struct c2_reqh *reqh,
		struct c2_rpcmachine *rpc, struct c2_dtm *dtm,
		struct c2_stob_domain *dom, struct c2_fol *fol,
		struct c2_service *serv)
{
	int result;

	if (reqh == NULL || dom == NULL ||
		fol == NULL || serv == NULL)
		return -EINVAL;

	/* initialize fom phase table with standard/generic fom phases,
	 * and their correspnding methods.
	 */
	set_fom_phase_table();

	/* allocate memory for c2_fom_domain member in reqh and initialize the same. */
	reqh->rh_fom_dom = c2_alloc(sizeof *reqh->rh_fom_dom);
	if (reqh->rh_fom_dom == NULL) {
		return -ENOMEM;
	}

	/* initialize global reqh addb context */
	c2_addb_ctx_init(&c2_reqh_addb_ctx, &c2_reqh_addb_ctx_type,
					&c2_addb_global_ctx);

	/* initialize reqh fops */
	reqh_fop_init();

	result = c2_fom_domain_init(reqh->rh_fom_dom);
	if (result) {
		REQH_ADDB_ADD(c2_reqh_addb_ctx,
				"c2_reqh_init: fom domain init failed",
				result);
		return result;
	}

	/* initialize reqh members */
	reqh->rh_rpc = rpc;
	reqh->rh_dtm = dtm;
	reqh->rh_dom = dom;
	reqh->rh_fol = fol;
	reqh->rh_serv = serv;

	return result;
}

/**
 * Request handler clean up function.
 * @param reqh -> c2_reqh structure pointer.
 * @pre assumes reqh not null.
 * @pre assumes fom domain in reqh is allocated and initialized.
 */
void c2_reqh_fini(struct c2_reqh *reqh)
{
	C2_PRE(reqh != NULL);
	C2_PRE(reqh->rh_fom_dom);
	c2_fom_domain_fini(reqh->rh_fom_dom);
	c2_addb_ctx_fini(&c2_reqh_addb_ctx);
	reqh_fop_fini();
}

/**
 * send generic error reply fop
 * @param service -> struct c2_service pointer
 * @param cookie -> void pointer containing some address.
 * @param rc -> int, error code to be sent in reply fop.
 */
void c2_reqh_send_err_rep(struct c2_service *service, void *cookie, int rc)
{
	struct c2_fop *rfop = NULL;
	struct c2_reqh_error_rep *out_fop = NULL;

	rfop = c2_fop_alloc(&c2_reqh_error_rep_fopt, NULL);
	if (rfop == NULL)
		return;
	out_fop = c2_fop_data(rfop);
	out_fop->sierr_rc = rc;
	/* Will be using c2_rpc_post in future */
	c2_net_reply_post(service, rfop, cookie);
}

/**
 * Function to accept fop and create corresponding fom
 * and submit it for further processing.
 * @param reqh -> c2_reqh structure pointer.
 * @param fom -> c2_fom sturcture pointer.
 * @param cookie -> void pointer to hold some pointer address.
 * @pre assumes reqh is not null.
 * @pre assumes fom is not null.
 */
void c2_reqh_fop_handle(struct c2_reqh *reqh, struct c2_fop *fop, void *cookie)
{
	struct c2_fom *fom = NULL;
	int			result;
	size_t			iloc;

	C2_PRE(reqh != NULL);
	C2_PRE(fop != NULL);

	/* Initialize fom for fop processing */
	result = fop->f_type->ft_ops->fto_fom_init(fop, &fom);
	if (result) {
		if (result != -ENOMEM) {
			/* fop could be corrupted or invalid,
			 * send an error reply.
			 */
			c2_reqh_send_err_rep(reqh->rh_serv, cookie, result);
		}
		REQH_ADDB_ADD(c2_reqh_addb_ctx,
				"c2_reqh_fop_handle: fom init failed",
				result);
		return;
	}

	fom->fo_fop_ctx->fc_cookie = cookie;
	fom->fo_fop_ctx->ft_service = reqh->rh_serv;
	fom->fo_fol = reqh->rh_fol;
	fom->fo_domain = reqh->rh_dom;

	/* locate fom's home locality */
	iloc = fom->fo_ops->fo_home_locality(fom, reqh->rh_fom_dom->fd_nr);
	if (iloc >= 0) {
		fom->fo_loc = &reqh->rh_fom_dom->fd_localities[iloc];
		C2_ASSERT(c2_locality_invariant(fom->fo_loc));
	}

	/* submit fom for further processing */
	c2_fom_queue(fom);
}

/**
 * Assign a sort-key to a fop.
 * @param reqh -> c2_reqh struct pointer
 * @param fop -> c2_fop struct pointer
 * @param key -> c2_fop_sortkey struct pointer.
 * @todo -> function is called by NRS to order fops in its incoming queue.
 */
void c2_reqh_fop_sortkey_get(struct c2_reqh *reqh, struct c2_fop *fop,
				struct c2_fop_sortkey *key)
{
}

/**
 * Funtion to handle init phase of fom.
 * @param fom -> c2_fom structure pointer.
 * @retval int -> returns 0, on success.
 *		returns -1, on failure.
 * @pre assumes fom is valid, c2_fom_invariant should return true.
 */
int c2_fom_phase_init(struct c2_fom *fom)
{
	C2_PRE(c2_fom_invariant(fom));

	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Function to authenticate fop.
 * @param fom -> c2_fom structure pointer.
 * @retval int -> returns 0, on success.
 *		returns -1, on failure.
 * @pre assumes fom is valid, c2_fom_invariant should return true.
 * @todo needs further more implementation, once depending
 *		routines are ready.
 */
int c2_fom_authen(struct c2_fom *fom)
{
	C2_PRE(c2_fom_invariant(fom));

	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Function invoked after a fom resumes execution
 * post wait, in authentication phase.
 * @param fom -> c2_fom structure pointer.
 * @retval int -> returns 0, on success.
 *		returns -1, on failure.
 * @pre assumes fom is valid, c2_fom_invariant should return true.
 * @todo needs further more implementation, once depending
 *		routines are ready.
 */
int c2_fom_authen_wait(struct c2_fom *fom)
{
	C2_PRE(c2_fom_invariant(fom));

	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Function to identify local resources
 * required for fop execution.
 * @param fom -> c2_fom structure pointer.
 * @retval int -> returns 0, on success.
 *		returns -1, on failure.
 * @pre assumes fom is valid, c2_fom_invariant should return true.
 * @todo needs further more implementation, once depending
 *		routines are ready.
 */
int c2_fom_loc_resource(struct c2_fom *fom)
{
	C2_PRE(c2_fom_invariant(fom));

	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Funtion invoked after a fom resumes execution
 * post wait, while checking for local resources.
 * @param fom -> c2_fom structure pointer.
 * @retval int -> returns 0, on success.
 *		returns -1, on failure.
 * @pre assumes fom is valid, c2_fom_invariant should return true.
 * @todo needs further more implementation, once depending
 *		routines are ready.
 */
int c2_fom_loc_resource_wait(struct c2_fom *fom)
{
	C2_PRE(c2_fom_invariant(fom));

	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Function to identify distributed resources
 * required for fop execution.
 * @param fom -> c2_fom structure pointer.
 * @retval int -> returns 0, on success.
 *		returns -1, on failure.
 * @pre assumes fom is valid, c2_fom_invariant should return true.
 * @todo needs further more implementation, once depending
 *		routines are ready.
 */
int c2_fom_dist_resource(struct c2_fom *fom)
{
	C2_PRE(c2_fom_invariant(fom));

	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Funtion invoked after a fom resumes execution
 * post wait, while checking for distributed resources.
 * @param fom -> c2_fom structure pointer.
 * @retval int -> returns 0, on success.
 *		returns -1, on failure.
 * @pre assumes fom is valid, c2_fom_invariant should return true.
 * @todo needs further more implementation, once depending
 *		routines are ready.
 */
int c2_fom_dist_resource_wait(struct c2_fom *fom)
{
	C2_PRE(c2_fom_invariant(fom));

	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Function to locate and load file system objects,
 * required for fop execution.
 * @param fom -> c2_fom structure pointer.
 * @retval int -> returns 0, on success.
 *		returns -1, on failure.
 * @pre assumes fom is valid, c2_fom_invariant should return true.
 * @todo needs further more implementation, once depending
 *		routines are ready.
 */
int c2_fom_obj_check(struct c2_fom *fom)
{
	C2_PRE(c2_fom_invariant(fom));

	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Funtion invoked after a fom resumes execution
 * post wait, while locating file system objects.
 * @param fom -> c2_fom structure pointer.
 * @retval int -> returns 0, on success.
 *		returns -1, on failure.
 * @pre assumes fom is valid, c2_fom_invariant should return true.
 * @todo needs further more implementation, once depending
 *		routines are ready.
 */
int c2_fom_obj_check_wait(struct c2_fom *fom)
{
	C2_PRE(c2_fom_invariant(fom));

	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Function to authorise fop in fom
 * @param fom -> c2_fom structure pointer.
 * @retval int -> returns 0, on success.
 *		returns -1, on failure.
 * @pre assumes fom is valid, c2_fom_invariant should return true.
 * @todo needs further more implementation, once depending
 *		routines are ready.
 */
int c2_fom_auth(struct c2_fom *fom)
{
	C2_PRE(c2_fom_invariant(fom));

	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Funtion invoked after a fom resumes execution
 * post wait in fop authorisation phase.
 * @param fom -> c2_fom structure pointer.
 * @retval int -> returns 0, on success.
 *		  returns -1, on failure.
 * @pre assumes fom is valid, c2_fom_invariant should return true.
 * @todo needs further more implementation, once depending
 *		routines are ready.
 */
int c2_fom_auth_wait(struct c2_fom *fom)
{
	C2_PRE(c2_fom_invariant(fom));

	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Funtion to create local transactional context, i.e we initialize a
 * struct c2_dtx object, which would be used for db transaction throughout
 * fop execution.
 * @param fom -> c2_fom structure pointer.
 * @retval int -> returns 0, on success.
 *		  returns -1, on failure.
 * @pre assumes fom is valid, c2_fom_invariant should return true.
 */
int c2_create_loc_ctx(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(c2_fom_invariant(fom));

	rc = fom->fo_domain->sd_ops->sdo_tx_make(fom->fo_domain,
							&fom->fo_tx);
	if (rc)
		fom->fo_phase = FOPH_FAILED;
	else
		fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return rc;
}

/**
 * Funtion to invoke after a fom resumes execution
 * post wait while creating local transactional context.
 * @param fom -> c2_fom structure pointer.
 * @retval int -> returns 0, on success.
 *		returns -1, on failure.
 * @pre assumes fom is valid, c2_fom_invariant should return true.
 * @todo needs further more implementation, once depending
 *		routines are ready.
 */
int c2_create_loc_ctx_wait(struct c2_fom *fom)
{
	C2_PRE(c2_fom_invariant(fom));

	fom->fo_phase = fp_table[fom->fo_phase].next_phase;
	return 0;
}

/**
 * Function to handle generic operations of fom
 * like authentication, authorisation, acquiring resources, &tc
 * @param fom -> c2_fom structure pointer.
 * @pre assumes fom is valid, c2_fom_invariant should return true.
 * @retval int -> returns 0, on success.
 *		  returns -1, on failure.
 */
int c2_fom_state_generic(struct c2_fom *fom)
{
	int rc = 0;
	bool stop = false;

	C2_PRE(c2_fom_invariant(fom) == true);

	while (!stop) {
		rc = fp_table[fom->fo_phase].action(fom);
		if (rc || fom->fo_phase == FOPH_DONE ||
			fom->fo_state == FOS_WAITING || fom->fo_phase == FOPH_EXEC)
			stop = true;
	}

	return rc;
}

/**
 * Transition table holding function pointers
 * to generic fom phases and next phase to transition into.
 * current phase is used as the offset in this table.
 */
void set_fom_phase_table(void)
{
	INIT_PHASE(FOPH_INIT, c2_fom_phase_init, FOPH_AUTHENTICATE)
	INIT_PHASE(FOPH_AUTHENTICATE, c2_fom_authen, FOPH_RESOURCE_LOCAL)
	INIT_PHASE(FOPH_AUTHENTICATE_WAIT, c2_fom_authen_wait, FOPH_RESOURCE_LOCAL)
	INIT_PHASE(FOPH_RESOURCE_LOCAL, c2_fom_loc_resource, FOPH_RESOURCE_DISTRIBUTED)
	INIT_PHASE(FOPH_RESOURCE_LOCAL_WAIT, c2_fom_loc_resource_wait, FOPH_RESOURCE_DISTRIBUTED)
	INIT_PHASE(FOPH_RESOURCE_DISTRIBUTED, c2_fom_dist_resource, FOPH_OBJECT_CHECK)
	INIT_PHASE(FOPH_RESOURCE_DISTRIBUTED_WAIT, c2_fom_dist_resource_wait, FOPH_OBJECT_CHECK)
	INIT_PHASE(FOPH_OBJECT_CHECK, c2_fom_obj_check, FOPH_AUTHORISATION)
	INIT_PHASE(FOPH_OBJECT_CHECK_WAIT, c2_fom_obj_check_wait, FOPH_AUTHORISATION)
	INIT_PHASE(FOPH_AUTHORISATION, c2_fom_auth, FOPH_TXN_CONTEXT)
	INIT_PHASE(FOPH_AUTHORISATION_WAIT, c2_fom_auth_wait, FOPH_TXN_CONTEXT)
	INIT_PHASE(FOPH_TXN_CONTEXT, c2_create_loc_ctx, FOPH_EXEC)
	INIT_PHASE(FOPH_TXN_CONTEXT_WAIT, c2_create_loc_ctx_wait, FOPH_EXEC)
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
