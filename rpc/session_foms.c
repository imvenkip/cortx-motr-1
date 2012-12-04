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
 * Original author: Rohan Puri <Rohan_Puri@xyratex.com>,
 *                  Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 04/15/2011
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/trace.h"
#include "stob/stob.h"
#include "net/net.h"

#include "rpc/rpc.h"
#include "rpc/rpc_internal.h"

/**
   @addtogroup rpc_session

   @{

   Definitions of foms that execute conn establish, conn terminate, session
   establish and session terminate fops.
 */

/**
   Common implementation of m0_fom::fo_ops::fo_fini() for conn establish,
   conn terminate, session establish and session terminate foms

   @see session_gen_fom_create
 */
static void session_gen_fom_fini(struct m0_fom *fom)
{
	m0_fom_fini(fom);
	m0_free(fom);
}

/**
   implementation of fop->f_type->ft_fom_type.ft_ops->fto_create for
   conn establish, conn terminate, session establish,
   session terminate fop types.
 */
static int session_gen_fom_create(struct m0_fop *fop, struct m0_fom **m)
{
	const struct m0_fom_ops *fom_ops;
	struct m0_fom           *fom;
	struct m0_fop_type      *reply_fopt;
	struct m0_fop           *reply_fop;
	int                      rc;

	M0_ENTRY("fop: %p", fop);

	M0_ALLOC_PTR(fom);
	if (fom == NULL)
		M0_RETURN(-ENOMEM);

	if (fop->f_type == &m0_rpc_fop_conn_establish_fopt) {

		reply_fopt = &m0_rpc_fop_conn_establish_rep_fopt;
		fom_ops = &m0_rpc_fom_conn_establish_ops;

	} else if (fop->f_type == &m0_rpc_fop_conn_terminate_fopt) {

		reply_fopt = &m0_rpc_fop_conn_terminate_rep_fopt;
		fom_ops = &m0_rpc_fom_conn_terminate_ops;

	} else if (fop->f_type == &m0_rpc_fop_session_establish_fopt) {

		reply_fopt = &m0_rpc_fop_session_establish_rep_fopt;
		fom_ops = &m0_rpc_fom_session_establish_ops;

	} else if (fop->f_type == &m0_rpc_fop_session_terminate_fopt) {

		reply_fopt = &m0_rpc_fop_session_terminate_rep_fopt;
		fom_ops = &m0_rpc_fom_session_terminate_ops;

	} else {
		reply_fopt = NULL;
		fom_ops = NULL;
	}

	if (reply_fopt == NULL || fom_ops == NULL) {
		rc = -EINVAL;
		goto out;
	}

	reply_fop = m0_fop_alloc(reply_fopt, NULL);
	if (reply_fop == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	m0_fom_init(fom, &fop->f_type->ft_fom_type, fom_ops, fop, reply_fop);
	*m = fom;
	rc = 0;

out:
	if (rc != 0) {
		m0_free(fom);
		*m = NULL;
	}

	M0_RETURN(rc);
}

const struct m0_fom_ops m0_rpc_fom_conn_establish_ops = {
	.fo_fini = session_gen_fom_fini,
	.fo_tick = m0_rpc_fom_conn_establish_tick,
	.fo_home_locality = m0_rpc_session_default_home_locality
};

struct m0_fom_type_ops m0_rpc_fom_conn_establish_type_ops = {
	.fto_create = session_gen_fom_create
};

struct m0_fom_type m0_rpc_fom_conn_establish_type = {
	.ft_ops = &m0_rpc_fom_conn_establish_type_ops
};

M0_INTERNAL size_t m0_rpc_session_default_home_locality(const struct m0_fom
							*fom)
{
	M0_PRE(fom != NULL);

	return m0_fop_opcode(fom->fo_fop);
}

M0_INTERNAL int m0_rpc_fom_conn_establish_tick(struct m0_fom *fom)
{
	struct m0_rpc_fop_conn_establish_rep *reply;
	struct m0_rpc_fop_conn_establish_ctx *ctx;
	struct m0_rpc_fop_conn_establish     *request;
	struct m0_fop                        *fop;
	struct m0_fop                        *fop_rep;
	struct m0_rpc_item                   *item;
	struct m0_rpc_machine                *machine;
	struct m0_rpc_session                *session0;
	struct m0_rpc_conn                   *conn;
	struct m0_rpc_slot                   *slot;
	int                                   rc;

	M0_ENTRY("fom: %p", fom);
	M0_PRE(fom != NULL);
	M0_PRE(fom->fo_fop != NULL && fom->fo_rep_fop != NULL);

	fop     = fom->fo_fop;
	request = m0_fop_data(fop);
	M0_ASSERT(request != NULL);

	fop_rep = fom->fo_rep_fop;
	reply   = m0_fop_data(fop_rep);
	M0_ASSERT(reply != NULL);

	item = &fop->f_item;

	/*
	 * On receiver side CONN_ESTABLISH fop is wrapped in
	 * m0_rpc_fop_conn_etablish_ctx object.
	 * See conn_establish_item_decode()
	 */
	ctx = container_of(fop, struct m0_rpc_fop_conn_establish_ctx, cec_fop);
	M0_ASSERT(ctx != NULL &&
		  ctx->cec_sender_ep != NULL &&
		  ctx->cec_rpc_machine != NULL);

	M0_ALLOC_PTR(conn);
	if (conn == NULL){
		M0_RETURN(-ENOMEM);
		/* no reply if conn establish failed.
		   See [4] at end of this function. */
	}

	machine = ctx->cec_rpc_machine;

	m0_rpc_machine_lock(machine);

	rc = m0_rpc_rcv_conn_init(conn, ctx->cec_sender_ep, machine,
				  &item->ri_slot_refs[0].sr_ow.osr_uuid);
	/* we won't need ctx->cec_sender_ep after this point */
	m0_net_end_point_put(ctx->cec_sender_ep);
	if (rc == 0) {
		rc = m0_rpc_rcv_conn_establish(conn);
		if (rc == 0) {
			/* See [1] at the end of function */
			session0         = m0_rpc_conn_session0(conn);
			slot             = session0->s_slot_table[0];
			M0_ASSERT(slot != NULL);

			item->ri_session = session0;
			m0_rpc_slot_item_add_internal(slot, item);

			/* See [2] at the end of function */
			item->ri_slot_refs[0].sr_ow.osr_sender_id =
				SENDER_ID_INVALID;

			M0_ASSERT(conn_state(conn) == M0_RPC_CONN_ACTIVE);
			M0_ASSERT(m0_rpc_conn_invariant(conn));
		} else {
			/* conn establish failed */
			m0_rpc_conn_fini_locked(conn);
		}
	}

	m0_rpc_machine_unlock(machine);

	if (rc == 0) {
		reply->rcer_sender_id = conn->c_sender_id;
		reply->rcer_rc        = 0;

		M0_LOG(M0_INFO, "Conn established: conn [%p] id [%lu]\n", conn,
				(unsigned long)conn->c_sender_id);

		m0_rpc_reply_post(&fop->f_item, &fop_rep->f_item);

	} else {
		M0_ASSERT(conn != NULL);
		m0_free(conn);
		/* No reply is sent if conn establish failed. See [4] */
		m0_fop_fini(&ctx->cec_fop); /* CONN_ESTABLISH fop */
		m0_free(ctx);
		M0_LOG(M0_ERROR, "Conn establish failed: rc [%d]\n", rc);
	}

	m0_fom_phase_set(fom, M0_FOPH_FINISH);
	M0_LEAVE();
	return M0_FSO_WAIT;
}
/*
 * [1]
 * As CONN_ESTABLISH request is directly submitted for execution.
 * Add the item explicitly to the slot0. This makes the slot
 * symmetric to corresponding sender side slot.
 */

/* [2]
 * IMPORTANT
 * @code
 *     item->ri_slot_refs[0].sr_sender_id = SENDER_ID_INVALID;
 * @endcode
 * Request item has SENDER_ID_INVALID.
 * slot_item_add_internal() overwrites it with conn->c_sender_id.
 * But we want reply to have sender_id SENDER_ID_INVALID.
 * m0_rpc_reply_post() simply copies sender id from req item to
 * reply item as it is. So set sender id of request item
 * to SENDER_ID_INVALID
 */

/* [3]
 * CONN_ESTABLISH item is directly submitted for execution. Update
 * rpc-layer stats on INCOMING path here.
 */

/* [4]
 * IMPORTANT: No reply is sent if conn establishing is failed.
 *
 * ACTIVE session is required to send reply. In case of, successful
 * conn establish operation, there is ACTIVE SESSION_0 and slot 0
 * (in the newly established ACTIVE conn) to send reply.
 *
 * But there is no SESSION_0 (in fact here is no conn object) if
 * conn establish operation is failed. Hence reply cannot be sent.
 *
 * In this case, sender will time-out and mark sender side conn
 * as FAILED.
 */

/*
 * FOM session create
 */

const struct m0_fom_ops m0_rpc_fom_session_establish_ops = {
	.fo_fini = session_gen_fom_fini,
	.fo_tick = m0_rpc_fom_session_establish_tick,
	.fo_home_locality = m0_rpc_session_default_home_locality
};

struct m0_fom_type_ops m0_rpc_fom_session_establish_type_ops = {
	.fto_create = session_gen_fom_create
};

struct m0_fom_type m0_rpc_fom_session_establish_type = {
	.ft_ops = &m0_rpc_fom_session_establish_type_ops
};

M0_INTERNAL int m0_rpc_fom_session_establish_tick(struct m0_fom *fom)
{
	struct m0_rpc_fop_session_establish_rep *reply;
	struct m0_rpc_fop_session_establish     *request;
	struct m0_rpc_item                      *item;
	struct m0_fop                           *fop;
	struct m0_fop                           *fop_rep;
	struct m0_rpc_session                   *session;
	struct m0_rpc_conn                      *conn;
	struct m0_rpc_machine                   *machine;
	uint32_t                                 slot_cnt;
	int                                      rc;

	M0_ENTRY("fom: %p", fom);
	M0_PRE(fom != NULL);
	M0_PRE(fom->fo_fop != NULL && fom->fo_rep_fop != NULL);

	fop = fom->fo_fop;
	request = m0_fop_data(fop);
	M0_ASSERT(request != NULL);

	fop_rep = fom->fo_rep_fop;
	reply = m0_fop_data(fop_rep);
	M0_ASSERT(reply != NULL);

	slot_cnt = request->rse_slot_cnt;

	session = NULL;
	if (slot_cnt == 0) { /* There should be some upper limit to slot_cnt */
		rc = -EINVAL;
		goto out;
	}

	M0_ALLOC_PTR(session);
	if (session == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	item = &fop->f_item;
	M0_ASSERT(item->ri_session != NULL);

	conn = item->ri_session->s_conn;
	M0_ASSERT(conn != NULL);

	machine = conn->c_rpc_machine;

	m0_rpc_machine_lock(machine);

	rc = m0_rpc_session_init_locked(session, conn, slot_cnt);
	if (rc == 0) {
		rc = m0_rpc_rcv_session_establish(session);
		if (rc == 0)
			reply->rser_session_id = session->s_session_id;
		else
			m0_rpc_session_fini_locked(session);
	}

	m0_rpc_machine_unlock(machine);

out:
	reply->rser_sender_id = request->rse_sender_id;
	reply->rser_rc        = rc;

	if (rc != 0) {
		reply->rser_session_id = SESSION_ID_INVALID;
		if (session != NULL)
			m0_free(session);
	}

	m0_rpc_reply_post(&fop->f_item, &fop_rep->f_item);
	m0_fom_phase_set(fom, M0_FOPH_FINISH);
	M0_LEAVE();
	return M0_FSO_WAIT;
}

/*
 * FOM session terminate
 */

const struct m0_fom_ops m0_rpc_fom_session_terminate_ops = {
	.fo_fini = session_gen_fom_fini,
	.fo_tick = m0_rpc_fom_session_terminate_tick,
	.fo_home_locality = m0_rpc_session_default_home_locality
};

struct m0_fom_type_ops m0_rpc_fom_session_terminate_type_ops = {
	.fto_create = session_gen_fom_create
};

struct m0_fom_type m0_rpc_fom_session_terminate_type = {
	.ft_ops = &m0_rpc_fom_session_terminate_type_ops
};

M0_INTERNAL int m0_rpc_fom_session_terminate_tick(struct m0_fom *fom)
{
	struct m0_rpc_fop_session_terminate_rep *reply;
	struct m0_rpc_fop_session_terminate     *request;
	struct m0_rpc_item                      *item;
	struct m0_rpc_session                   *session;
	struct m0_rpc_machine                   *machine;
	struct m0_rpc_conn                      *conn;
	uint64_t                                 session_id;
	int                                      rc;

	M0_ENTRY("fom: %p", fom);
	M0_PRE(fom != NULL);
	M0_PRE(fom->fo_fop != NULL && fom->fo_rep_fop != NULL);

	request = m0_fop_data(fom->fo_fop);
	M0_ASSERT(request != NULL);

	reply = m0_fop_data(fom->fo_rep_fop);
	M0_ASSERT(reply != NULL);

	reply->rstr_sender_id = request->rst_sender_id;
	reply->rstr_session_id = session_id = request->rst_session_id;

	item = &fom->fo_fop->f_item;
	M0_ASSERT(item->ri_session != NULL);

	conn = item->ri_session->s_conn;
	M0_ASSERT(conn != NULL);

	machine = conn->c_rpc_machine;

	m0_rpc_machine_lock(machine);

	M0_ASSERT(m0_rpc_conn_invariant(conn));
	M0_ASSERT(conn_state(conn) == M0_RPC_CONN_ACTIVE);

	session = m0_rpc_session_search(conn, session_id);
	if (session != NULL) {
		m0_sm_timedwait(&session->s_sm, M0_BITS(M0_RPC_SESSION_IDLE),
				M0_TIME_NEVER);
		rc = m0_rpc_rcv_session_terminate(session);
		M0_ASSERT(ergo(rc != 0,
			       session_state(session) ==
					M0_RPC_SESSION_FAILED));
		M0_ASSERT(ergo(rc == 0,
			       session_state(session) ==
					M0_RPC_SESSION_TERMINATED));

		m0_rpc_session_fini_locked(session);
		m0_free(session);
	} else { /* session == NULL */
		rc = -ENOENT;
	}

	m0_rpc_machine_unlock(machine);

	reply->rstr_rc = rc;
	M0_LOG(M0_INFO, "Session terminate %s: session [%p] rc [%d]\n",
			(rc == 0) ? "successful" : "failed", session, rc);
	/*
	 * Note: request is received on SESSION_0, which is different from
	 * current session being terminated. Reply will also go on SESSION_0.
	 */
	m0_fom_phase_set(fom, M0_FOPH_FINISH);
	m0_rpc_reply_post(&fom->fo_fop->f_item, &fom->fo_rep_fop->f_item);

	M0_LEAVE();
	return M0_FSO_WAIT;
}

/*
 * FOM RPC connection terminate
 */
const struct m0_fom_ops m0_rpc_fom_conn_terminate_ops = {
	.fo_fini = session_gen_fom_fini,
	.fo_tick = m0_rpc_fom_conn_terminate_tick,
	.fo_home_locality = m0_rpc_session_default_home_locality
};

struct m0_fom_type_ops m0_rpc_fom_conn_terminate_type_ops = {
	.fto_create = session_gen_fom_create
};

struct m0_fom_type m0_rpc_fom_conn_terminate_type = {
	.ft_ops = &m0_rpc_fom_conn_terminate_type_ops
};

M0_INTERNAL int m0_rpc_fom_conn_terminate_tick(struct m0_fom *fom)
{
	struct m0_rpc_fop_conn_terminate_rep *reply;
	struct m0_rpc_fop_conn_terminate     *request;
	struct m0_rpc_item                   *item;
	struct m0_fop                        *fop;
	struct m0_fop                        *fop_rep;
	struct m0_rpc_conn                   *conn;
	struct m0_rpc_machine                *machine;
	int                                   rc;

	M0_ENTRY("fom: %p", fom);
	M0_PRE(fom != NULL);
	M0_PRE(fom->fo_fop != NULL && fom->fo_rep_fop != NULL);

	fop = fom->fo_fop;
	request = m0_fop_data(fop);
	M0_ASSERT(request != NULL);

	fop_rep = fom->fo_rep_fop;
	reply = m0_fop_data(fop_rep);
	M0_ASSERT(reply != NULL);

	reply->ctr_sender_id = request->ct_sender_id;

	item = &fop->f_item;
	M0_ASSERT(item->ri_session != NULL);

	conn = item->ri_session->s_conn;
	M0_ASSERT(conn != NULL);

	machine = conn->c_rpc_machine;

	m0_rpc_machine_lock(machine);

	rc = m0_rpc_rcv_conn_terminate(conn);

	if (conn_state(conn) == M0_RPC_CONN_FAILED) {
		/*
		 * conn has been moved to FAILED state. fini() and free() it.
		 * Cannot send reply back to sender. Sender will time-out and
		 * set sender side conn to FAILED state.
		 * XXX generate ADDB record here.
		 */
		M0_LOG(M0_ERROR, "Conn terminate failed: conn [%p]\n", conn);
		m0_rpc_conn_fini_locked(conn);

		m0_rpc_machine_unlock(machine);

		m0_free(conn);
		m0_fom_phase_set(fom, M0_FOPH_FINISH);
		M0_LEAVE();
		return M0_FSO_WAIT;
	} else {
		M0_ASSERT(M0_IN(conn_state(conn),
				(M0_RPC_CONN_ACTIVE, M0_RPC_CONN_TERMINATING)));

		m0_rpc_machine_unlock(machine);

		/*
		 * In memory state of conn is not cleaned up, at this point.
		 * conn will be finalised and freed in the ->rio_sent()
		 * callback of &fop_rep->f_item item.
		 */
		reply->ctr_rc = rc; /* rc can be -EBUSY */
		m0_fom_phase_set(fom, M0_FOPH_FINISH);
		M0_LOG(M0_INFO, "Conn terminate successful: conn [%p]\n", conn);
		m0_rpc_reply_post(&fop->f_item, &fop_rep->f_item);
		M0_LEAVE();
		return M0_FSO_WAIT;
	}
}

/** @} End of rpc_session group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
