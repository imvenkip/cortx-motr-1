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
#include "lib/finject.h"
#include "stob/stob.h"
#include "net/net.h"
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
static int session_gen_fom_create(struct m0_fop *fop, struct m0_fom **m,
				  struct m0_reqh *reqh)
{
	const struct m0_fom_ops		     *fom_ops;
	struct m0_fom			     *fom;
	struct m0_fop_type		     *reply_fopt;
	struct m0_fop			     *reply_fop;
	int				      rc;

	M0_ENTRY("fop: %p", fop);

	M0_ALLOC_PTR(fom);
	if (fom == NULL) {
		rc = M0_ERR(-ENOMEM);
		goto out;
	}
	if (fop->f_type == &m0_rpc_fop_conn_establish_fopt) {

		reply_fopt = &m0_rpc_fop_conn_establish_rep_fopt;
		fom_ops    = &m0_rpc_fom_conn_establish_ops;

	} else if (fop->f_type == &m0_rpc_fop_conn_terminate_fopt) {

		reply_fopt = &m0_rpc_fop_conn_terminate_rep_fopt;
		fom_ops    = &m0_rpc_fom_conn_terminate_ops;

	} else if (fop->f_type == &m0_rpc_fop_session_establish_fopt) {

		reply_fopt = &m0_rpc_fop_session_establish_rep_fopt;
		fom_ops    = &m0_rpc_fom_session_establish_ops;

	} else if (fop->f_type == &m0_rpc_fop_session_terminate_fopt) {

		reply_fopt = &m0_rpc_fop_session_terminate_rep_fopt;
		fom_ops    = &m0_rpc_fom_session_terminate_ops;

	} else {
		reply_fopt = NULL;
		fom_ops    = NULL;
	}

	if (reply_fopt == NULL || fom_ops == NULL) {
		rc = -EINVAL;
		M0_LOG(M0_ERROR, "unsupported fop type '%s'\n",
				 fop->f_type->ft_name);
		goto out;
	}

	reply_fop = m0_fop_reply_alloc(fop, reply_fopt);
	if (M0_FI_ENABLED("reply_fop_alloc_failed")) {
		m0_fop_put(reply_fop);
		reply_fop = NULL;
	}
	if (reply_fop == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	m0_fom_init(fom, &fop->f_type->ft_fom_type, fom_ops, fop, reply_fop,
		    reqh);
	*m = fom;
	rc = 0;

out:
	if (rc != 0) {
		m0_free(fom);
		*m = NULL;
	}
	return M0_RC(rc);
}

const struct m0_fom_ops m0_rpc_fom_conn_establish_ops = {
	.fo_fini          = session_gen_fom_fini,
	.fo_tick          = m0_rpc_fom_conn_establish_tick,
	.fo_home_locality = m0_rpc_session_default_home_locality
};

struct m0_fom_type_ops m0_rpc_fom_conn_establish_type_ops = {
	.fto_create = session_gen_fom_create
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
	struct m0_rpc_item_header2           *header;
	struct m0_fop                        *fop;
	struct m0_fop                        *fop_rep;
	struct m0_rpc_item                   *item;
	struct m0_rpc_machine                *machine;
	struct m0_rpc_session                *session0;
	struct m0_rpc_conn                   *conn;
	static struct m0_fom_timeout         *fom_timeout = NULL;
	int                                   rc;

	M0_ENTRY("fom: %p", fom);
	M0_PRE(fom != NULL);
	M0_PRE(fom->fo_fop != NULL && fom->fo_rep_fop != NULL);

	if (M0_FI_ENABLED("sleep-for-resend")) {
		M0_ASSERT(fom_timeout == NULL);
		M0_ALLOC_PTR(fom_timeout);
		M0_ASSERT(fom_timeout != NULL);
		m0_fom_timeout_init(fom_timeout);
		rc = m0_fom_timeout_wait_on(fom_timeout, fom,
		  m0_time_from_now(M0_RPC_ITEM_RESEND_INTERVAL * 2, 0));
		M0_ASSERT(rc == 0);
		M0_LEAVE();
		return M0_FSO_WAIT;
	}
	if (M0_FI_ENABLED("free-timer") && fom_timeout != NULL &&
	    fom_timeout->to_cb.fc_fom == fom) {
		/* don't touch not our timer (from resend) */
		m0_fom_timeout_fini(fom_timeout);
		m0_free(fom_timeout);
		m0_fi_disable(__func__, "free-timer");
	}

	fop     = fom->fo_fop;
	request = m0_fop_data(fop);
	M0_ASSERT(request != NULL);

	fop_rep = fom->fo_rep_fop;
	reply   = m0_fop_data(fop_rep);
	M0_ASSERT(reply != NULL);

	item = &fop->f_item;
	header = &item->ri_header;
	/*
	 * On receiver side CONN_ESTABLISH fop is wrapped in
	 * m0_rpc_fop_conn_etablish_ctx object.
	 * See conn_establish_item_decode()
	 */
	ctx = container_of(fop, struct m0_rpc_fop_conn_establish_ctx, cec_fop);
	M0_ASSERT(ctx != NULL &&
		  ctx->cec_sender_ep != NULL);

	M0_ALLOC_PTR(conn);
	if (M0_FI_ENABLED("conn-alloc-failed"))
		m0_free0(&conn);
	if (conn == NULL) {
		goto ret;
		/* no reply if conn establish failed.
		   See [4] at end of this function. */
	}

