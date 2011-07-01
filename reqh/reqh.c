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
 * Reqh addb event location identifier object.
 */
const struct c2_addb_loc c2_reqh_addb_loc = {
	.al_name = "reqh"
};

/**
 * Reqh state of addb context.
 */
const struct c2_addb_ctx_type c2_reqh_addb_ctx_type = {
	.act_name = "t1-reqh"
};

/**
 * Reqh addb context.
 */
struct c2_addb_ctx c2_reqh_addb_ctx;

#define REQH_ADDB_ADD(addb_ctx, name, rc)  \
C2_ADDB_ADD(&addb_ctx, &c2_reqh_addb_loc, c2_addb_func_fail, (name), (rc))

/**
 * fop type object for a c2_reqh_error_rep fop.
 */
extern struct c2_fop_type c2_reqh_error_rep_fopt;

extern int reqh_fop_init(void);

extern void reqh_fop_fini(void);

/**
 * fom phase table to hold function pointers, which execute
 * various fom phases.
 */
static struct c2_fom_phase_ops fp_table[FOPH_NR];

/**
 * function to initialize fom phase table.
 */
void set_fom_phase_table(void);

extern bool c2_fom_invariant(const struct c2_fom *fom);

extern bool c2_locality_invariant(struct c2_fom_locality *loc);

/*
 * macro definition to set a fom phase in fom phase table.
 */
#define INIT_PHASE(curr_phase, act, np) \
	fp_table[curr_phase].fpo_action = act; \
	fp_table[curr_phase].fpo_nextphase = np; \
	fp_table[curr_phase].fpo_wait = false; \

/**
 * Function to initialize request handler and fom domain
 * @param reqh -> c2_reqh structure pointer.
 * @param rpc -> c2_rpc_machine structure pointer.
 * @param dtm -> c2_dtm structure pointer.
 * @param dom -> c2_stob_domain strutcure pointer.
 * @param fol -> c2_fol structure pointer.
 * @param serv -> c2_service structure pointer.
 * @retval int -> returns 0, on success.
 * 		returns -errno, on failure.
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
	reqh->rh_fom_dom = C2_ALLOC_PTR(reqh->rh_fom_dom);
	if (reqh->rh_fom_dom == NULL) {
		return -ENOMEM;
	}

	/* initialize reqh addb context */
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
 *
 * @param reqh -> c2_reqh structure pointer.
 *
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
 *
 * @param service -> struct c2_service pointer
 * @param cookie -> void pointer containing some address.
 * @param rc -> int, error code to be sent in reply fop.
 *
 * @todo c2_net_reply_post should be replaced by c2_rpc_post.
 */
void c2_reqh_send_err_rep(struct c2_service *service, void *cookie, int rc)
{
	struct c2_fop *rfop;
	struct c2_reqh_error_rep *out_fop;

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
 *
 * @param reqh -> c2_reqh structure pointer.
 * @param fom -> c2_fom sturcture pointer.
 * @param cookie -> void pointer to hold some pointer address.
 *
 * @pre reqh != null.
 * @pre fom != null.
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
	fom->fo_stdomain = reqh->rh_dom;
	fom->fo_domain = reqh->rh_fom_dom;

	/* locate fom's home locality */
	iloc = fom->fo_ops->fo_home_locality(fom);
	if (iloc >= 0) {
		fom->fo_loc = &reqh->rh_fom_dom->fd_localities[iloc];
		C2_ASSERT(c2_locality_invariant(fom->fo_loc));
	}

	/* submit fom for further processing */
	c2_fom_queue(fom);
}

void c2_reqh_fop_sortkey_get(struct c2_reqh *reqh, struct c2_fop *fop,
				struct c2_fop_sortkey *key)
{
}

/**
 * Funtion to handle init phase of fom.
 * Transitions fom to next phase.
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> returns FSO_AGAIN.
 *
 * @pre c2_fom_invariant(fom) == true.
 */
