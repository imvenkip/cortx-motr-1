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
	.act_name = "reqh"
};

/**
 * Reqh addb context.
 */
struct c2_addb_ctx c2_reqh_addb_ctx;

#define REQH_ADDB_ADD(addb_ctx, name, rc)  \
C2_ADDB_ADD(&addb_ctx, &c2_reqh_addb_loc, c2_addb_func_fail, (name), (rc))

/**
 * Reqh generic error fop type.
 */
extern struct c2_fop_type c2_reqh_error_rep_fopt;

extern int reqh_fop_init(void);

extern void reqh_fop_fini(void);

/**
 * Table structure to hold fom phase transition and execution.
 */
struct fom_phase_ops {
        /** function pointer to phase execution routine */
        int (*fpo_action) (struct c2_fom *fom);
        /** next phase to transition into */
        int fpo_nextphase;
        /** wait flag */
        bool fpo_wait;
};

/**
 * c2_fom_phase_ops table, contains implementations of fom phases,
 * next transitional fom phase, and fom wait flag.
 * various fom phases.
 */
static struct fom_phase_ops fp_table[FOPH_NR];

/**
 * Initialises fom phase table.
 */
static void set_fom_phase_table(void);

extern bool c2_fom_invariant(const struct c2_fom *fom);

extern bool c2_locality_invariant(struct c2_fom_locality *loc);

extern bool c2_fom_domain_invariant(struct c2_fom_domain *dom);

/*
 * macro definition to initialise a fom phase in fom phase table.
 */
#define INIT_PHASE(curr_phase, act, np) \
	fp_table[curr_phase].fpo_action = act; \
	fp_table[curr_phase].fpo_nextphase = np; \
	fp_table[curr_phase].fpo_wait = false; \

int  c2_reqh_init(struct c2_reqh *reqh,
		struct c2_rpcmachine *rpc, struct c2_dtm *dtm,
		struct c2_stob_domain *stdom, struct c2_fol *fol,
		struct c2_service *serv)
{
	int result;

	C2_PRE(reqh != NULL && stdom != NULL &&
		fol != NULL && serv != NULL);

	/* initialise fom phase table with standard/generic fom phases,
	 * and their correspnding methods.
	 */
	set_fom_phase_table();

	/* allocate and initialise fom domain  */
	reqh->rh_fom_dom = C2_ALLOC_PTR(reqh->rh_fom_dom);
	if (reqh->rh_fom_dom == NULL) {
		return -ENOMEM;
	}

	/* initialise reqh addb context */
	c2_addb_ctx_init(&c2_reqh_addb_ctx, &c2_reqh_addb_ctx_type,
					&c2_addb_global_ctx);

	/* initialise reqh fops */
	reqh_fop_init();

	result = c2_fom_domain_init(reqh->rh_fom_dom);
	if (result) {
		REQH_ADDB_ADD(c2_reqh_addb_ctx,
				"c2_reqh_init", result);
		return result;
	}

	C2_ASSERT(c2_fom_domain_invariant(reqh->rh_fom_dom));
	/* initialise reqh members */
	reqh->rh_rpc = rpc;
	reqh->rh_dtm = dtm;
	reqh->rh_stdom = stdom;
	reqh->rh_fol = fol;
	reqh->rh_serv = serv;

	return result;
}

void c2_reqh_fini(struct c2_reqh *reqh)
{
	C2_PRE(reqh != NULL);
	C2_PRE(reqh->rh_fom_dom != NULL);
	c2_fom_domain_fini(reqh->rh_fom_dom);
	c2_addb_ctx_fini(&c2_reqh_addb_ctx);
	reqh_fop_fini();
}

/**
 * Sends generic error reply fop
 *
 * @param service -> c2_service
 * @param cookie -> void reference provided by client.
 * @param rc -> int, error code to be sent in reply fop.
 *
 * @todo c2_net_reply_post to be replaced by c2_rpc_post.
 */
static void reqh_send_err_rep(struct c2_service *service, void *cookie, int rc)
{
	struct c2_fop			*rfop;
	struct c2_reqh_error_rep	*out_fop;

	rfop = c2_fop_alloc(&c2_reqh_error_rep_fopt, NULL);
	if (rfop == NULL)
		return;
	out_fop = c2_fop_data(rfop);
	out_fop->sierr_rc = rc;
	/* Will be using c2_rpc_post in future */
	c2_net_reply_post(service, rfop, cookie);
}

