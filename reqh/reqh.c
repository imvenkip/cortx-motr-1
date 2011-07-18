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
#include "lib/misc.h"
#include "stob/stob.h"
#include "net/net.h"
#include "fop/fop.h"
#include "fop/fom.h"
#include "fop/fop_iterator.h"
#include "dtm/dtm.h"
#include "fop/fop_format_def.h"

#ifdef __KERNEL__
#include "reqh_fops_k.h"
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
        /* Phase execution routine */
        int (*fpo_action) (struct c2_fom *fom);
        /* Next phase to transition into */
        int fpo_nextphase;
	/* Phase name */
	const char *fpo_name;
};

int  c2_reqh_init(struct c2_reqh *reqh,
		struct c2_rpcmachine *rpc, struct c2_dtm *dtm,
		struct c2_stob_domain *stdom, struct c2_fol *fol,
		struct c2_service *serv)
{
	int result;

	C2_PRE(reqh != NULL && stdom != NULL &&
		fol != NULL && serv != NULL);

	c2_addb_ctx_init(&c2_reqh_addb_ctx, &c2_reqh_addb_ctx_type,
					&c2_addb_global_ctx);

	/* Initialise generic reqh fops */
	reqh_fop_init();

	result = c2_fom_domain_init(&reqh->rh_fom_dom);
	if (result) {
		REQH_ADDB_ADD(c2_reqh_addb_ctx,
				"c2_reqh_init", result);
		return result;
	}

	C2_ASSERT(c2_fom_domain_invariant(&reqh->rh_fom_dom));
	reqh->rh_rpc = rpc;
	reqh->rh_dtm = dtm;
	reqh->rh_stdom = stdom;
	reqh->rh_fol = fol;
	reqh->rh_serv = serv;
	reqh->rh_fom_dom.fd_reqh = reqh;

	return result;
}

void c2_reqh_fini(struct c2_reqh *reqh)
{
	C2_PRE(reqh != NULL);
	c2_fom_domain_fini(&reqh->rh_fom_dom);
	c2_addb_ctx_fini(&c2_reqh_addb_ctx);
	reqh_fop_fini();
}

void c2_reqh_fop_handle(struct c2_reqh *reqh, struct c2_fop *fop, void *cookie)
{
	struct c2_fom	       *fom;
	int			result;
	size_t			iloc;

	C2_PRE(reqh != NULL);
	C2_PRE(fop != NULL);

	result = fop->f_type->ft_ops->fto_fom_init(fop, &fom);
	if (result != 0) {
		REQH_ADDB_ADD(c2_reqh_addb_ctx, "c2_reqh_fop_handle", result);
		return;
	}

	fom->fo_cookie = cookie;
	fom->fo_fol = reqh->rh_fol;
	fom->fo_stdomain = reqh->rh_stdom;
	fom->fo_domain = &reqh->rh_fom_dom;

	iloc = fom->fo_ops->fo_home_locality(fom);
	C2_ASSERT(iloc >= 0 && iloc <= fom->fo_domain->fd_localities_nr);
	fom->fo_loc = &reqh->rh_fom_dom.fd_localities[iloc];
	C2_ASSERT(c2_fom_invariant(fom));
	C2_ASSERT(c2_locality_invariant(fom->fo_loc));

	c2_fom_queue(fom);
}

void c2_reqh_fop_sortkey_get(struct c2_reqh *reqh, struct c2_fop *fop,
				struct c2_fop_sortkey *key)
{
}

/**
 * Begins fom execution.
 *
 * @param fom, fom under execution
 *
 * @pre fom->fo_phase == FOPH_INIT
 *
 * @see c2_fom_state_generic()
 */
static int fom_phase_init(struct c2_fom *fom)
{
	C2_PRE(fom->fo_phase == FOPH_INIT);

	return FSO_AGAIN;
}

/**
 * Performs authenticity checks on fop,
 * executed by the fom.
 *
 * @pre fom->fo_phase = FOPH_AUTHENTICATE
 */
