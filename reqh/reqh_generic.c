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
 * Original creation date: 07/19/2010
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "stob/stob.h"
#include "net/net.h"
#include "fop/fop.h"
#include "fop/fom.h"
#include "dtm/dtm.h"
#include "reqh_fops_xc.h"
#include "reqh.h"

/**
   @addtogroup reqh
   @{
 */

/**
 * Reqh addb event location identifier object.
 */
static const struct c2_addb_loc reqh_gen_addb_loc = {
        .al_name = "reqh generic"
};

#define REQH_GEN_ADDB_ADD(addb_ctx, name, rc)  \
C2_ADDB_ADD((addb_ctx), &reqh_gen_addb_loc, c2_addb_func_fail, (name), (rc))

/**
 * Reqh generic error fop type.
 */
extern struct c2_fop_type c2_reqh_error_rep_fopt;

/**
 * Fom phase operations structure, helps to transition fom
 * through its standard phases
 */
struct fom_phase_ops {
        /**
	   Perfoms actions corresponding to a particular standard fom
	   phase, as defined.

	   @retval returns C2_FSO_AGAIN, this transitions fom to its next phase

	   @see c2_fom_state_generic()
	 */
        int (*fpo_action) (struct c2_fom *fom);
        /**
	   Next phase the fom should transition into, after successfully
	   completing the current phase execution.
	 */
        int		   fpo_nextphase;
	/**
	   Fom phase name in user readable format.
	 */
	const char	  *fpo_name;
	/**
	   Bitmap representation of the fom phase.
	   This is used in pre condition checks before executing
	   fom phase action.

	   @see c2_fom_state_generic()
	 */
	uint64_t	   fpo_pre_phase;
};

/**
 * Begins fom execution, transitions fom to its first
 * standard phase.
 *
 * @see c2_fom_state_generic()
 *
 * @retval C2_FSO_AGAIN, to execute next fom phase
 */
static int fom_phase_init(struct c2_fom *fom)
{
	return C2_FSO_AGAIN;
}

/**
 * Performs authenticity checks on fop,
 * executed by the fom.
 */
