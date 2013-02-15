/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 *		    Madhavrao Vemuri <madhav_vemuri@xyratex.com>
 * Original creation date: 07/19/2011
 */

#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "stob/stob.h"
#include "net/net.h"
#include "fop/fop.h"
#include "dtm/dtm.h"
#include "rpc/rpc.h"
#include "rpc/rpc_opcodes.h"    /* M0_REQH_ERROR_REPLY_OPCODE */
#include "reqh/reqh.h"
#include "fop/fom_generic.h"
#include "fop/fom_generic_xc.h"

/**
   @addtogroup fom
   @{
 */

M0_INTERNAL struct m0_fop_type m0_fom_error_rep_fopt;

M0_INTERNAL void m0_fom_generic_fini(void)
{
	m0_fop_type_fini(&m0_fom_error_rep_fopt);
	m0_xc_fom_generic_fini();
}

M0_INTERNAL int m0_fom_generic_init(void)
{
	m0_xc_fom_generic_init();
	return M0_FOP_TYPE_INIT(&m0_fom_error_rep_fopt,
				.name      = "fom error reply",
				.opcode    = M0_REQH_ERROR_REPLY_OPCODE,
				.xt        = m0_fom_error_rep_xc,
				.rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
}


/**
 * Fom phase descriptor structure, helps to transition fom
 * through its standard phases
 */
struct fom_phase_desc {
        /**
	   Perfoms actions corresponding to a particular standard fom
	   phase, as defined.

	   @retval returns M0_FSO_AGAIN, this transitions fom to its next phase

	   @see m0_fom_tick_generic()
	 */
        int (*fpd_action) (struct m0_fom *fom);
        /**
	   Next phase the fom should transition into, after successfully
	   completing the current phase execution.
	 */
        int		   fpd_nextphase;
	/**
	   Fom phase name in user readable format.
	 */
	const char	  *fpd_name;
	/**
	   Bitmap representation of the fom phase.
	   This is used in pre condition checks before executing
	   fom phase action.

	   @see m0_fom_tick_generic()
	 */
	uint64_t	   fpd_pre_phase;
};

/**
 * Checks if transaction context is valid.
 * We check if m0_db_tx::dt_env is initialised or not.
 *
 * @retval bool -> return true, if transaction is initialised
 *		return false, if transaction is uninitialised
 */
static bool is_tx_initialized(const struct m0_db_tx *tx)
{
	return tx->dt_env != 0;
}

/**
 * Begins fom execution, transitions fom to its first
 * standard phase.
 *
 * @see m0_fom_tick_generic()
 *
 * @retval M0_FSO_AGAIN, to execute next fom phase
 */
static int fom_phase_init(struct m0_fom *fom)
{
	return M0_FSO_AGAIN;
}

/**
 * Performs authenticity checks on fop,
 * executed by the fom.
 */
static int fom_authen(struct m0_fom *fom)
{
	return M0_FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation
 * in M0_FOPH_AUTHENTICATE phase.
 */
static int fom_authen_wait(struct m0_fom *fom)
{
	return M0_FSO_AGAIN;
}

/**
 * Identifies local resources required for fom
 * execution.
 */
static int fom_loc_resource(struct m0_fom *fom)
{
	return M0_FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation
 * in M0_FOPH_RESOURCE_LOCAL phase.
 */
static int fom_loc_resource_wait(struct m0_fom *fom)
{
	return M0_FSO_AGAIN;
}

/**
 * Identifies distributed resources required for fom execution.
 */
static int fom_dist_resource(struct m0_fom *fom)
{
	return M0_FSO_AGAIN;
}

/**
 * Resumes fom execution after completing blocking operation
 * in M0_FOPH_RESOURCE_DISTRIBUTED_PHASE.
 */
static int fom_dist_resource_wait(struct m0_fom *fom)
{
	return M0_FSO_AGAIN;
}

/**
 * Locates and loads filesystem objects affected by
 * fop executed by this fom.
 */
static int fom_obj_check(struct m0_fom *fom)
{
	return M0_FSO_AGAIN;
}

/**
 * Resumes fom execution after completing blocking operation
 * in M0_FOPH_OBJECT_CHECK.
 */
static int fom_obj_check_wait(struct m0_fom *fom)
{
	return M0_FSO_AGAIN;
}

/**
 * Performs authorisation checks on behalf of the user,
 * accessing the file system objects affected by
 * the fop.
 */
static int fom_auth(struct m0_fom *fom)
{
	return M0_FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation
 * M0_FOPH_AUTHORISATION phase.
 */
static int fom_auth_wait(struct m0_fom *fom)
{
	return M0_FSO_AGAIN;
}

/**
 * Creates fom local transactional context, the fom operations
 * are executed in this context.
 * If fom execution is completed successfully, the transaction is commited,
 * else it is aborted.
 */
static int create_loc_ctx(struct m0_fom *fom)
{
	int		rc;
	struct m0_reqh *reqh;

        M0_PRE(!is_tx_initialized(&fom->fo_tx.tx_dbtx));
	reqh = m0_fom_reqh(fom);
	rc = m0_db_tx_init(&fom->fo_tx.tx_dbtx, reqh->rh_dbenv, 0);
	if (rc < 0)
		return rc;

	return M0_FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation,
 * M0_FOPH_TXN_CONTEXT phase.
 */
static int create_loc_ctx_wait(struct m0_fom *fom)
{
	return M0_FSO_AGAIN;
}

/**
 * Allocates generic reqh error reply fop and sets the same
 * into fom->fo_rep_fop.
 */
static int set_gen_err_reply(struct m0_fom *fom)
{
	struct m0_fop           *rfop;
	struct m0_fom_error_rep *out_fop;

	M0_PRE(fom != NULL);

	rfop = m0_fop_alloc(&m0_fom_error_rep_fopt, NULL);
	if (rfop == NULL)
		return -ENOMEM;
	out_fop = m0_fop_data(rfop);
	out_fop->rerr_rc = m0_fom_rc(fom);
	fom->fo_rep_fop = rfop;

	return 0;
}

/**
 * Handles fom execution failure, if fom fails in one of
 * the standard phases, then we contruct a generic error
 * reply fop and assign it to m0_fom::fo_rep_fop, else if
 * fom fails in fop specific operation, then fom should
 * already contain a fop specific error reply provided by
 * fop specific operation.
 */
static int fom_failure(struct m0_fom *fom)
{
	if (m0_fom_rc(fom) != 0 && fom->fo_rep_fop == NULL)
		set_gen_err_reply(fom);

	return M0_FSO_AGAIN;
}

/**
 * Fom execution is successfull.
 */
static int fom_success(struct m0_fom *fom)
{
	return M0_FSO_AGAIN;
}

/**
 * Make a FOL transaction record
 */
static int fom_fol_rec_add(struct m0_fom *fom)
{
	int rc;
	m0_fom_block_enter(fom);
	rc = m0_fop_fol_rec_add(fom->fo_fop, m0_fom_reqh(fom)->rh_fol,
	                        &fom->fo_tx.tx_dbtx);
	m0_fom_block_leave(fom);
	if (rc < 0)
		return rc;

	return M0_FSO_AGAIN;
}

/**
 * Commits local fom transactional context if fom
 * execution is successful.
 */
static int fom_txn_commit(struct m0_fom *fom)
{
	int rc;

	rc = m0_db_tx_commit(&fom->fo_tx.tx_dbtx);
	if (rc < 0) {
		set_gen_err_reply(fom);
		return rc;
	}

	return M0_FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation
 * in M0_FOPH_TXN_COMMIT phase.
 */
static int fom_txn_commit_wait(struct m0_fom *fom)
{
	return M0_FSO_AGAIN;
}

/**
 * Aborts db transaction, if fom execution failed.
 * If fom executions fails before even the transaction
 * is initialised, we don't need to abort any transaction.
 */
static int fom_txn_abort(struct m0_fom *fom)
{
	int rc;

	if (is_tx_initialized(&fom->fo_tx.tx_dbtx)) {
		rc = m0_db_tx_abort(&fom->fo_tx.tx_dbtx);
		if (rc < 0)
			return rc;
	}

	return M0_FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation
 * in M0_FOPH_TXN_ABORT phase.
 */
static int fom_txn_abort_wait(struct m0_fom *fom)
{
	return M0_FSO_AGAIN;
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
static int fom_queue_reply(struct m0_fom *fom)
{
	M0_PRE(fom->fo_rep_fop != NULL);

        m0_rpc_reply_post(&fom->fo_fop->f_item,
			  m0_fop_to_rpc_item(fom->fo_rep_fop));
	return M0_FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation
 * in M0_FOPH_QUEUE_REPLY phase.
 */
static int fom_queue_reply_wait(struct m0_fom *fom)
{
	return M0_FSO_AGAIN;
}

static int fom_timeout(struct m0_fom *fom)
{
	return M0_FSO_AGAIN;
}

/**
 * Fom phase operations table, this defines a fom_phase_desc object
 * for every generic phase of the fom, containing a function pointer
 * to the phase handler, the next phase fom should transition into
 * and a phase name in user visible format inorder to log addb event.
 *
 * @see struct fom_phase_desc
 */
static const struct fom_phase_desc fpd_table[] = {
	[M0_FOPH_INIT] =		   { &fom_phase_init,
					      M0_FOPH_AUTHENTICATE,
					     "fom_init",
					      1 << M0_FOPH_INIT },
	[M0_FOPH_AUTHENTICATE] =	   { &fom_authen,
					      M0_FOPH_RESOURCE_LOCAL,
					     "fom_authen",
				              1 << M0_FOPH_AUTHENTICATE },
	[M0_FOPH_AUTHENTICATE_WAIT] =      { &fom_authen_wait,
				              M0_FOPH_RESOURCE_LOCAL,
					     "fom_authen_wait",
					     1 << M0_FOPH_AUTHENTICATE_WAIT},
	[M0_FOPH_RESOURCE_LOCAL] =	   { &fom_loc_resource,
					      M0_FOPH_RESOURCE_DISTRIBUTED,
					     "fom_loc_resource",
					     1 << M0_FOPH_RESOURCE_LOCAL },
	[M0_FOPH_RESOURCE_LOCAL_WAIT] =	   { &fom_loc_resource_wait,
					      M0_FOPH_RESOURCE_DISTRIBUTED,
					     "fom_loc_resource_wait",
				             1 << M0_FOPH_RESOURCE_LOCAL_WAIT },
	[M0_FOPH_RESOURCE_DISTRIBUTED] =   { &fom_dist_resource,
					      M0_FOPH_OBJECT_CHECK,
					     "fom_dist_resource",
					    1 << M0_FOPH_RESOURCE_DISTRIBUTED },
	[M0_FOPH_RESOURCE_DISTRIBUTED_WAIT] = { &fom_dist_resource_wait,
					      M0_FOPH_OBJECT_CHECK,
					     "fom_dist_resource_wait",
				      1 << M0_FOPH_RESOURCE_DISTRIBUTED_WAIT },
	[M0_FOPH_OBJECT_CHECK] =	   { &fom_obj_check,
					      M0_FOPH_AUTHORISATION,
					     "fom_obj_check",
					      1 << M0_FOPH_OBJECT_CHECK },
	[M0_FOPH_OBJECT_CHECK_WAIT] =	   { &fom_obj_check_wait,
					      M0_FOPH_AUTHORISATION,
					     "fom_obj_check_wait",
					      1 << M0_FOPH_OBJECT_CHECK_WAIT },
	[M0_FOPH_AUTHORISATION] =	   { &fom_auth,
					      M0_FOPH_TXN_CONTEXT,
					     "fom_auth",
					      1 << M0_FOPH_AUTHORISATION },
	[M0_FOPH_AUTHORISATION_WAIT] =	   { &fom_auth_wait,
					      M0_FOPH_TXN_CONTEXT,
					     "fom_auth_wait",
					      1 << M0_FOPH_AUTHORISATION_WAIT },
	[M0_FOPH_TXN_CONTEXT] =		   { &create_loc_ctx,
					      M0_FOPH_TYPE_SPECIFIC,
					     "create_loc_ctx",
					      1 << M0_FOPH_TXN_CONTEXT },
	[M0_FOPH_TXN_CONTEXT_WAIT] =	   { &create_loc_ctx_wait,
					      M0_FOPH_TYPE_SPECIFIC,
					     "create_loc_ctx_wait",
					      1 << M0_FOPH_TXN_CONTEXT_WAIT },
	[M0_FOPH_SUCCESS] =		   { &fom_success,
					      M0_FOPH_FOL_REC_ADD,
					     "fom_success",
					      1 << M0_FOPH_SUCCESS },
	[M0_FOPH_FOL_REC_ADD] =		   { &fom_fol_rec_add,
					      M0_FOPH_TXN_COMMIT,
					     "fom_fol_rec_add",
					      1 << M0_FOPH_FOL_REC_ADD },
	[M0_FOPH_TXN_COMMIT] =		   { &fom_txn_commit,
					      M0_FOPH_QUEUE_REPLY,
					     "fom_txn_commit",
					      1 << M0_FOPH_TXN_COMMIT },
	[M0_FOPH_TXN_COMMIT_WAIT] =	   { &fom_txn_commit_wait,
					      M0_FOPH_QUEUE_REPLY,
					     "fom_txn_commit_wait",
					      1 << M0_FOPH_TXN_COMMIT_WAIT },
	[M0_FOPH_TIMEOUT] =		   { &fom_timeout,
					      M0_FOPH_FAILURE,
					     "fom_timeout",
					      1 << M0_FOPH_TIMEOUT },
	[M0_FOPH_FAILURE] =		   { &fom_failure,
					      M0_FOPH_TXN_ABORT,
					     "fom_failure",
					      1 << M0_FOPH_FAILURE },
	[M0_FOPH_TXN_ABORT] =		   { &fom_txn_abort,
					      M0_FOPH_QUEUE_REPLY,
					     "fom_txn_abort",
					      1 << M0_FOPH_TXN_ABORT },
	[M0_FOPH_TXN_ABORT_WAIT] =	   { &fom_txn_abort_wait,
					      M0_FOPH_QUEUE_REPLY,
					     "fom_txn_abort_wait",
					      1 << M0_FOPH_TXN_ABORT_WAIT },
	[M0_FOPH_QUEUE_REPLY] =		   { &fom_queue_reply,
					      M0_FOPH_FINISH,
					     "fom_queue_reply",
					      1 << M0_FOPH_QUEUE_REPLY },
	[M0_FOPH_QUEUE_REPLY_WAIT] =	   { &fom_queue_reply_wait,
					      M0_FOPH_FINISH,
					     "fom_queue_reply_wait",
					      1 << M0_FOPH_QUEUE_REPLY_WAIT }
};

/**
 * FOM generic phases, allowed transitions from each phase and their functions
 * are assigned to a state machine descriptor.
 * State name is used to log addb event.
 */
static const struct m0_sm_state_descr generic_phases[] = {
	[M0_FOPH_INIT] = {
		.sd_flags     = M0_SDF_INITIAL,
		.sd_name      = "Init",
		.sd_allowed   = M0_BITS(M0_FOPH_AUTHENTICATE, M0_FOPH_FINISH,
					M0_FOPH_SUCCESS, M0_FOPH_FAILURE,
					M0_FOPH_TYPE_SPECIFIC)
	},
	[M0_FOPH_AUTHENTICATE] = {
		.sd_name      = "fom_authen",
		.sd_allowed   = M0_BITS(M0_FOPH_AUTHENTICATE_WAIT,
					M0_FOPH_RESOURCE_LOCAL,
					M0_FOPH_FAILURE)
	},
	[M0_FOPH_AUTHENTICATE_WAIT] = {
		.sd_name      = "fom_authen_wait",
		.sd_allowed   = M0_BITS(M0_FOPH_RESOURCE_LOCAL, M0_FOPH_FAILURE)
	},
	[M0_FOPH_RESOURCE_LOCAL] = {
		.sd_name      = "fom_loc_resource",
		.sd_allowed   = M0_BITS(M0_FOPH_RESOURCE_LOCAL_WAIT,
					M0_FOPH_RESOURCE_DISTRIBUTED,
					M0_FOPH_FAILURE)
	},
	[M0_FOPH_RESOURCE_LOCAL_WAIT] = {
		.sd_name      = "fom_loc_resource_wait",
		.sd_allowed   = M0_BITS(M0_FOPH_RESOURCE_DISTRIBUTED,
					M0_FOPH_FAILURE)
	},
	[M0_FOPH_RESOURCE_DISTRIBUTED] = {
		.sd_name      = "fom_dist_resource",
		.sd_allowed   = M0_BITS(M0_FOPH_RESOURCE_DISTRIBUTED_WAIT,
					M0_FOPH_OBJECT_CHECK,
					M0_FOPH_FAILURE)
	},
	[M0_FOPH_RESOURCE_DISTRIBUTED_WAIT] = {
		.sd_name      = "fom_dist_resource_wait",
		.sd_allowed   = M0_BITS(M0_FOPH_OBJECT_CHECK, M0_FOPH_FAILURE)
	},
	[M0_FOPH_OBJECT_CHECK] = {
		.sd_name      = "fom_obj_check",
		.sd_allowed   = M0_BITS(M0_FOPH_OBJECT_CHECK_WAIT,
					M0_FOPH_AUTHORISATION,
					M0_FOPH_FAILURE)
	},
	[M0_FOPH_OBJECT_CHECK_WAIT] = {
		.sd_name      = "fom_obj_check_wait",
		.sd_allowed   = M0_BITS(M0_FOPH_AUTHORISATION, M0_FOPH_FAILURE)
	},
	[M0_FOPH_AUTHORISATION] = {
		.sd_name      = "fom_auth",
		.sd_allowed   = M0_BITS(M0_FOPH_AUTHORISATION_WAIT,
					M0_FOPH_TXN_CONTEXT,
					M0_FOPH_FAILURE)
	},
	[M0_FOPH_AUTHORISATION_WAIT] = {
		.sd_name      = "fom_auth_wait",
		.sd_allowed   = M0_BITS(M0_FOPH_TXN_CONTEXT, M0_FOPH_FAILURE)
	},
	[M0_FOPH_TXN_CONTEXT] = {
		.sd_name      = "create_loc_ctx",
		.sd_allowed   = M0_BITS(M0_FOPH_TXN_CONTEXT_WAIT,
					M0_FOPH_SUCCESS,
					M0_FOPH_FAILURE,
					M0_FOPH_TYPE_SPECIFIC)
	},
	[M0_FOPH_TXN_CONTEXT_WAIT] = {
		.sd_name      = "create_loc_ctx_wait",
		.sd_allowed   = M0_BITS(M0_FOPH_SUCCESS, M0_FOPH_FAILURE,
					M0_FOPH_TYPE_SPECIFIC)
	},
	[M0_FOPH_SUCCESS] = {
		.sd_name      = "fom_success",
		.sd_allowed   = M0_BITS(M0_FOPH_FOL_REC_ADD)
	},
	[M0_FOPH_FOL_REC_ADD] = {
		.sd_name      = "fom_fol_rec_add",
		.sd_allowed   = M0_BITS(M0_FOPH_TXN_COMMIT)
	},
	[M0_FOPH_TXN_COMMIT] = {
		.sd_name      = "fom_txn_commit",
		.sd_allowed   = M0_BITS(M0_FOPH_TXN_COMMIT_WAIT,
					M0_FOPH_QUEUE_REPLY)
	},
	[M0_FOPH_TXN_COMMIT_WAIT] = {
		.sd_name      = "fom_txn_commit_wait",
		.sd_allowed   = M0_BITS(M0_FOPH_QUEUE_REPLY)
	},
	[M0_FOPH_TIMEOUT] = {
		.sd_name      = "fom_timeout",
		.sd_allowed   = M0_BITS(M0_FOPH_FAILURE)
	},
	[M0_FOPH_FAILURE] = {
		.sd_flags     = M0_SDF_FAILURE,
		.sd_name      = "fom_failure",
		.sd_allowed   = M0_BITS(M0_FOPH_TXN_ABORT)
	},
	[M0_FOPH_TXN_ABORT] = {
		.sd_name      = "fom_txn_abort",
		.sd_allowed   = M0_BITS(M0_FOPH_TXN_ABORT_WAIT,
					M0_FOPH_QUEUE_REPLY)
	},
	[M0_FOPH_TXN_ABORT_WAIT] = {
		.sd_name      = "fom_txn_abort_wait",
		.sd_allowed   = M0_BITS(M0_FOPH_QUEUE_REPLY)
	},
	[M0_FOPH_QUEUE_REPLY] = {
		.sd_name      = "fom_queue_reply",
		.sd_allowed   = M0_BITS(M0_FOPH_QUEUE_REPLY_WAIT,
					M0_FOPH_FINISH)
	},
	[M0_FOPH_QUEUE_REPLY_WAIT] = {
		.sd_name      = "fom_queue_reply_wait",
		.sd_allowed   = M0_BITS(M0_FOPH_FINISH)
	},
	[M0_FOPH_FINISH] = {
		.sd_flags     = M0_SDF_TERMINAL,
		.sd_name      = "SM finish",
	},
	[M0_FOPH_TYPE_SPECIFIC] = {
		.sd_name      = "Specific phase",
		.sd_allowed   = M0_BITS(M0_FOPH_SUCCESS, M0_FOPH_FAILURE,
					M0_FOPH_FINISH)
	}
};

const struct m0_sm_conf m0_generic_conf = {
	.scf_name      = "FOM standard phases",
	.scf_nr_states = ARRAY_SIZE(generic_phases),
	.scf_state     = generic_phases
};
M0_EXPORTED(m0_generic_conf);

int m0_fom_tick_generic(struct m0_fom *fom)
{
	int			     rc;
	const struct fom_phase_desc *fpd_phase;
	struct m0_reqh              *reqh;

	M0_PRE(fom != NULL);

	reqh = m0_fom_reqh(fom);

	fpd_phase = &fpd_table[m0_fom_phase(fom)];

	rc = fpd_phase->fpd_action(fom);
	if (rc < 0) {
		m0_fom_phase_move(fom, rc, M0_FOPH_FAILURE);
		rc = M0_FSO_AGAIN;
	} else if (rc == M0_FSO_AGAIN) {
		m0_fom_phase_set(fom, fpd_phase->fpd_nextphase);
	}

	if (m0_fom_phase(fom) == M0_FOPH_FINISH)
		rc = M0_FSO_WAIT;

	return rc;
}

/** @} end of fom group */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
