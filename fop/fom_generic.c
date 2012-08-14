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
#include "fop/fom_generic.h"
#include "fop/fop_iterator.h"
#include "dtm/dtm.h"
#include "fop/fop_format_def.h"
#include "sm/sm.h"
#include "reqh/reqh.h"
#include "rpc/rpc2.h"

#ifdef __KERNEL__
#include "fom_generic_fops_k.h"
#else
#include "fom_generic_fops_u.h"
#endif

/**
   @addtogroup fom
   @{
 */

/** FOM generic error fop type. */
extern struct c2_fop_type c2_fom_generic_error_rep_fopt;
extern const struct c2_sm_conf fom_conf;

/**
 * Performs authenticity checks on fop,
 * executed by the fom.
 */
static int fom_authen(struct c2_sm *mach)
{
	return C2_FOPH_RESOURCE_LOCAL;
}

/**
 * Resumes fom execution after completing a blocking operation
 * in C2_FOPH_AUTHENTICATE phase.
 */
static int fom_authen_wait(struct c2_sm *mach)
{
	return C2_FOPH_RESOURCE_LOCAL;
}

/**
 * Identifies local resources required for fom
 * execution.
 */
static int fom_loc_resource(struct c2_sm *mach)
{
	return C2_FOPH_RESOURCE_DISTRIBUTED;
}

/**
 * Resumes fom execution after completing a blocking operation
 * in C2_FOPH_RESOURCE_LOCAL phase.
 */
static int fom_loc_resource_wait(struct c2_sm *mach)
{
	return C2_FOPH_RESOURCE_DISTRIBUTED;
}

/**
 * Identifies distributed resources required for fom execution.
 */
static int fom_dist_resource(struct c2_sm *mach)
{
	return C2_FOPH_OBJECT_CHECK;
}

/**
 * Resumes fom execution after completing blocking operation
 * in C2_FOPH_RESOURCE_DISTRIBUTED_PHASE.
 */
static int fom_dist_resource_wait(struct c2_sm *mach)
{
	return C2_FOPH_OBJECT_CHECK;
}

/**
 * Locates and loads filesystem objects affected by
 * fop executed by this fom.
 */
static int fom_obj_check(struct c2_sm *mach)
{
	return C2_FOPH_AUTHORISATION;
}

/**
 * Resumes fom execution after completing blocking operation
 * in C2_FOPH_OBJECT_CHECK.
 */
static int fom_obj_check_wait(struct c2_sm *mach)
{
	return C2_FOPH_AUTHORISATION;
}

/**
 * Performs authorisation checks on behalf of the user,
 * accessing the file system objects affected by
 * the fop.
 */
static int fom_auth(struct c2_sm *mach)
{
	return C2_FOPH_TXN_CONTEXT;
}

/**
 * Resumes fom execution after completing a blocking operation
 * C2_FOPH_AUTHORISATION phase.
 */
static int fom_auth_wait(struct c2_sm *mach)
{
	return C2_FOPH_TXN_CONTEXT;
}

/**
 * Creates fom local transactional context, the fom operations
 * are executed in this context.
 * If fom execution is completed successfully, the transaction is commited,
 * else it is aborted.
 */
static int create_loc_ctx(struct c2_sm *mach)
{
	int		rc;
	struct c2_reqh *reqh;
	struct c2_fom  *fom = c2_sm2fom(mach);

	reqh = fom->fo_loc->fl_dom->fd_reqh;
	rc = c2_db_tx_init(&fom->fo_tx.tx_dbtx, reqh->rh_dbenv, 0);
	if (rc != 0) {
		mach->sm_rc = rc;
		return C2_FOPH_FAILURE;
	}
	fom->fo_next_phase = C2_FOPH_NR + 1;
	return C2_FSO_AGAIN;
}

/**
 * Resumes fom execution after completing a blocking operation,
 * C2_FOPH_TXN_CONTEXT phase.
 */
static int create_loc_ctx_wait(struct c2_sm *mach)
{
	return C2_FOPH_NR + 1;
}

/**
 * Allocates generic error reply fop and sets the same
 * into fom->fo_rep_fop.
 */