static int fom_authen(struct c2_fom *fom)
{
	return C2_FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation
 * in C2_FOPH_AUTHENTICATE phase.
 */
static int fom_authen_wait(struct c2_fom *fom)
{
	return C2_FSO_AGAIN;
}

/**
 * Identifies local resources required for fom
 * execution.
 */
static int fom_loc_resource(struct c2_fom *fom)
{
	return C2_FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation
 * in C2_FOPH_RESOURCE_LOCAL phase.
 */
static int fom_loc_resource_wait(struct c2_fom *fom)
{
	return C2_FSO_AGAIN;
}

/**
 * Identifies distributed resources required for fom execution.
 */
static int fom_dist_resource(struct c2_fom *fom)
{
	return C2_FSO_AGAIN;
}

/**
 * Resumes fom execution after completing blocking operation
 * in C2_FOPH_RESOURCE_DISTRIBUTED_PHASE.
 */
static int fom_dist_resource_wait(struct c2_fom *fom)
{
	return C2_FSO_AGAIN;
}

/**
 * Locates and loads filesystem objects affected by
 * fop executed by this fom.
 */
static int fom_obj_check(struct c2_fom *fom)
{
	return C2_FSO_AGAIN;
}

/**
 * Resumes fom execution after completing blocking operation
 * in C2_FOPH_OBJECT_CHECK.
 */
static int fom_obj_check_wait(struct c2_fom *fom)
{
	return C2_FSO_AGAIN;
}

/**
 * Performs authorisation checks on behalf of the user,
 * accessing the file system objects affected by
 * the fop.
 */
static int fom_auth(struct c2_fom *fom)
{
	return C2_FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation
 * C2_FOPH_AUTHORISATION phase.
 */
static int fom_auth_wait(struct c2_fom *fom)
{
	return C2_FSO_AGAIN;
}

/**
 * Creates fom local transactional context, the fom operations
 * are executed in this context.
 * If fom execution is completed successfully, the transaction is commited,
 * else it is aborted.
 */
static int create_loc_ctx(struct c2_fom *fom)
{
	int		rc;
	struct c2_reqh *reqh;

	reqh = fom->fo_loc->fl_dom->fd_reqh;
	rc = c2_db_tx_init(&fom->fo_tx.tx_dbtx, reqh->rh_dbenv, 0);
	if (rc != 0)
		fom->fo_rc = rc;

	return C2_FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation,
 * C2_FOPH_TXN_CONTEXT phase.
 */
static int create_loc_ctx_wait(struct c2_fom *fom)
{
	return C2_FSO_AGAIN;
}

/**
 * Allocates generic reqh error reply fop and sets the same
 * into fom->fo_rep_fop.
 */
static int set_gen_err_reply(struct c2_fom *fom)
{
	struct c2_fop			*rfop;
	struct c2_reqh_error_rep	*out_fop;

	C2_PRE(fom != NULL);

	rfop = c2_fop_alloc(&c2_reqh_error_rep_fopt, NULL);
	if (rfop == NULL)
		return -ENOMEM;
	out_fop = c2_fop_data(rfop);
	out_fop->rerr_rc = fom->fo_rc;
	fom->fo_rep_fop = rfop;

	return 0;
}

/**
 * Handles fom execution failure, if fom fails in one of
 * the standard phases, then we contruct a generic error
 * reply fop and assign it to c2_fom::fo_rep_fop, else if
 * fom fails in fop specific operation, then fom should
 * already contain a fop specific error reply provided by
 * fop specific operation.
 */
static int fom_failure(struct c2_fom *fom)
{
	if (fom->fo_rc != 0 && fom->fo_rep_fop == NULL)
		set_gen_err_reply(fom);

	return C2_FSO_AGAIN;
}

/**
 * Fom execution is successfull.
 */
static int fom_success(struct c2_fom *fom)
{
	return C2_FSO_AGAIN;
}

/**
 * Make a FOL transaction record
 */
static int fom_fol_rec_add(struct c2_fom *fom)
{
        c2_fom_block_enter(fom);
        fom->fo_rc = c2_fop_fol_rec_add(fom->fo_fop, fom->fo_fol,
                                        &fom->fo_tx.tx_dbtx);
        c2_fom_block_leave(fom);

	return C2_FSO_AGAIN;
}

/**
 * Commits local fom transactional context if fom
 * execution is successful.
 */
static int fom_txn_commit(struct c2_fom *fom)
{
	int rc;

	rc = c2_db_tx_commit(&fom->fo_tx.tx_dbtx);

	if (rc != 0) {
		fom->fo_rc = rc;
		set_gen_err_reply(fom);
	}

	return C2_FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation
 * in C2_FOPH_TXN_COMMIT phase.
 */
static int fom_txn_commit_wait(struct c2_fom *fom)
{
	return C2_FSO_AGAIN;
}

/**
 * Checks if transaction context is valid.
 * We check if c2_db_tx::dt_env is initialised or not.
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
 * is initialised, we don't need to abort any transaction.
 */
static int fom_txn_abort(struct c2_fom *fom)
{
	int rc;

	if (is_tx_initialised(&fom->fo_tx.tx_dbtx)) {
		rc = c2_db_tx_abort(&fom->fo_tx.tx_dbtx);
		if (rc != 0)
			fom->fo_rc = rc;
	}

	return C2_FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation
 * in C2_FOPH_TXN_ABORT phase.
 */
static int fom_txn_abort_wait(struct c2_fom *fom)
{
	return C2_FSO_AGAIN;
}

/**
 * Posts reply fop, if the fom execution was done locally,
 * reply fop is cached until the changes are integrated
 * with the server.
 *
 * @pre fom->fo_rep_fop != NULL
 *
 * @todo Implement write back cache, during which we may
 *	perform updations on local objects and re integrate
 *	with the server later, in that case we may block while,
	we caching fop, this requires more additions to the routine.
 */
static int fom_queue_reply(struct c2_fom *fom)
{
	struct c2_rpc_item *item;

	C2_PRE(fom->fo_rep_fop != NULL);

        item = c2_fop_to_rpc_item(fom->fo_rep_fop);
        c2_rpc_reply_post(&fom->fo_fop->f_item, item);

	return C2_FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation
 * in C2_FOPH_QUEUE_REPLY phase.
 */
static int fom_queue_reply_wait(struct c2_fom *fom)
{
	return C2_FSO_AGAIN;
}

static int fom_timeout(struct c2_fom *fom)
{
	return C2_FSO_AGAIN;
}

/**
 * Fom phase operations table, this defines a fom_phase_ops object
 * for every generic phase of the fom, containing a function pointer
 * to the phase handler, the next phase fom should transition into
 * and a phase name in user visible format inorder to log addb event.
 *
 * @see struct fom_phase_ops
 */
static const struct fom_phase_ops fpo_table[] = {
	[C2_FOPH_INIT] =		   { &fom_phase_init,
					      C2_FOPH_AUTHENTICATE,
					     "fom_init",
					      1 << C2_FOPH_INIT },
	[C2_FOPH_AUTHENTICATE] =	   { &fom_authen,
					      C2_FOPH_RESOURCE_LOCAL,
					     "fom_authen",
				              1 << C2_FOPH_AUTHENTICATE },
	[C2_FOPH_AUTHENTICATE_WAIT] =      { &fom_authen_wait,
				              C2_FOPH_RESOURCE_LOCAL,
					     "fom_authen_wait",
					     1 << C2_FOPH_AUTHENTICATE_WAIT},
	[C2_FOPH_RESOURCE_LOCAL] =	   { &fom_loc_resource,
					      C2_FOPH_RESOURCE_DISTRIBUTED,
					     "fom_loc_resource",
					     1 << C2_FOPH_RESOURCE_LOCAL },
	[C2_FOPH_RESOURCE_LOCAL_WAIT] =	   { &fom_loc_resource_wait,
					      C2_FOPH_RESOURCE_DISTRIBUTED,
					     "fom_loc_resource_wait",
				             1 << C2_FOPH_RESOURCE_LOCAL_WAIT },
	[C2_FOPH_RESOURCE_DISTRIBUTED] =   { &fom_dist_resource,
					      C2_FOPH_OBJECT_CHECK,
					     "fom_dist_resource",
					    1 << C2_FOPH_RESOURCE_DISTRIBUTED },
	[C2_FOPH_RESOURCE_DISTRIBUTED_WAIT] = { &fom_dist_resource_wait,
					      C2_FOPH_OBJECT_CHECK,
					     "fom_dist_resource_wait",
				      1 << C2_FOPH_RESOURCE_DISTRIBUTED_WAIT },
	[C2_FOPH_OBJECT_CHECK] =	   { &fom_obj_check,
					      C2_FOPH_AUTHORISATION,
					     "fom_obj_check",
					      1 << C2_FOPH_OBJECT_CHECK },
	[C2_FOPH_OBJECT_CHECK_WAIT] =	   { &fom_obj_check_wait,
					      C2_FOPH_AUTHORISATION,
					     "fom_obj_check_wait",
					      1 << C2_FOPH_OBJECT_CHECK_WAIT },
	[C2_FOPH_AUTHORISATION] =	   { &fom_auth,
					      C2_FOPH_TXN_CONTEXT,
					     "fom_auth",
					      1 << C2_FOPH_AUTHORISATION },
	[C2_FOPH_AUTHORISATION_WAIT] =	   { &fom_auth_wait,
					      C2_FOPH_TXN_CONTEXT,
					     "fom_auth_wait",
					      1 << C2_FOPH_AUTHORISATION_WAIT },
	[C2_FOPH_TXN_CONTEXT] =		   { &create_loc_ctx,
					      C2_FOPH_NR+1,
					     "create_loc_ctx",
					      1 << C2_FOPH_TXN_CONTEXT },
	[C2_FOPH_TXN_CONTEXT_WAIT] =	   { &create_loc_ctx_wait,
					      C2_FOPH_NR+1,
					     "create_loc_ctx_wait",
					      1 << C2_FOPH_TXN_CONTEXT_WAIT },
	[C2_FOPH_SUCCESS] =		   { &fom_success,
					      C2_FOPH_FOL_REC_ADD,
					     "fom_success",
					      1 << C2_FOPH_SUCCESS },
	[C2_FOPH_FOL_REC_ADD] =		   { &fom_fol_rec_add,
					      C2_FOPH_TXN_COMMIT,
					     "fom_fol_rec_add",
					      1 << C2_FOPH_FOL_REC_ADD },
	[C2_FOPH_TXN_COMMIT] =		   { &fom_txn_commit,
					      C2_FOPH_QUEUE_REPLY,
					     "fom_txn_commit",
					      1 << C2_FOPH_TXN_COMMIT },
	[C2_FOPH_TXN_COMMIT_WAIT] =	   { &fom_txn_commit_wait,
					      C2_FOPH_QUEUE_REPLY,
					     "fom_txn_commit_wait",
					      1 << C2_FOPH_TXN_COMMIT_WAIT },
	[C2_FOPH_TIMEOUT] =		   { &fom_timeout,
					      C2_FOPH_FAILURE,
					     "fom_timeout",
					      1 << C2_FOPH_TIMEOUT },
	[C2_FOPH_FAILURE] =		   { &fom_failure,
					      C2_FOPH_TXN_ABORT,
					     "fom_failure",
					      1 << C2_FOPH_FAILURE },
	[C2_FOPH_TXN_ABORT] =		   { &fom_txn_abort,
					      C2_FOPH_QUEUE_REPLY,
					     "fom_txn_abort",
					      1 << C2_FOPH_TXN_ABORT },
	[C2_FOPH_TXN_ABORT_WAIT] =	   { &fom_txn_abort_wait,
					      C2_FOPH_QUEUE_REPLY,
					     "fom_txn_abort_wait",
					      1 << C2_FOPH_TXN_ABORT_WAIT },
	[C2_FOPH_QUEUE_REPLY] =		   { &fom_queue_reply,
					      C2_FOPH_FINISH,
					     "fom_queue_reply",
					      1 << C2_FOPH_QUEUE_REPLY },
	[C2_FOPH_QUEUE_REPLY_WAIT] =	   { &fom_queue_reply_wait,
					      C2_FOPH_FINISH,
					     "fom_queue_reply_wait",
					      1 << C2_FOPH_QUEUE_REPLY_WAIT }
};

int c2_fom_state_generic(struct c2_fom *fom)
{
	int			    rc;
	const struct fom_phase_ops *fpo_phase;
	struct c2_reqh             *reqh;

	C2_PRE(IS_IN_ARRAY(fom->fo_phase, fpo_table));

	fpo_phase = &fpo_table[fom->fo_phase];

	C2_ASSERT(fpo_phase->fpo_pre_phase == 0 ||
			(fpo_phase->fpo_pre_phase & (1 << fom->fo_phase)));

	rc = fpo_phase->fpo_action(fom);

	reqh = fom->fo_loc->fl_dom->fd_reqh;
	if (rc == C2_FSO_AGAIN) {
		if (fom->fo_rc != 0 && fom->fo_phase < C2_FOPH_FAILURE) {
			fom->fo_phase = C2_FOPH_FAILURE;
			REQH_GEN_ADDB_ADD(reqh->rh_addb,
						fpo_phase->fpo_name,
						fom->fo_rc);
		} else
			fom->fo_phase = fpo_phase->fpo_nextphase;
	}

	if (fom->fo_phase == C2_FOPH_FINISH)
		rc = C2_FSO_WAIT;

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