void c2_reqh_fop_handle(struct c2_reqh *reqh, struct c2_fop *fop, void *cookie)
{
	struct c2_fom	       *fom = NULL;
	int			result;
	size_t			iloc;
	struct c2_fop_ctx      *fo_ctx;

	C2_PRE(reqh != NULL);
	C2_PRE(fop != NULL);

	C2_ALLOC_PTR(fo_ctx);
	if (fo_ctx == NULL)
		return;

	fo_ctx->ft_service = reqh->rh_serv;
	fo_ctx->fc_cookie = cookie;

	/* Initialise fom for fop processing */
	result = fop->f_type->ft_ops->fto_fom_init(fop, &fom);
	if (result != 0) {
		if (result != -ENOMEM) {
			/* send an error reply */
			reqh_send_err_rep(reqh->rh_serv, cookie, result);
			REQH_ADDB_ADD(c2_reqh_addb_ctx,
					"c2_reqh_fop_handle", result);
		}
		return;
	}

	fom->fo_fop_ctx = fo_ctx;
	fom->fo_fol = reqh->rh_fol;
	fom->fo_stdomain = reqh->rh_stdom;
	fom->fo_domain = reqh->rh_fom_dom;

	/* locate fom's home locality */
	iloc = fom->fo_ops->fo_home_locality(fom);
	C2_ASSERT(iloc >= 0);
	fom->fo_loc = &reqh->rh_fom_dom->fd_localities[iloc];
	C2_ASSERT(c2_fom_invariant(fom));
	C2_ASSERT(c2_locality_invariant(fom->fo_loc));

	/* submit fom for further processing */
	c2_fom_queue(fom);
}

void c2_reqh_fop_sortkey_get(struct c2_reqh *reqh, struct c2_fop *fop,
				struct c2_fop_sortkey *key)
{
}

/**
 * Fom init phase.
 * Transitions fom to next standard phase.
 *
 * @param fom -> c2_fom.
 *
 * @retval int -> returns FSO_AGAIN.
 *
 * @pre fom->fo_phase == FOPH_INIT.
 */