static int set_gen_err_reply(struct c2_fom *fom, int rc)
{
	struct c2_fop			*rfop;
	struct c2_fom_generic_error_rep	*out_fop;

	C2_PRE(fom != NULL);

	rfop = c2_fop_alloc(&c2_fom_generic_error_rep_fopt, NULL);
	if (rfop == NULL)
		return -ENOMEM;
	out_fop = c2_fop_data(rfop);
	out_fop->rerr_rc = rc;
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
static int fom_failure(struct c2_sm *mach)
{
	struct c2_fom *fom = c2_sm2fom(mach);

	C2_PRE(fom != NULL);

	if (mach->sm_rc < 0 && fom->fo_rep_fop == NULL)
		set_gen_err_reply(fom, mach->sm_rc);

	mach->sm_rc = 0;
	return C2_FOPH_TXN_ABORT;
}

/**
 * Fom execution is successful.
 */
static int fom_success(struct c2_sm *mach)
{
	return C2_FOPH_FOL_REC_ADD;
}

/**
 * Make a FOL transaction record
 */
static int fom_fol_rec_add(struct c2_sm *mach)
{
	struct c2_fom *fom = c2_sm2fom(mach);

        c2_fom_block_enter(fom);
#ifndef __KERNEL__
	mach->sm_rc = c2_fop_fol_rec_add(fom->fo_fop, fom->fo_fol,
                                        &fom->fo_tx.tx_dbtx);
	if (mach->sm_rc != 0) {
		c2_fom_block_leave(fom);
		return C2_FOPH_FAILURE;
	}
#endif
        c2_fom_block_leave(fom);
	return C2_FOPH_TXN_COMMIT;
}

/**
 * Commits local fom transactional context if fom
 * execution is successful.
 */
static int fom_txn_commit(struct c2_sm *mach)
{
	struct c2_fom *fom = c2_sm2fom(mach);
	int	       rc;

	rc = c2_db_tx_commit(&fom->fo_tx.tx_dbtx);
	if (rc < 0)
		set_gen_err_reply(fom, rc);

	return C2_FOPH_QUEUE_REPLY;
}

/**
 * Resumes fom execution after completing a blocking operation
 * in C2_FOPH_TXN_COMMIT phase.
 */
static int fom_txn_commit_wait(struct c2_sm *mach)
{
	return C2_FOPH_QUEUE_REPLY;
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
static int fom_txn_abort(struct c2_sm *mach)
{
	int	       rc = 0;
	struct c2_fom *fom = c2_sm2fom(mach);

	if (is_tx_initialised(&fom->fo_tx.tx_dbtx)) {
		rc = c2_db_tx_abort(&fom->fo_tx.tx_dbtx);
		if (rc != 0)
			mach->sm_rc = rc;
	}

	return C2_FOPH_QUEUE_REPLY;
}

/**
 * Resumes fom execution after completing a blocking operation
 * in C2_FOPH_TXN_ABORT phase.
 */
static int fom_txn_abort_wait(struct c2_sm *mach)
{
	return C2_FOPH_QUEUE_REPLY;
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
static int fom_queue_reply(struct c2_sm *mach)
{
	struct c2_rpc_item *item;
	struct c2_fom	   *fom = c2_sm2fom(mach);

	C2_PRE(fom->fo_rep_fop != NULL);

        item = c2_fop_to_rpc_item(fom->fo_rep_fop);
        c2_rpc_reply_post(&fom->fo_fop->f_item, item);

	return C2_FOPH_FINISH;
}

/**
 * Resumes fom execution after completing a blocking operation
 * in C2_FOPH_QUEUE_REPLY phase.
 */
static int fom_queue_reply_wait(struct c2_sm *mach)
{
	struct c2_fom *fom;

	fom = container_of(mach, struct c2_fom, fo_sm_phase);
	return C2_FOPH_FINISH;
}

static int fom_timeout(struct c2_sm *mach)
{
	struct c2_fom *fom;

	fom = container_of(mach, struct c2_fom, fo_sm_phase);
	return C2_FOPH_FAILURE;
}

/**
 * FOM generic phases, allowed transitions from each phase and their functions
 * are assigned to a state machine descriptor.
 * State name is used to log addb event.
 */
const struct c2_sm_state_descr generic_phases[] = {
	[C2_FOPH_INIT] = {
		.sd_flags     = C2_SDF_INITIAL,
		.sd_name      = "SM init",
		.sd_in        = NULL,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_AUTHENTICATE) |
				(1 << C2_FOPH_FINISH) |
				(1 << C2_FOPH_SUCCESS) |
				(1 << C2_FOPH_FAILURE)
	},
	[C2_FOPH_AUTHENTICATE] = {
		.sd_flags     = 0,
		.sd_name      = "fom_authen",
		.sd_in        = &fom_authen,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_AUTHENTICATE_WAIT) |
				(1 << C2_FOPH_RESOURCE_LOCAL) |
				(1 << C2_FOPH_FAILURE)
	},
	[C2_FOPH_AUTHENTICATE_WAIT] = {
		.sd_flags     = 0,
		.sd_name      = "fom_authen_wait",
		.sd_in        = &fom_authen_wait,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_RESOURCE_LOCAL) |
				(1 << C2_FOPH_FAILURE)
	},
	[C2_FOPH_RESOURCE_LOCAL] = {
		.sd_flags     = 0,
		.sd_name      = "fom_loc_resource",
		.sd_in        = &fom_loc_resource,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_RESOURCE_LOCAL_WAIT) |
				(1 << C2_FOPH_RESOURCE_DISTRIBUTED) |
				(1 << C2_FOPH_FAILURE)
	},
	[C2_FOPH_RESOURCE_LOCAL_WAIT] = {
		.sd_flags     = 0,
		.sd_name      = "fom_loc_resource_wait",
		.sd_in        = &fom_loc_resource_wait,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_RESOURCE_DISTRIBUTED) |
				(1 << C2_FOPH_FAILURE)
	},
	[C2_FOPH_RESOURCE_DISTRIBUTED] = {
		.sd_flags     = 0,
		.sd_name      = "fom_dist_resource",
		.sd_in        = &fom_dist_resource,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_RESOURCE_DISTRIBUTED_WAIT) |
				(1 << C2_FOPH_OBJECT_CHECK) |
				(1 << C2_FOPH_FAILURE)
	},
	[C2_FOPH_RESOURCE_DISTRIBUTED_WAIT] = {
		.sd_flags     = 0,
		.sd_name      = "fom_dist_resource_wait",
		.sd_in        = &fom_dist_resource_wait,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_OBJECT_CHECK) |
				(1 << C2_FOPH_FAILURE)
	},
	[C2_FOPH_OBJECT_CHECK] = {
		.sd_flags     = 0,
		.sd_name      = "fom_obj_check",
		.sd_in        = &fom_obj_check,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_OBJECT_CHECK_WAIT) |
				(1 << C2_FOPH_AUTHORISATION) |
				(1 << C2_FOPH_FAILURE)
	},
	[C2_FOPH_OBJECT_CHECK_WAIT] = {
		.sd_flags     = 0,
		.sd_name      = "fom_obj_check_wait",
		.sd_in        = &fom_obj_check_wait,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_AUTHORISATION) |
				(1 << C2_FOPH_FAILURE)
	},
	[C2_FOPH_AUTHORISATION] = {
		.sd_flags     = 0,
		.sd_name      = "fom_auth",
		.sd_in        = &fom_auth,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_AUTHORISATION_WAIT) |
				(1 << C2_FOPH_TXN_CONTEXT) |
				(1 << C2_FOPH_FAILURE)
	},
	[C2_FOPH_AUTHORISATION_WAIT] = {
		.sd_flags     = 0,
		.sd_name      = "fom_auth_wait",
		.sd_in        = &fom_auth_wait,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_TXN_CONTEXT) |
				(1 << C2_FOPH_FAILURE)
	},
	[C2_FOPH_TXN_CONTEXT] = {
		.sd_flags     = 0,
		.sd_name      = "create_loc_ctx",
		.sd_in        = &create_loc_ctx,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_TXN_CONTEXT_WAIT) |
				(1 << C2_FOPH_SUCCESS) |
				(1 << C2_FOPH_FAILURE) |
				(1 << (C2_FOPH_NR + 1))
	},
	[C2_FOPH_TXN_CONTEXT_WAIT] = {
		.sd_flags     = 0,
		.sd_name      = "create_loc_ctx_wait",
		.sd_in        = &create_loc_ctx_wait,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_SUCCESS) |
				(1 << C2_FOPH_FAILURE) |
				(1 << (C2_FOPH_NR + 1))
	},
	[C2_FOPH_SUCCESS] = {
		.sd_flags     = 0,
		.sd_name      = "fom_success",
		.sd_in        = &fom_success,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_FOL_REC_ADD)
	},
	[C2_FOPH_FOL_REC_ADD] = {
		.sd_flags     = 0,
		.sd_name      = "fom_fol_rec_add",
		.sd_in        = &fom_fol_rec_add,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_TXN_COMMIT)
	},
	[C2_FOPH_TXN_COMMIT] = {
		.sd_flags     = 0,
		.sd_name      = "fom_txn_commit",
		.sd_in        = &fom_txn_commit,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_TXN_COMMIT_WAIT) |
				(1 << C2_FOPH_QUEUE_REPLY)
	},
	[C2_FOPH_TXN_COMMIT_WAIT] = {
		.sd_flags     = 0,
		.sd_name      = "fom_txn_commit_wait",
		.sd_in        = &fom_txn_commit_wait,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_QUEUE_REPLY)
	},
	[C2_FOPH_TIMEOUT] = {
		.sd_flags     = 0,
		.sd_name      = "fom_timeout",
		.sd_in        = &fom_timeout,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_FAILURE)
	},
	[C2_FOPH_FAILURE] = {
		.sd_flags     = C2_SDF_FAILURE,
		.sd_name      = "fom_failure",
		.sd_in        = &fom_failure,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_TXN_ABORT)
	},
	[C2_FOPH_TXN_ABORT] = {
		.sd_flags     = 0,
		.sd_name      = "fom_txn_abort",
		.sd_in        = &fom_txn_abort,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_TXN_ABORT_WAIT) |
				(1 << C2_FOPH_QUEUE_REPLY)
	},
	[C2_FOPH_TXN_ABORT_WAIT] = {
		.sd_flags     = 0,
		.sd_name      = "fom_txn_abort_wait",
		.sd_in        = &fom_txn_abort_wait,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_QUEUE_REPLY)
	},
	[C2_FOPH_QUEUE_REPLY] = {
		.sd_flags     = 0,
		.sd_name      = "fom_queue_reply",
		.sd_in        = &fom_queue_reply,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_QUEUE_REPLY_WAIT) |
				(1 << C2_FOPH_FINISH)
	},
	[C2_FOPH_QUEUE_REPLY_WAIT] = {
		.sd_flags     = 0,
		.sd_name      = "fom_queue_reply_wait",
		.sd_in        = &fom_queue_reply_wait,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_FINISH)
	},
	[C2_FOPH_FINISH] = {
		.sd_flags     = C2_SDF_TERMINAL,
		.sd_name      = "SM finish",
		.sd_in        = NULL,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = 0
	},
	[C2_FOPH_NR + 1] = {
		.sd_flags     = 0,
		.sd_name      = "dummy specific phase ",
		.sd_in        = NULL,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOPH_SUCCESS) |
				(1 << C2_FOPH_FAILURE)
	}
};

