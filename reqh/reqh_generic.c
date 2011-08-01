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

extern const struct c2_addb_loc c2_reqh_addb_loc;

extern const struct c2_addb_ctx_type c2_reqh_addb_ctx_type;

extern struct c2_addb_ctx c2_reqh_addb_ctx;

#define REQH_GEN_ADDB_ADD(addb_ctx, name, rc)  \
C2_ADDB_ADD(&addb_ctx, &c2_reqh_addb_loc, c2_addb_func_fail, (name), (rc))

/**
 * Reqh generic error fop type.
 */
extern struct c2_fop_type c2_reqh_error_rep_fopt;

/**
 * Fom phase operations structure, helps to transition fom
 * through its standard phases
 */
struct fom_phase_ops {
        /* Phase execution routine */
        int (*fpo_action) (struct c2_fom *fom);
        /* Next phase to transition into */
        int fpo_nextphase;
	/* Phase name */
	const char *fpo_name;
};

/**
 * Begins fom execution, transitions fom to its first
 * standard phase.
 *
 * @pre fom->fo_phase == FOPH_INIT
 *
 * @see c2_fom_state_generic()
 *
 * @retval FSO_AGAIN, to execute next fom phase
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
 * Posts reply fop, if the fom execution was done locally,
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
 * and a phase name in user visible format inorder to log addb event.
 *
 * @see struct fom_phase_ops
 */
static const struct fom_phase_ops fpo_table[] = {
	{ &fom_phase_init, FOPH_AUTHENTICATE, "fom_init" },
	{ &fom_authen, FOPH_RESOURCE_LOCAL, "fom_authen" },
	{ &fom_authen_wait, FOPH_RESOURCE_LOCAL, "fom_authen_wait" },
	{ &fom_loc_resource, FOPH_RESOURCE_DISTRIBUTED, "fom_loc_resource" },
	{ &fom_loc_resource_wait, FOPH_RESOURCE_DISTRIBUTED, "fom_loc_resource_wait" },
	{ &fom_dist_resource, FOPH_OBJECT_CHECK, "fom_dist_resource" },
	{ &fom_dist_resource_wait, FOPH_OBJECT_CHECK, "fom_dist_resource_wait" },
	{ &fom_obj_check, FOPH_AUTHORISATION, "fom_obj_check" },
	{ &fom_obj_check_wait, FOPH_AUTHORISATION, "fom_obj_check_wait" },
	{ &fom_auth, FOPH_TXN_CONTEXT, "fom_auth" },
	{ &fom_auth_wait, FOPH_TXN_CONTEXT, "fom_auth_wait" },
	{ &create_loc_ctx, FOPH_NR+1, "create_loc_ctx" },
	{ &create_loc_ctx_wait, FOPH_NR+1, "create_loc_ctx_wait" },
	{ &fom_success, FOPH_TXN_COMMIT, "fom_success" },
	{ &fom_txn_commit, FOPH_QUEUE_REPLY, "fom_txn_commit" },
	{ &fom_txn_commit_wait, FOPH_QUEUE_REPLY, "fom_txn_commit_wait" },
	{ &fom_timeout, FOPH_FAILED, "fom_timeout" },
	{ &fom_failed, FOPH_TXN_ABORT, "fom_failed" },
	{ &fom_txn_abort, FOPH_QUEUE_REPLY, "fom_txn_abort" },
	{ &fom_txn_abort_wait, FOPH_QUEUE_REPLY, "fom_txn_abort_wait" },
	{ &fom_queue_reply, FOPH_DONE, "fom_queue_reply" },
	{ &fom_queue_reply_wait, FOPH_DONE, "fom_queue_reply_wait" }
};

int c2_fom_state_generic(struct c2_fom *fom)
{
	int rc;

	C2_ASSERT(c2_fom_invariant(fom));

	rc = fpo_table[fom->fo_phase].fpo_action(fom);

	if (rc == FSO_AGAIN) {
		if (fom->fo_rc != 0 && fom->fo_phase < FOPH_FAILED) {
			fom->fo_phase = FOPH_FAILED;
			REQH_GEN_ADDB_ADD(c2_reqh_addb_ctx,
					fpo_table[fom->fo_phase].fpo_name, fom->fo_rc);
		} else
			fom->fo_phase = fpo_table[fom->fo_phase].fpo_nextphase;
	}

	if (fom->fo_phase == FOPH_DONE)
		rc = FSO_WAIT;

	return rc;
}
C2_EXPORTED(c2_fom_state_generic);

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