static int fom_phase_init(struct c2_fom *fom)
{
	int rc;

	C2_PRE(fom->fo_phase == FOPH_INIT);

	fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Authenticates fop.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_phase = FOPH_AUTHENTICATE.
 *
 * @retval int -> if succeeds, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILURE phase and
 *		logs addb even and returns FSO_AGAIN.
 *
 * @todo needs further more implementation, once depending
 *		routines are ready to perform fop athentication.
 */
static int fom_authen(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(fom->fo_phase == FOPH_AUTHENTICATE);

	/*
	 * In case of blocking operation, then set rc = FSO_WAIT,
	 * and fom->fo_phase to corresponding wait phase.
	 * else if operation is succeed, set rc = FSO_AGAIN and transition
	 * to next phase.
	 * in case of failure, set fom->fo_phase = FOPH_FAILURE log addb event
	 * and set rc = FSO_AGAIN;
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILURE;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "fom_authen", rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Resumes fom execution post wait in FOPH_AUTHENTICATE
 * phase.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_phase == FOPH_AUTHENTICATE_WAIT.
 *
 * @retval int -> if succeeds, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILURE phase and
 *		returns FSO_AGAIN.
 *
 * @todo needs further more implementation, once depending
 *		routines are ready for fop authentication.
 */
static int fom_authen_wait(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(fom->fo_phase == FOPH_AUTHENTICATE_WAIT);

	/*
	 * check if operation is succeed, and transition to next phase and
	 * set rc = FSO_AGAIN.
	 * else if operation failure, set fom->fo_phase = FOPH_FAILURE, log
	 * addb event and set rc = FSO_AGAIN.
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILURE;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "fom_authen_wait", rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Identifies local resources required for fom
 * execution.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_phase == FOPH_RESOURCE_LOCAL.
 *
 * @retval int -> if succeeds, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILURE phase and
 *		returns FSO_AGAIN.
 *
 * @todo needs further more implementation, once depending
 *		routines are ready to support this operation.
 */
static int fom_loc_resource(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(fom->fo_phase == FOPH_RESOURCE_LOCAL);

	/*
	 * In case of blocking operation, then set rc = FSO_WAIT,
	 * and fom->fo_phase to corresponding wait phase.
	 * else if operation is succeed, set rc = FSO_AGAIN and transition
	 * to next phase.
	 * in case of failure, set fom->fo_phase = FOPH_FAILURE, log addb
	 * event and set rc = FSO_AGAIN.
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILURE;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "fom_loc_resource", rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Resumes fom execution post wait in FOPH_RESOURCE_LOCAL phase.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_phase == FOPH_RESOURCE_LOCAL_WAIT.
 *
 * @retval int -> if succeeds, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILURE phase and
 *		returns FSO_AGAIN.
 *
 * @todo needs further more implementation, once depending
 *		routines are ready to support the operation.
 */
static int fom_loc_resource_wait(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(fom->fo_phase == FOPH_RESOURCE_LOCAL_WAIT);

	/*
	 * check if operation is succeed, and transition to next phase and
	 * set rc = FSO_AGAIN.
	 * else if operation failure, set fom->fo_phase = FOPH_FAILURE, log addb
	 * event, set rc = FSO_AGAIN.
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILURE;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "fom_loc_resource_wait", rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Identifies distributed resources needed for fom execution.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_phase == FOPH_RESOURCE_DISTRIBUTED.
 *
 * @retval int -> if succeeds, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILURE phase and
 *		returns FSO_AGAIN.
 *
 * @todo needs further more implementation, once depending
 *		routines are ready to support the operation.
 */
static int fom_dist_resource(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(fom->fo_phase == FOPH_RESOURCE_DISTRIBUTED);

	/*
	 * In case of blocking operation, then set rc = FSO_WAIT,
	 * and fom->fo_phase to corresponding wait phase.
	 * else if operation is succeed, set rc = FSO_AGAIN and transition
	 * to next phase.
	 * in case of failure, set fom->fo_phase = FOPH_FAILURE, log addb event
	 * and set rc = FSO_AGAIN.
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILURE;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "fom_dist_resource", rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Resumes fom execution post wait in FOPH_RESOURCE_DISTRIBUTED phase.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_phase == FOPH_RESOURCE_DISTRIBUTED_WAIT.
 *
 * @retval int -> if succeeds, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILURE phase and
 *		returns FSO_AGAIN.
 *
 * @todo needs further more implementation, once depending
 *		routines are ready to support operation.
 */
static int fom_dist_resource_wait(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(fom->fo_phase == FOPH_RESOURCE_DISTRIBUTED_WAIT);

	/*
	 * check if operation is succeed, and transition to next phase and
	 * set rc = FSO_AGAIN.
	 * else if operation failure, set fom->fo_phase = FOPH_FAILURE, log addb
	 * event, set rc = FSO_AGAIN.
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILURE;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "fom_dist_resource_wait", rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Locates and loads file system object required for fom
 * execution.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_phase == FOPH_OBJECT_CHECK.
 *
 * @retval int -> if succeeds, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILURE phase and
 *		returns FSO_AGAIN.
 *
 * @todo needs further more implementation, once depending
 *		routines are ready to support the operation.
 */
static int fom_obj_check(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(fom->fo_phase == FOPH_OBJECT_CHECK);

	/*
	 * In case of blocking operation, then set rc = FSO_WAIT,
	 * and fom->fo_phase to corresponding wait phase.
	 * else if operation is succeed, set rc = FSO_AGAIN and transition
	 * to next phase.
	 * in case of failure, set fom->fo_phase = FOPH_FAILURE, log addb event
	 * and set rc = FSO_AGAIN.
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILURE;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "fom_obj_check", rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Resumes fom execution post wait in FOPH_OBJECT_CHECK.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_phase == FOPH_OBJECT_CHECK_WAIT.
 *
 * @retval int -> if succeeds, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILURE phase and
 *		returns FSO_AGAIN.
 *
 * @todo needs further more implementation, once depending
 *		routines are ready to support the operation.
 */
static int fom_obj_check_wait(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(fom->fo_phase == FOPH_OBJECT_CHECK_WAIT);

	/*
	 * check if operation is succeed, and transition to next phase and
	 * set rc = FSO_AGAIN.
	 * else if operation failure, set fom->fo_phase = FOPH_FAILURE, log addb
	 * event and set rc = FSO_AGAIN.
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILURE;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "fom_obj_check_wait", rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Authorises fop.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_phase == FOPH_AUTHORISATION.
 *
 * @retval int -> if succeeds, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILURE phase and
 *		returns FSO_AGAIN.
 *
 * @todo needs further more implementation, once depending
 *		routines are ready to support the operation.
 */
static int fom_auth(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(fom->fo_phase == FOPH_AUTHORISATION);

	/*
	 * In case of blocking operation, then set rc = FSO_WAIT,
	 * and fom->fo_phase to corresponding wait phase.
	 * else if operation is succeed, set rc = FSO_AGAIN and transition
	 * to next phase.
	 * in case of failure, set fom->fo_phase = FOPH_FAILURE, log addb
	 * event and set rc = FSO_AGAIN.
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILURE;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "fom_auth", rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Resumes fom execution post wait in FOPH_AUTHORISATION.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_phase == FOPH_AUTHORISATION_WAIT.
 *
 * @retval int -> if succeeds, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILURE phase and
 *		returns FSO_AGAIN.
 *
 * @todo needs further more implementation, once depending
 *		routines are ready to support the operation.
 */
static int fom_auth_wait(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(fom->fo_phase == FOPH_AUTHORISATION_WAIT);

	/*
	 * check if operation is succeed, and transition to next phase and
	 * set rc = FSO_AGAIN.
	 * else if operation failure, set fom->fo_phase = FOPH_FAILURE, log addb
	 * event and set rc = FSO_AGAIN.
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILURE;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "fom_auth_wait", rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Creates local transactional object, required in fom io transactions.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_phase == FOPH_TXN_CONTEXT.
 *
 * @retval int -> if succeeds, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILURE phase and
 *		returns FSO_AGAIN.
 */
static int create_loc_ctx(struct c2_fom *fom)
{
	int rc;

	C2_PRE(fom->fo_phase == FOPH_TXN_CONTEXT);

	/*
	 * In case of blocking operation, then set rc = FSO_WAIT,
	 * and fom->fo_phase to corresponding wait phase.
	 * else if operation is succeed, set rc = FSO_AGAIN and transition
	 * to next phase.
	 * in case of failure, set fom->fo_phase = FOPH_FAILURE, log addb event
	 * and set rc = FSO_AGAIN.
	 */

	rc = fom->fo_stdomain->sd_ops->sdo_tx_make(fom->fo_stdomain,
							&fom->fo_tx);
	if (rc != 0) {
		fom->fo_phase = FOPH_FAILURE;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "create_loc_ctx", rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Resumes fom execution post wait in FOPH_TXN_CONTEXT.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_phase == FOPH_TXN_CONTEXT_WAIT.
 *
 * @retval int -> if succeeds, transitions fom to next phase and
 *		returns FSO_AGAIN.
 *		on failure, transitions fom to FOPH_FAILURE phase and
 *		returns FSO_AGAIN.
 *
 * @todo currently db operations may block, thus will need to implement wait phase
 *	once we have non-blocking db routines.
 */
static int create_loc_ctx_wait(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(fom->fo_phase == FOPH_TXN_CONTEXT_WAIT);

	/*
	 * check if operation is succeed, and transition to next phase and
	 * set rc = FSO_AGAIN.
	 * else if operation failure, set fom->fo_phase = FOPH_FAILURE, log addb
	 * event and set rc = FSO_AGAIN.
	 */

	if (rc != 0) {
		fom->fo_phase = FOPH_FAILURE;
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "create_loc_ctx_wait", rc);
	} else
		fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_AGAIN;
	return rc;
}

/**
 * Handles failure during fom execution, sends an error
 * reply fop and transitions to FOPH_DONE phase.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_phase == FOPH_FAILURE.
 *
 * @retval int -> transitions fom to next phase and
 *		returns FSO_AGAIN.
 */
static int fom_failure(struct c2_fom *fom)
{
	int rc;

	C2_PRE(fom->fo_phase == FOPH_FAILURE);

	reqh_send_err_rep(fom->fo_fop_ctx->ft_service, fom->fo_fop_ctx->fc_cookie, 1);
	fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;
	rc = FSO_AGAIN;
	return rc;
}

/**
 * Handles fom execution success, transitions to FOPH_DONE
 * phase.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_phase == FOPH_SUCCEED.
 *
 * @retval int -> transitions fom to next phase and
 *		returns FSO_AGAIN.
 */
static int fom_succeed(struct c2_fom *fom)
{
	int rc;

	C2_PRE(fom->fo_phase == FOPH_SUCCEED);

	fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;
	rc = FSO_AGAIN;
	return rc;
}

/**
 * Commits local db transaction.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_phase == FOPH_TXN_COMMIT.
 *
 * @retval int -> returns FSO_WAIT,
 *		logs addb event on db commit failure.
 *
 * @todo currently db transaction routines may block, hence would need more
 *	implementation once non blocking db routines are available.
 */
static int fom_txn_commit(struct c2_fom *fom)
{
	int rc;

	C2_PRE(fom->fo_phase == FOPH_TXN_COMMIT);

	/*
	 * In case of blocking operation, then set rc = FSO_WAIT,
	 * and fom->fo_phase to corresponding wait phase.
	 * else if operation is succeed, transition to next phase,
	 * and set rc = 0, as we are done. else log addb event.
	 */

	/* Commit db transaction.*/
	rc = c2_db_tx_commit(&fom->fo_tx.tx_dbtx);

	if (rc != 0)
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "fom_txn_commit", rc);

	fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;

	rc = FSO_WAIT;
	return rc;
}

/**
 * Resumes fom execution post wait in FOPH_TXN_COMMIT phase.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_phase == FOPH_TXN_COMMIT_WAIT.
 *
 * @retval int -> returns FSO_WAIT,
 *		logs addb event on db commit failure.
 *
 * @todo currently db transaction routines may block, hence would need more
 *	implementation once non blocking db routines are available.
 */
static int fom_txn_commit_wait(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(fom->fo_phase == FOPH_TXN_COMMIT_WAIT);

	/*
	 * check if operation was succeed, transition to next phase,
	 * and set rc = 0, as fom execution is done, else log addb
	 * event.
	 */
	if (rc != 0)
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "fom_txn_commit_wait", rc);

	fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;
	rc = FSO_WAIT;
	return rc;
}

/**
 * Checks if transaction context is valid.
 * In case of failure, we abort the transaction, thus,
 * if we fail before even the transaction is initialised,
 * we don't need to abort any transaction.
 *
 * @param tx -> c2_db_tx.
 *
 * @retval bool -> returns true, if transaction is initialised
 *		return false, if transaction is uninitialised.
 */
static bool is_tx_initialised(struct c2_db_tx *tx)
{
	return tx->dt_env != 0;
}

/**
 * Aborts db transaction.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_phase == FOPH_TXN_ABORT.
 *
 * @retval int -> returns FSO_WAIT,
 *		logs addb event on db abort failure.
 *
 * @todo currently db transaction routines may block, hence would need more
 *	implementation once non blocking db routines are available.
 */
static int fom_txn_abort(struct c2_fom *fom)
{
	int rc;

	C2_PRE(fom->fo_phase == FOPH_TXN_ABORT);

	/*
	 * check if local transaction is initialised, or
	 * we failure before creating one.
	 */
	if (is_tx_initialised(&fom->fo_tx.tx_dbtx)) {
		rc = c2_db_tx_abort(&fom->fo_tx.tx_dbtx);
		if (rc != 0)
			REQH_ADDB_ADD(fom->fo_fop->f_addb, "fom_txn_abort", rc);
	}

	fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;
	rc = FSO_WAIT;
	return rc;
}

/**
 * Resumes fom execution post wait in FOPH_TXN_ABORT phase.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_phase == FOPH_TXN_ABORT_WAIT.
 *
 * @retval int -> returns FSO_WAIT,
 *		logs addb event on db abort failure.
 *
 * @todo currently db transaction routines may block, hence would need more
 *	implementation once non blocking db routines are available.
 */
static int fom_txn_abort_wait(struct c2_fom *fom)
{
	int rc;

	C2_PRE(fom->fo_phase == FOPH_TXN_ABORT_WAIT);

	/* check if we aborted succeedfully, else log addb event.*/

	if (rc != 0)
		REQH_ADDB_ADD(fom->fo_fop->f_addb, "fom_txn_abort_wait", rc);

	fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;
	rc = FSO_WAIT;
	return rc;
}

/**
 * Posts reply fop.
 * Transitions back to fom specific non standard phase.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_phase == FOPH_QUEUE_REPLY.
 * @pre fom->fo_rep_fop != NULL.
 *
 * @retval int -> returns FSO_AGAIN,
 *		logs addb event on failure.
 *
 * @todo currently we don't have write back cache implementation, in which
 *	we may perform updations on local objects and re integrate with
 *	server later, in that case we may block for cache space, that would
 *	need more implementation for this routine.
 */
static int fom_queue_reply(struct c2_fom *fom)
{
	int rc;

	C2_PRE(fom->fo_phase == FOPH_QUEUE_REPLY);
	C2_PRE(fom->fo_rep_fop != NULL);

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
 * Resumes fom execution post wait in FOPH_QUEUE_REPLY phase.
 * Transitions back to fom specific non standard phase.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_phase == FOPH_QUEUE_REPLY_WAIT.
 *
 * @retval int -> returns FSO_AGAIN,
 *		logs addb event on failure.
 *
 * @todo currently we don't have write back cache implementation, in which
 *	we may perform updations on local objects and re integrate with
 *	server later, in that case we may block for cache space, that would
 *	need more implementation for this routine.
 */
static int fom_queue_reply_wait(struct c2_fom *fom)
{
	int rc;

	C2_PRE(fom->fo_phase == FOPH_QUEUE_REPLY_WAIT);

	/* transition to next phase */
	fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;
	rc = FSO_AGAIN;

	return rc;
}

int c2_fom_state_generic(struct c2_fom *fom)
{
	int rc = 0;

	C2_ASSERT(c2_fom_invariant(fom) == true);

	rc = fp_table[fom->fo_phase].fpo_action(fom);

	return rc;
}

/**
 * Transition table holding function pointers
 * to generic fom phases and next phase to transition into.
 * current phase is used as the offset in this table.
 */
static void set_fom_phase_table(void)
{
	INIT_PHASE(FOPH_INIT, fom_phase_init, FOPH_AUTHENTICATE)
	INIT_PHASE(FOPH_AUTHENTICATE, fom_authen, FOPH_RESOURCE_LOCAL)
	INIT_PHASE(FOPH_AUTHENTICATE_WAIT, fom_authen_wait, FOPH_RESOURCE_LOCAL)
	INIT_PHASE(FOPH_RESOURCE_LOCAL, fom_loc_resource, FOPH_RESOURCE_DISTRIBUTED)
	INIT_PHASE(FOPH_RESOURCE_LOCAL_WAIT, fom_loc_resource_wait, FOPH_RESOURCE_DISTRIBUTED)
	INIT_PHASE(FOPH_RESOURCE_DISTRIBUTED, fom_dist_resource, FOPH_OBJECT_CHECK)
	INIT_PHASE(FOPH_RESOURCE_DISTRIBUTED_WAIT, fom_dist_resource_wait, FOPH_OBJECT_CHECK)
	INIT_PHASE(FOPH_OBJECT_CHECK, fom_obj_check, FOPH_AUTHORISATION)
	INIT_PHASE(FOPH_OBJECT_CHECK_WAIT, fom_obj_check_wait, FOPH_AUTHORISATION)
	INIT_PHASE(FOPH_AUTHORISATION, fom_auth, FOPH_TXN_CONTEXT)
	INIT_PHASE(FOPH_AUTHORISATION_WAIT, fom_auth_wait, FOPH_TXN_CONTEXT)
	INIT_PHASE(FOPH_TXN_CONTEXT, create_loc_ctx, FOPH_NR+1)
	INIT_PHASE(FOPH_TXN_CONTEXT_WAIT, create_loc_ctx_wait, FOPH_NR+1)
	INIT_PHASE(FOPH_QUEUE_REPLY, fom_queue_reply, FOPH_NR+1)
	INIT_PHASE(FOPH_QUEUE_REPLY_WAIT, fom_queue_reply_wait, FOPH_NR+1)
	INIT_PHASE(FOPH_FAILURE, fom_failure, FOPH_TXN_ABORT)
	INIT_PHASE(FOPH_SUCCEED, fom_succeed, FOPH_TXN_COMMIT)
	INIT_PHASE(FOPH_TXN_COMMIT, fom_txn_commit, FOPH_DONE)
	INIT_PHASE(FOPH_TXN_COMMIT_WAIT, fom_txn_commit_wait, FOPH_DONE)
	INIT_PHASE(FOPH_TXN_ABORT, fom_txn_abort, FOPH_DONE)
	INIT_PHASE(FOPH_TXN_ABORT_WAIT, fom_txn_abort_wait, FOPH_DONE)
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