int c2_fom_phase_init(struct c2_fom *fom)
{
	int rc;
	C2_PRE(c2_fom_invariant(fom));

	fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Function to authenticate fop.
 * Transitions fom to next phase.
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> on success, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILED phase and
 *		logs addb even and returns FSO_AGAIN.
 *
 * @pre c2_fom_invariant(fom) == true.
 *
 * @todo needs further more implementation, once depending
 *		routines are ready to perform fop athentication.
 */
int c2_fom_authen(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(c2_fom_invariant(fom));

	/*
	 * In case of blocking operation, then set rc = FSO_WAIT,
	 * and fom->fo_phase to corresponding wait phase.
	 * else if operation is success, set rc = FSO_AGAIN and transition
	 * to next phase.
	 * in case of failure, set fom->fo_phase = FOPH_FAILED log addb event
	 * and set rc = FSO_AGAIN;
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILED;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "fop authentication failed", rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Invoked post wait in fop authentication phase.
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> on success, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILED phase and
 *		returns FSO_AGAIN.
 *
 * @pre c2_fom_invariant(fom) == true.
 *
 * @todo needs further more implementation, once depending
 *		routines are ready for fop authentication.
 */
int c2_fom_authen_wait(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(c2_fom_invariant(fom));

	/*
	 * check if operation is success, and transition to next phase and
	 * set rc = FSO_AGAIN.
	 * else if operation failed, set fom->fo_phase = FOPH_FAILED, log
	 * addb event and set rc = FSO_AGAIN.
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILED;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "fop authentication failed", rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Function to identify local resources required for
 * fop execution.
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> on success, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILED phase and
 *		returns FSO_AGAIN.
 *
 * @pre c2_fom_invariant(fom) == true.
 *
 * @todo needs further more implementation, once depending
 *		routines are ready to support this operation.
 */
int c2_fom_loc_resource(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(c2_fom_invariant(fom));

	/*
	 * In case of blocking operation, then set rc = FSO_WAIT,
	 * and fom->fo_phase to corresponding wait phase.
	 * else if operation is success, set rc = FSO_AGAIN and transition
	 * to next phase.
	 * in case of failure, set fom->fo_phase = FOPH_FAILED, log addb
	 * event and set rc = FSO_AGAIN.
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILED;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "local resource acquisition failed",
				rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Funtion invoked after a fom resumes execution post wait while
 * acquiring local resources.
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> on success, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILED phase and
 *		returns FSO_AGAIN.
 *
 * @pre c2_fom_invariant(fom) == true.
 *
 * @todo needs further more implementation, once depending
 *		routines are ready to support the operation.
 */
int c2_fom_loc_resource_wait(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(c2_fom_invariant(fom));

	/*
	 * check if operation is success, and transition to next phase and
	 * set rc = FSO_AGAIN.
	 * else if operation failed, set fom->fo_phase = FOPH_FAILED, log addb
	 * event, set rc = FSO_AGAIN.
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILED;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "local resource acquisition failed",
				rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Function to identify distributed resources required for fop
 * execution.
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> on success, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILED phase and
 *		returns FSO_AGAIN.
 *
 * @pre c2_fom_invariant(fom) == true.
 *
 * @todo needs further more implementation, once depending
 *		routines are ready to support the operation.
 */
int c2_fom_dist_resource(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(c2_fom_invariant(fom));

	/*
	 * In case of blocking operation, then set rc = FSO_WAIT,
	 * and fom->fo_phase to corresponding wait phase.
	 * else if operation is success, set rc = FSO_AGAIN and transition
	 * to next phase.
	 * in case of failure, set fom->fo_phase = FOPH_FAILED, log addb event
	 * and set rc = FSO_AGAIN.
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILED;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "acquiring distributed resources failed",
				rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Funtion invoked after a fom resumes execution post wait in
 * identifying distributed resources phase.
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> on success, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILED phase and
 *		returns FSO_AGAIN.
 *
 * @pre c2_fom_invariant(fom) == true.
 *
 * @todo needs further more implementation, once depending
 *		routines are ready to support operation.
 */
int c2_fom_dist_resource_wait(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(c2_fom_invariant(fom));

	/*
	 * check if operation is success, and transition to next phase and
	 * set rc = FSO_AGAIN.
	 * else if operation failed, set fom->fo_phase = FOPH_FAILED, log addb
	 * event, set rc = FSO_AGAIN.
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILED;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "acquiring distributed resources failed",
				rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Function to locate and load file system objects, required for
 * fop execution.
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> on success, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILED phase and
 *		returns FSO_AGAIN.
 *
 * @pre c2_fom_invariant(fom) == true.
 *
 * @todo needs further more implementation, once depending
 *		routines are ready to support the operation.
 */
int c2_fom_obj_check(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(c2_fom_invariant(fom));

	/*
	 * In case of blocking operation, then set rc = FSO_WAIT,
	 * and fom->fo_phase to corresponding wait phase.
	 * else if operation is success, set rc = FSO_AGAIN and transition
	 * to next phase.
	 * in case of failure, set fom->fo_phase = FOPH_FAILED, log addb event
	 * and set rc = FSO_AGAIN.
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILED;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "object checking failed", rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Funtion invoked after a fom resumes execution post wait,
 * in filesystem object checking phase.
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> on success, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILED phase and
 *		returns FSO_AGAIN.
 *
 * @pre c2_fom_invariant(fom) == true.
 *
 * @todo needs further more implementation, once depending
 *		routines are ready to support the operation.
 */
int c2_fom_obj_check_wait(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(c2_fom_invariant(fom));

	/*
	 * check if operation is success, and transition to next phase and
	 * set rc = FSO_AGAIN.
	 * else if operation failed, set fom->fo_phase = FOPH_FAILED, log addb
	 * event and set rc = FSO_AGAIN.
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILED;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "object checking failed", rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Function to authorise fop in fom
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> on success, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILED phase and
 *		returns FSO_AGAIN.
 *
 * @pre c2_fom_invariant(fom) == true.
 *
 * @todo needs further more implementation, once depending
 *		routines are ready to support the operation.
 */
int c2_fom_auth(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(c2_fom_invariant(fom));

	/*
	 * In case of blocking operation, then set rc = FSO_WAIT,
	 * and fom->fo_phase to corresponding wait phase.
	 * else if operation is success, set rc = FSO_AGAIN and transition
	 * to next phase.
	 * in case of failure, set fom->fo_phase = FOPH_FAILED, log addb
	 * event and set rc = FSO_AGAIN.
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILED;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "fop authorisation failed", rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Funtion invoked after a fom resumes execution post wait
 * in fop authorisation phase.
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> on success, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILED phase and
 *		returns FSO_AGAIN.
 *
 * @pre c2_fom_invariant(fom) == true.
 *
 * @todo needs further more implementation, once depending
 *		routines are ready to support the operation.
 */
int c2_fom_auth_wait(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(c2_fom_invariant(fom));

	/*
	 * check if operation is success, and transition to next phase and
	 * set rc = FSO_AGAIN.
	 * else if operation failed, set fom->fo_phase = FOPH_FAILED, log addb
	 * event and set rc = FSO_AGAIN.
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILED;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "fop authorisation failed", rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Funtion to create local transactional context, i.e we initialize a
 * struct c2_dtx object, which would be used for db transaction throughout
 * fop execution.
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> on success, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILED phase and
 *		returns FSO_AGAIN.
 *
 * @pre c2_fom_invariant(fom) == true.
 */
int c2_create_loc_ctx(struct c2_fom *fom)
{
	int rc;

	C2_PRE(c2_fom_invariant(fom));

	/*
	 * In case of blocking operation, then set rc = FSO_WAIT,
	 * and fom->fo_phase to corresponding wait phase.
	 * else if operation is success, set rc = FSO_AGAIN and transition
	 * to next phase.
	 * in case of failure, set fom->fo_phase = FOPH_FAILED, log addb event
	 * and set rc = FSO_AGAIN.
	 */

	rc = fom->fo_stdomain->sd_ops->sdo_tx_make(fom->fo_stdomain,
							&fom->fo_tx);
	if (rc != 0) {
		fom->fo_phase = FOPH_FAILED;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "transaction object creation failed", rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Funtion to invoke after a fom resumes execution
 * post wait while creating local transactional context.
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> on success, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILED phase and
 *		returns FSO_AGAIN.
 *
 * @pre c2_fom_invariant(fom) == true.
 *
 * @todo currently db operations may block, thus will need to implement wait phase
 * 		once we have non-blocking db routines.
 */
int c2_create_loc_ctx_wait(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(c2_fom_invariant(fom));

	/*
	 * check if operation is success, and transition to next phase and
	 * set rc = FSO_AGAIN.
	 * else if operation failed, set fom->fo_phase = FOPH_FAILED, log addb
	 * event and set rc = FSO_AGAIN.
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILED;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "transaction object creation failed", rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * This function is invoked if fom execution fails.
 * An error fop reply is sent and we transition to next phase,
 * i.e abort db transaction.
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> transitions fom to next phase and
 *		returns FSO_AGAIN.
 *
 * @pre c2_fom_invariant(fom) == true.
 */
int c2_fom_failed(struct c2_fom *fom)
{
	int rc;

	C2_PRE(c2_fom_invariant(fom));
	C2_ASSERT(fom->fo_phase == FOPH_FAILED);

	c2_reqh_send_err_rep(fom->fo_fop_ctx->ft_service, fom->fo_fop_ctx->fc_cookie, 1);
	fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;
	rc = FSO_AGAIN;
	return rc;
}

/**
 * This function is invoked if fom execution is sucessfully
 * completed. fom transitions to next phase i.e commit db transaction.
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> transitions fom to next phase and
 *		returns FSO_AGAIN.
 *
 * @pre c2_fom_invariant(fom) == true.
 */
int c2_fom_success(struct c2_fom *fom)
{
	int rc;

	C2_PRE(c2_fom_invariant(fom));
	fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;
	rc = FSO_AGAIN;
	return rc;
}

/**
 * Commits local db transaction.
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> returns FSO_WAIT,
 * 		 logs addb event on db commit failure.
 *
 * @pre c2_fom_invariant(fom) == true.
 *
 * @todo currently db transaction routines may block, hence would need more
 * 	implementation once non blocking db routines are available.
 */
int c2_fom_txn_commit(struct c2_fom *fom)
{
	int rc;

	C2_PRE(c2_fom_invariant(fom));

	/*
	 * In case of blocking operation, then set rc = FSO_WAIT,
	 * and fom->fo_phase to corresponding wait phase.
	 * else if operation is success, transition to next phase,
	 * and set rc = 0, as we are done. else log addb event.
	 */

	/* Commit db transaction.*/
	rc = c2_db_tx_commit(&fom->fo_tx.tx_dbtx);

	if (rc != 0)
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "DB commit failed", rc);

	fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_WAIT;
	return rc;
}

/**
 * Function invoked post db transaction commit wait phase.
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> returns FSO_WAIT,
 * 		 logs addb event on db commit failure.
 *
 * @pre c2_fom_invariant(fom) == true.
 *
 * @todo currently db transaction routines may block, hence would need more
 * 	implementation once non blocking db routines are available.
 */
int c2_fom_txn_commit_wait(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(c2_fom_invariant(fom));

	/*
	 * check if operation was success, transition to next phase,
	 * and set rc = 0, as fom execution is done, else log addb
	 * event.
	 */
	if (rc != 0)
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "DB commit failed", rc);

	fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;
	rc = FSO_WAIT;
	return rc;
}

/**
 * Function to check if transaction context is valid.
 * In case of failure, we abort the transaction, thus,
 * if we fail before even the transaction is initialised.
 * the abort will fail.
 *
 * @param tx -> struct c2_db_tx pointer.
 *
 * @retval bool -> returns true, if transaction is initialised
 *		return false, if transaction is uninitialised.
 */
static bool is_tx_initialised(struct c2_db_tx *tx)
{
	return tx->dt_env != 0;
}

/**
 * Function to abort db transaction.
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> returns FSO_WAIT,
 * 		 logs addb event on db abort failure.
 *
 * @pre c2_fom_invariant(fom) == true.
 *
 * @todo currently db transaction routines may block, hence would need more
 * 	implementation once non blocking db routines are available.
 */
int c2_fom_txn_abort(struct c2_fom *fom)
{
	int rc;

	C2_PRE(c2_fom_invariant(fom));

	/*
	 * check if local transaction is initialised, or
	 * we failed before creating one.
	 */
	if (is_tx_initialised(&fom->fo_tx.tx_dbtx)) {
		rc = c2_db_tx_abort(&fom->fo_tx.tx_dbtx);
		if (rc != 0)
			REQH_ADDB_ADD(fom->fo_fop->f_addb, "DB abort failed", rc);
	}

	fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;
	rc = FSO_WAIT;
	return rc;
}

/**
 * Invoked post wait in db transaction abort phase.
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> returns FSO_WAIT,
 * 		 logs addb event on db abort failure.
 *
 * @pre c2_fom_invariant(fom) == true.
 *
 * @todo currently db transaction routines may block, hence would need more
 * 	implementation once non blocking db routines are available.
 */
int c2_fom_txn_abort_wait(struct c2_fom *fom)
{
	int rc;

	C2_PRE(c2_fom_invariant(fom));

	/* check if we aborted successfully, else log addb event.*/

	if (rc != 0)
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "DB abort failed", rc);

	fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;
	rc = FSO_WAIT;
	return rc;
}

/**
 * Posts reply fop.
 * Transitions back to fom specific non standard phase.
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> returns FSO_AGAIN,
 * 		 logs addb event on failure.
 *
 * @pre c2_fom_invariant(fom) == true.
 *
 * @todo currently we don't have write back cache implementation, in which
 *	we may perform updations on local objects and re integrate with
 * 	server later, in that case we may block for cache space, that would
 *	need more implementation for this routine.
 */
int c2_fom_queue_reply(struct c2_fom *fom)
{
	int rc;

	C2_PRE(c2_fom_invariant(fom));
	C2_ASSERT(fom->fo_rep_fop != NULL);

	/*
	 * In case of cacheable operation at client side, all the operations
	 * are executed on local objects, here we cache fop, if we block while
	 * doing so, change fom phase to FOPH_QUEUE_REPLY_WAIT, else transition
	 * to next phase.
	 */

	c2_net_reply_post(fom->fo_fop_ctx->ft_service, fom->fo_rep_fop, fom->fo_fop_ctx->fc_cookie);
	fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;
	rc = FSO_AGAIN;

	return rc;
}

/**
 * Invoked post wait in fom queue reply phase.
 * Transitions back to fom specific non standard phase.
 *
 * @param fom -> c2_fom object.
 *
 * @retval int -> returns FSO_AGAIN,
 * 		 logs addb event on failure.
 *
 * @pre c2_fom_invariant(fom) == true.
 *
 * @todo currently we don't have write back cache implementation, in which
 *	we may perform updations on local objects and re integrate with
 * 	server later, in that case we may block for cache space, that would
 *	need more implementation for this routine.
 */
int c2_fom_queue_reply_wait(struct c2_fom *fom)
{
	int rc;

	C2_PRE(c2_fom_invariant(fom));

	/* transition to next phase */
	fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;
	rc = FSO_AGAIN;

	return rc;
}

/**
 * Function to handle standard/generic operations of fom
 * like authentication, authorisation, acquiring resources, &tc
 *
 * @param fom -> c2_fom object.
 *
 * @pre c2_fom_invariant(fom) == true.
 *
 * @retval int -> returns FSO_AGAIN, on success.
 *	returns FSO_WAIT, if operation blocks or fom execution ends.
 */
int c2_fom_state_generic(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(c2_fom_invariant(fom) == true);
	rc = fp_table[fom->fo_phase].fpo_action(fom);

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
	INIT_PHASE(FOPH_TXN_CONTEXT, c2_create_loc_ctx, FOPH_NR+1)
	INIT_PHASE(FOPH_TXN_CONTEXT_WAIT, c2_create_loc_ctx_wait, FOPH_NR+1)
	INIT_PHASE(FOPH_QUEUE_REPLY, c2_fom_queue_reply, FOPH_NR+1)
	INIT_PHASE(FOPH_QUEUE_REPLY_WAIT, c2_fom_queue_reply_wait, FOPH_NR+1)
	INIT_PHASE(FOPH_FAILED, c2_fom_failed, FOPH_TXN_ABORT)
	INIT_PHASE(FOPH_SUCCESS, c2_fom_success, FOPH_TXN_COMMIT)
	INIT_PHASE(FOPH_TXN_COMMIT, c2_fom_txn_commit, FOPH_DONE)
	INIT_PHASE(FOPH_TXN_COMMIT_WAIT, c2_fom_txn_commit_wait, FOPH_DONE)
	INIT_PHASE(FOPH_TXN_ABORT, c2_fom_txn_abort, FOPH_DONE)
	INIT_PHASE(FOPH_TXN_ABORT_WAIT, c2_fom_txn_abort_wait, FOPH_DONE)
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