const struct c2_sm_conf	generic_conf = {
	.scf_name      = "FOM standard phases",
	.scf_nr_states = ARRAY_SIZE(generic_phases),
	.scf_state     = generic_phases
};

int c2_fom_state_transition(struct c2_fom *fom)
{
	int rc;

	C2_PRE(fom != NULL);

	rc = c2_sm_state_set(&fom->fo_sm_phase, fom->fo_next_phase);
	C2_ASSERT(rc == C2_FSO_AGAIN || rc == C2_FSO_WAIT);
	return rc;
}

void c2_fom_sm_init(struct c2_fom *fom)
{
	struct c2_sm_group	*fom_group;
	struct c2_addb_ctx	*fom_addb_ctx;
	const struct c2_sm_conf *conf;

	C2_PRE(fom != NULL);

	conf = &fom->fo_type->ft_conf;
	C2_ASSERT(conf->scf_nr_states != 0);

	fom_group    = &fom->fo_loc->fl_group;
	fom_addb_ctx = &fom->fo_loc->fl_dom->fd_addb_ctx;

	c2_sm_init(&fom->fo_sm_phase, conf, C2_FOPH_INIT, fom_group,
		    fom_addb_ctx);
	c2_sm_init(&fom->fo_sm_state, &fom_conf, C2_FOS_INIT, fom_group,
		    fom_addb_ctx);

	fom->fo_next_phase = C2_FOPH_AUTHENTICATE;
}

void c2_fom_type_register(struct c2_fom_type *fom_type)
{
	int i;

	C2_PRE(fom_type != NULL);

	if (fom_type->ft_phases_nr > C2_FOPH_NR) {
		for (i = 0; i < C2_FOPH_NR; i++)
			fom_type->ft_phases[i] = generic_phases[i];

		C2_ASSERT(fom_type->ft_phases != NULL);
		fom_type->ft_conf.scf_state     = fom_type->ft_phases;
		fom_type->ft_conf.scf_nr_states = fom_type->ft_phases_nr;
	} else
		fom_type->ft_conf = generic_conf;
}

/** @} endgroup fom */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