static int fom_authen(struct c2_fom *fom)
{
	C2_PRE(fom->fo_phase == FOPH_AUTHENTICATE);

	return FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation
 * in FOPH_AUTHENTICATE phase.
 *
 * @pre fom->fo_phase == FOPH_AUTHENTICATE_WAIT
 */
static int fom_authen_wait(struct c2_fom *fom)
{
	C2_PRE(fom->fo_phase == FOPH_AUTHENTICATE_WAIT);

	return FSO_AGAIN;
}

/**
 * Identifies local resources required for fom
 * execution.
 *
 * @pre fom->fo_phase == FOPH_RESOURCE_LOCAL
 */
static int fom_loc_resource(struct c2_fom *fom)
{
	C2_PRE(fom->fo_phase == FOPH_RESOURCE_LOCAL);

	return FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation
 * in FOPH_RESOURCE_LOCAL phase.
 *
 * @pre fom->fo_phase == FOPH_RESOURCE_LOCAL_WAIT
 */
static int fom_loc_resource_wait(struct c2_fom *fom)
{
	C2_PRE(fom->fo_phase == FOPH_RESOURCE_LOCAL_WAIT);

	return FSO_AGAIN;
}

/**
 * Identifies distributed resources required for fom execution.
 *
 * @pre fom->fo_phase == FOPH_RESOURCE_DISTRIBUTED
 */
static int fom_dist_resource(struct c2_fom *fom)
{
	C2_PRE(fom->fo_phase == FOPH_RESOURCE_DISTRIBUTED);

	return FSO_AGAIN;
}

/**
 * Resumes fom execution after completing blocking operation
 * in FOPH_RESOURCE_DISTRIBUTED_PHASE.
 *
 * @pre fom->fo_phase == FOPH_RESOURCE_DISTRIBUTED_WAIT
 */
static int fom_dist_resource_wait(struct c2_fom *fom)
{
	C2_PRE(fom->fo_phase == FOPH_RESOURCE_DISTRIBUTED_WAIT);

	return FSO_AGAIN;
}

/**
 * Locates and loads filesystem objects affected by
 * fop executed by this fom.
 *
 * @pre fom->fo_phase == FOPH_OBJECT_CHECK
 */
static int fom_obj_check(struct c2_fom *fom)
{
	C2_PRE(fom->fo_phase == FOPH_OBJECT_CHECK);

	return FSO_AGAIN;
}

/**
 * Resumes fom execution after completing blocking operation
 * in FOPH_OBJECT_CHECK.
 *
 * @pre fom->fo_phase == FOPH_OBJECT_CHECK_WAIT
 */
static int fom_obj_check_wait(struct c2_fom *fom)
{
	C2_PRE(fom->fo_phase == FOPH_OBJECT_CHECK_WAIT);

	return FSO_AGAIN;
}

/**
 * Performs authorisation checks on behalf of the user,
 * accessing the file system objects affected by
 * the fop.
 *
 * @pre fom->fo_phase == FOPH_AUTHORISATION
 */
static int fom_auth(struct c2_fom *fom)
{
	C2_PRE(fom->fo_phase == FOPH_AUTHORISATION);

	return FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation
 * FOPH_AUTHORISATION phase.
 *
 * @pre fom->fo_phase == FOPH_AUTHORISATION_WAIT
 */
static int fom_auth_wait(struct c2_fom *fom)
{
	C2_PRE(fom->fo_phase == FOPH_AUTHORISATION_WAIT);

	return FSO_AGAIN;
}

/**
 * Creates fom local transactional context, the fom operations
 * are executed in this context.
 * If fom execution is completed successfully, the transaction is commited,
 * else it is aborted.
 *
 * @pre fom->fo_phase == FOPH_TXN_CONTEXT
 */
static int create_loc_ctx(struct c2_fom *fom)
{
	int rc;

	C2_PRE(fom->fo_phase == FOPH_TXN_CONTEXT);

	rc = fom->fo_stdomain->sd_ops->sdo_tx_make(fom->fo_stdomain,
							&fom->fo_tx);
	if (rc != 0)
		fom->fo_phase = FOPH_FAILED;

	return FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation,
 * FOPH_TXN_CONTEXT phase.
 *
 * @pre fom->fo_phase == FOPH_TXN_CONTEXT_WAIT
 */
static int create_loc_ctx_wait(struct c2_fom *fom)
{
	C2_PRE(fom->fo_phase == FOPH_TXN_CONTEXT_WAIT);

	return FSO_AGAIN;
}

/**
 * Handles fom execution failure, if fom fails in one of
 * the standard phases, then we contruct a generic error
 * reply fop and assign it to c2_fom::fo_rep_fop, else if
 * fom fails in fop specific operation, then fom should
 * already contain a fop specific error reply provided by
 * fop specific operation.
 *
 * @pre fom->fo_phase == FOPH_FAILED
 */
static int fom_failed(struct c2_fom *fom)
{
	struct c2_fop			*rfop;
	struct c2_reqh_error_rep	*out_fop;

	C2_PRE(fom->fo_phase == FOPH_FAILED);

	if (fom->fo_rep_fop == NULL) {
		rfop = c2_fop_alloc(&c2_reqh_error_rep_fopt, NULL);
		if (rfop == NULL)
			return -ENOMEM;
		out_fop = c2_fop_data(rfop);
		out_fop->rerr_rc = fom->fo_rc;
		fom->fo_rep_fop = rfop;
	}

	return FSO_AGAIN;
}

/**
 * Fom execution is successfull.
 *
 * @pre fom->fo_phase == FOPH_SUCCESS
 */
static int fom_success(struct c2_fom *fom)
{
	C2_PRE(fom->fo_phase == FOPH_SUCCESS);

	return FSO_AGAIN;
}

/**
 * Commits local fom transactional context if fom
 * execution is successful.
 *
 * @pre fom->fo_phase == FOPH_TXN_COMMIT
 */
static int fom_txn_commit(struct c2_fom *fom)
{
	int rc;

	C2_PRE(fom->fo_phase == FOPH_TXN_COMMIT);

	rc = c2_db_tx_commit(&fom->fo_tx.tx_dbtx);

	if (rc != 0)
		fom->fo_phase = FOPH_FAILED;

	return FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation
 * in FOPH_TXN_COMMIT phase.
 *
 * @pre fom->fo_phase == FOPH_TXN_COMMIT_WAIT
 */
static int fom_txn_commit_wait(struct c2_fom *fom)
{
	C2_PRE(fom->fo_phase == FOPH_TXN_COMMIT_WAIT);

	return FSO_AGAIN;
}

/**
 * Checks if transaction context is valid.
 * In case of failed, we abort the transaction, thus,
 *
 * @retval bool -> return true, if transaction is initialised
 *		return false, if transaction is uninitialised
 */
static bool is_tx_initialised(const struct c2_db_tx *tx)
{
	return tx->dt_env != 0;
}

/**
 * Aborts db transaction, if fom execution failed.
 * If fom executions fails before even the transaction
 * is initialised, we don't need to abort any transaction,
 * so we first check if the local transactional context
 * was created.
 *
 * @pre fom->fo_phase == FOPH_TXN_ABORT
 */
static int fom_txn_abort(struct c2_fom *fom)
{
	int rc;

	C2_PRE(fom->fo_phase == FOPH_TXN_ABORT);

	if (is_tx_initialised(&fom->fo_tx.tx_dbtx)) {
		rc = c2_db_tx_abort(&fom->fo_tx.tx_dbtx);
		if (rc != 0)
			fom->fo_phase = FOPH_FAILED;
	}

	return FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation
 * in FOPH_TXN_ABORT phase.
 *
 * @pre fom->fo_phase == FOPH_TXN_ABORT_WAIT
 */
static int fom_txn_abort_wait(struct c2_fom *fom)
{
	C2_PRE(fom->fo_phase == FOPH_TXN_ABORT_WAIT);

	return FSO_AGAIN;
}

/**
 * Posts reply fop, if the execution was done locally,
 * reply fop is cached until the changes are integrated
 * with the server.
 *
 * @pre fom->fo_phase == FOPH_QUEUE_REPLY
 * @pre fom->fo_rep_fop != NULL
 *
 * @todo Implement write back cache, during which we may
 *	perform updations on local objects and re integrate
 *	with the server later, in that case we may block while,
	we caching fop, this requires more additions to the routine.
 */
static int fom_queue_reply(struct c2_fom *fom)
{
	C2_PRE(fom->fo_phase == FOPH_QUEUE_REPLY);
	C2_PRE(fom->fo_rep_fop != NULL);


	c2_net_reply_post(fom->fo_domain->fd_reqh->rh_serv,
				fom->fo_rep_fop, fom->fo_cookie);
	return FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation
 * in FOPH_QUEUE_REPLY phase.
 *
 * @pre fom->fo_phase == FOPH_QUEUE_REPLY_WAIT
 */
static int fom_queue_reply_wait(struct c2_fom *fom)
{
	C2_PRE(fom->fo_phase == FOPH_QUEUE_REPLY_WAIT);

	return FSO_AGAIN;
}

static int fom_timeout(struct c2_fom *fom)
{
	return FSO_AGAIN;
}

/**
 * Fom phase operations table, this defines a fom_phase_ops object
 * for every generic phase of the fom, containing a function pointer
 * to the phase handler, the next phase fom should transition into
 * and a phase name in user visible format.
 *
 * @see struct fom_phase_ops
 */
static const struct fom_phase_ops fpo_table[] = {
	{ .fpo_action = fom_phase_init,
	  .fpo_nextphase = FOPH_AUTHENTICATE,
	  .fpo_name = "fom_init" },

	{ .fpo_action = fom_authen,
	  .fpo_nextphase = FOPH_RESOURCE_LOCAL,
	  .fpo_name = "fom_authen" },

	{ .fpo_action = fom_authen_wait,
	  .fpo_nextphase = FOPH_RESOURCE_LOCAL,
	  .fpo_name = "fom_authen_wait" },

	{ .fpo_action = fom_loc_resource,
	  .fpo_nextphase = FOPH_RESOURCE_DISTRIBUTED,
	  .fpo_name = "fom_loc_resource" },

	{ .fpo_action = fom_loc_resource_wait,
	  .fpo_nextphase = FOPH_RESOURCE_DISTRIBUTED,
	  .fpo_name = "fom_loc_resource_wait" },

	{ .fpo_action = fom_dist_resource,
	  .fpo_nextphase = FOPH_OBJECT_CHECK,
	  .fpo_name = "fom_dist_resource" },

	{ .fpo_action = fom_dist_resource_wait,
	  .fpo_nextphase = FOPH_OBJECT_CHECK,
	  .fpo_name = "fom_dist_resource_wait" },

	{ .fpo_action = fom_obj_check,
	  .fpo_nextphase = FOPH_AUTHORISATION,
	  .fpo_name = "fom_obj_check" },

	{ .fpo_action = fom_obj_check_wait,
	  .fpo_nextphase = FOPH_AUTHORISATION,
	  .fpo_name = "fom_obj_check_wait" },

	{ .fpo_action = fom_auth,
	  .fpo_nextphase = FOPH_TXN_CONTEXT,
	  .fpo_name = "fom_auth" },

	{ .fpo_action = fom_auth_wait,
	  .fpo_nextphase = FOPH_TXN_CONTEXT,
	  .fpo_name = "fom_auth_wait" },

	{ .fpo_action = create_loc_ctx,
	  .fpo_nextphase = FOPH_NR+1,
	  .fpo_name = "create_loc_ctx" },

	{ .fpo_action = create_loc_ctx_wait,
	  .fpo_nextphase = FOPH_NR+1,
	  .fpo_name = "create_loc_ctx_wait" },

	{ .fpo_action = fom_success,
	  .fpo_nextphase = FOPH_TXN_COMMIT,
	  .fpo_name = "fom_success" },

	{ .fpo_action = fom_txn_commit,
	  .fpo_nextphase = FOPH_QUEUE_REPLY,
	  .fpo_name = "fom_txn_commit" },

	{ .fpo_action = fom_txn_commit_wait,
	  .fpo_nextphase = FOPH_QUEUE_REPLY,
	  .fpo_name = "fom_txn_commit_wait" },

	{ .fpo_action = fom_timeout,
	  .fpo_nextphase = FOPH_FAILED,
	  .fpo_name = "fom_timeout" },

	{ .fpo_action = fom_failed,
	  .fpo_nextphase = FOPH_TXN_ABORT,
	  .fpo_name = "fom_failed" },

	{ .fpo_action = fom_txn_abort,
	  .fpo_nextphase = FOPH_QUEUE_REPLY,
	  .fpo_name = "fom_txn_abort" },

	{ .fpo_action = fom_txn_abort_wait,
	  .fpo_nextphase = FOPH_QUEUE_REPLY,
	  .fpo_name = "fom_txn_abort_wait" },

	{ .fpo_action = fom_queue_reply,
	  .fpo_nextphase = FOPH_DONE,
	  .fpo_name = "fom_queue_reply" },

	{ .fpo_action = fom_queue_reply_wait,
	  .fpo_nextphase = FOPH_DONE,
	  .fpo_name = "fom_queue_reply_wait" }};

int c2_fom_state_generic(struct c2_fom *fom)
{
	int rc;

	C2_ASSERT(c2_fom_invariant(fom) == true);

	rc = fp_table[fom->fo_phase].fpo_action(fom);

	if (rc == FSO_AGAIN) {
		if (fom->fo_rc != 0 && fom->fo_phase < FOPH_FAILED) {
			fom->fo_phase = FOPH_FAILED;
			REQH_ADDB_ADD(c2_reqh_addb_ctx,
					fp_table[fom->fo_phase].fpo_name, fom->fo_rc);
		} else
			fom->fo_phase = fp_table[fom->fo_phase].fpo_nextphase;
	}

	if (fom->fo_phase == FOPH_DONE)
		rc = FSO_WAIT;

	return rc;
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