	machine = item->ri_rmachine;
	m0_rpc_machine_lock(machine);
	if (m0_rpc_machine_find_conn(machine, item) != NULL) {
		/* This is a duplicate request that was accepted
		   after original conn-establish request was accepted but
		   before the conn-establish operation completed.

		   Ignore this item.
		 */
		M0_LOG(M0_INFO, "Duplicate conn-establish request %p", item);
		m0_rpc_machine_unlock(machine);
		m0_free(conn);
		goto ret;
	}
	rc = m0_rpc_rcv_conn_init(conn, ctx->cec_sender_ep, machine,
				  &header->osr_uuid);
	if (rc == 0) {
		session0 = m0_rpc_conn_session0(conn);
		conn->c_sender_id = m0_rpc_id_generate();
		conn_state_set(conn, M0_RPC_CONN_ACTIVE);
		item->ri_session = session0;
		/* freed at m0_rpc_item_process_reply() */
		m0_rpc_session_hold_busy(session0);
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
		M0_LOG(M0_ERROR, "Conn establish failed: rc [%d]\n", rc);
	}

ret:
	m0_fom_phase_set(fom, M0_FOPH_FINISH);
	M0_LEAVE();
	return M0_FSO_WAIT;
}

/*
 * FOM session create
 */

const struct m0_fom_ops m0_rpc_fom_session_establish_ops = {
	.fo_fini          = session_gen_fom_fini,
	.fo_tick          = m0_rpc_fom_session_establish_tick,
	.fo_home_locality = m0_rpc_session_default_home_locality
};

struct m0_fom_type_ops m0_rpc_fom_session_establish_type_ops = {
	.fto_create = session_gen_fom_create
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

	item = &fop->f_item;
	M0_ASSERT(item->ri_session != NULL);
	conn = item->ri_session->s_conn;
	M0_ASSERT(conn != NULL);
	machine = conn->c_rpc_machine;

	M0_ALLOC_PTR(session);
	if (M0_FI_ENABLED("session-alloc-failed"))
		m0_free0(&session);
	if (session == NULL) {
		rc = M0_ERR(-ENOMEM);
		goto out;
	}
	m0_rpc_machine_lock(machine);
	rc = m0_rpc_session_init_locked(session, conn);
	if (rc == 0) {
		do {
			session->s_session_id = m0_rpc_id_generate();
		} while (session->s_session_id <= SESSION_ID_MIN ||
			 session->s_session_id >  SESSION_ID_MAX);
		session_state_set(session, M0_RPC_SESSION_IDLE);
		reply->rser_session_id = session->s_session_id;
	}
	m0_rpc_machine_unlock(machine);

out:
	reply->rser_sender_id  = request->rse_sender_id;
	reply->rser_rc         = rc;
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
	.fo_fini          = session_gen_fom_fini,
	.fo_tick          = m0_rpc_fom_session_terminate_tick,
	.fo_home_locality = m0_rpc_session_default_home_locality
};

struct m0_fom_type_ops m0_rpc_fom_session_terminate_type_ops = {
	.fto_create = session_gen_fom_create
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
	if (conn_state(conn) != M0_RPC_CONN_ACTIVE) {
		rc = -EINVAL;
		session = NULL;
		goto out;
	}

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
out:
	m0_rpc_machine_unlock(machine);

	reply->rstr_rc = rc;
	M0_LOG(M0_DEBUG, "Session terminate %s: session [%p] rc [%d]",
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

M0_INTERNAL int m0_rpc_fom_conn_terminate_tick(struct m0_fom *fom)
{
	struct m0_rpc_fop_conn_terminate_rep *reply;
	struct m0_rpc_fop_conn_terminate     *request;
	struct m0_rpc_item                   *item;
	struct m0_fop                        *fop;
	struct m0_fop                        *fop_rep;
	struct m0_rpc_conn                   *conn;
	struct m0_rpc_machine                *machine;

	M0_ENTRY("fom: %p", fom);
	M0_PRE(fom != NULL);
	M0_PRE(fom->fo_fop != NULL && fom->fo_rep_fop != NULL);

	fop     = fom->fo_fop;
	fop_rep = fom->fo_rep_fop;
	request = m0_fop_data(fop);
	reply   = m0_fop_data(fop_rep);

	item    = &fop->f_item;
	conn    = item->ri_session->s_conn;
	machine = conn->c_rpc_machine;

	m0_rpc_machine_lock(machine);
	reply->ctr_sender_id = request->ct_sender_id;
	reply->ctr_rc        = m0_rpc_rcv_conn_terminate(conn);
	m0_rpc_machine_unlock(machine);

	m0_rpc_reply_post(&fop->f_item, &fop_rep->f_item);

	m0_rpc_machine_lock(machine);
	m0_sm_timedwait(&item->ri_session->s_sm, M0_BITS(M0_RPC_SESSION_IDLE),
			M0_TIME_NEVER);
	m0_rpc_conn_terminate_reply_sent(conn);
	m0_rpc_machine_unlock(machine);

	/*
	 * In memory state of conn is not cleaned up, at this point.
	 * conn will be finalised and freed in the ->rio_sent()
	 * callback of &fop_rep->f_item item.
	 * see: conn_terminate_reply_sent_cb, conn_cleanup_ast()
	 */
	m0_fom_phase_set(fom, M0_FOPH_FINISH);
	M0_LEAVE();
	return M0_FSO_WAIT;
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
