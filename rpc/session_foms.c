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
 * Original author: Rohan Puri <Rohan_Puri@xyratex.com>,
 *                  Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 04/15/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fop/fop.h"
#include "rpc/session_foms.h"
#include "rpc/session_fops.h"
#include "stob/stob.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "net/net.h"

#ifdef __KERNEL__
#include "rpc/session_k.h"
#define printf printk
#else
#include "rpc/session_u.h"
#endif

#include "fop/fop_format_def.h"
#include "rpc/session_internal.h"

/**
   @addtogroup rpc_session

   @{
 */

const struct c2_fom_ops c2_rpc_fom_conn_establish_ops = {
	.fo_fini = c2_rpc_fom_conn_establish_fini,
	.fo_state = c2_rpc_fom_conn_establish_state
};

static struct c2_fom_type_ops c2_rpc_fom_conn_establish_type_ops = {
	.fto_create = NULL
};

struct c2_fom_type c2_rpc_fom_conn_establish_type = {
	.ft_ops = &c2_rpc_fom_conn_establish_type_ops
};

int c2_rpc_fom_conn_establish_state(struct c2_fom *fom)
{
	struct c2_rpc_fop_conn_establish_rep *reply;
	struct c2_rpc_fop_conn_establish_ctx *ctx;
	struct c2_rpc_fop_conn_establish     *request;
	struct c2_rpc_fom_conn_establish     *fom_ce;
	struct c2_fop                        *fop;
	struct c2_fop                        *fop_rep;
	struct c2_rpc_item                   *item;
	struct c2_rpc_session                *session0;
	struct c2_rpc_conn                   *conn;
	struct c2_rpc_slot                   *slot;
	int                                   rc;

	C2_PRE(fom != NULL);

	fom_ce = container_of(fom, struct c2_rpc_fom_conn_establish, fce_gen);
	C2_ASSERT(fom_ce->fce_fop != NULL && fom_ce->fce_fop_rep != NULL);

	/* Request fop */
	fop = fom_ce->fce_fop;
	request = c2_fop_data(fop);
	C2_ASSERT(request != NULL);

	/* reply fop */
	fop_rep = fom_ce->fce_fop_rep;
	reply = c2_fop_data(fop_rep);
	C2_ASSERT(reply != NULL);

	/* request item */
	item = &fop->f_item;

	/*
	 * On receiver side CONN_ESTABLISH fop is wrapped in
	 * c2_rpc_fop_conn_etablish_ctx object.
	 * See conn_establish_item_decode()
	 */
	ctx = container_of(fop, struct c2_rpc_fop_conn_establish_ctx, cec_fop);
	C2_ASSERT(ctx != NULL && ctx->cec_sender_ep != NULL &&
				 ctx->cec_rpcmachine != NULL);

	C2_ALLOC_PTR(conn);
	if (conn == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	rc = c2_rpc_rcv_conn_init(conn, ctx->cec_sender_ep,
				  ctx->cec_rpcmachine,
				  &item->ri_slot_refs[0].sr_uuid);
	if (rc != 0)
		goto out_free;

	rc = c2_rpc_rcv_conn_establish(conn);
	if (rc != 0) {
		C2_ASSERT(conn->c_state == C2_RPC_CONN_FAILED);
		goto out_fini;
	}

	/*
	 * As CONN_ESTABLISH request is directly submitted for execution.
	 * Add the item explicitly to the slot0. This makes the slot
	 * symmetric to corresponding sender side slot.
	 */
	c2_mutex_lock(&conn->c_mutex);
	session0 = c2_rpc_conn_session0(conn);
	c2_mutex_unlock(&conn->c_mutex);

	item->ri_session = session0;
	slot = session0->s_slot_table[0];
	C2_ASSERT(slot != NULL);

	c2_mutex_lock(&slot->sl_mutex);
	c2_mutex_lock(&session0->s_mutex);

	c2_rpc_slot_item_add_internal(slot, item);

	c2_mutex_unlock(&session0->s_mutex);
	c2_mutex_unlock(&slot->sl_mutex);

	/*
	 * IMPORTANT
	 * Following line is required. Request item has SENDER_ID_INVALID.
	 * slot_item_add_internal() overwrites it with conn->c_sender_id.
	 * But we want reply to have sender_id SENDER_ID_INVALID.
	 * c2_rpc_reply_post() simply copies sender id from req item to
	 * reply item as it is. So set sender id of request item
	 * to SENDER_ID_INVALID
	 */
	item->ri_slot_refs[0].sr_sender_id = SENDER_ID_INVALID;

	C2_ASSERT(conn->c_state == C2_RPC_CONN_ACTIVE);
	reply->rcer_sender_id = conn->c_sender_id;
	reply->rcer_rc = 0;      /* successful */
	fom->fo_phase = FOPH_SUCCESS;

	printf("ce_state: conn establish successful %lu\n",
			(unsigned long)conn->c_sender_id);

	c2_rpc_reply_post(&fop->f_item, &fop_rep->f_item);
	return FSO_AGAIN;

out_fini:
	C2_ASSERT(conn != NULL && rc != 0);
	c2_rpc_conn_fini(conn);

out_free:
	C2_ASSERT(conn != NULL);
	c2_free(conn);

out:
	c2_fop_fini(&ctx->cec_fop); /* CONN_ESTABLISH fop */
	c2_free(ctx);

	/*
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
	fom->fo_phase = FOPH_FAILURE;
	return FSO_AGAIN;
}

void c2_rpc_fom_conn_establish_fini(struct c2_fom *fom)
{
}

/*
 * FOM session create
 */

const struct c2_fom_ops c2_rpc_fom_session_establish_ops = {
	.fo_fini = c2_rpc_fom_session_establish_fini,
	.fo_state = c2_rpc_fom_session_establish_state
};

static struct c2_fom_type_ops c2_rpc_fom_session_establish_type_ops = {
	.fto_create = NULL
};

struct c2_fom_type c2_rpc_fom_session_establish_type = {
	.ft_ops = &c2_rpc_fom_session_establish_type_ops
};

int c2_rpc_fom_session_establish_state(struct c2_fom *fom)
{
	struct c2_rpc_fop_session_establish_rep *reply;
	struct c2_rpc_fop_session_establish     *request;
	struct c2_rpc_fom_session_establish     *fom_se;
	struct c2_rpc_item                      *item;
	struct c2_fop                           *fop;
	struct c2_fop                           *fop_rep;
	struct c2_rpc_session                   *session;
	struct c2_rpc_conn                      *conn;
	uint32_t                                 slot_cnt;
	int                                      rc;

	fom_se = container_of(fom, struct c2_rpc_fom_session_establish,
				fse_gen);

	C2_PRE(fom != NULL && fom_se != NULL && fom_se->fse_fop != NULL &&
			fom_se->fse_fop_rep != NULL);

	fop = fom_se->fse_fop;
	request = c2_fop_data(fop);
	C2_ASSERT(request != NULL);

	fop_rep = fom_se->fse_fop_rep;
	reply = c2_fop_data(fop_rep);
	C2_ASSERT(reply != NULL);

	reply->rser_sender_id = request->rse_sender_id;
	slot_cnt = request->rse_slot_cnt;

	if (slot_cnt == 0) { /* There should be some upper limit to slot_cnt */
		rc = -EINVAL;
		goto errout;
	}

	C2_ALLOC_PTR(session);
	if (session == NULL) {
		rc = -ENOMEM;
		goto errout;
	}

	item = &fop->f_item;
	C2_ASSERT(item->ri_session != NULL);

	conn = item->ri_session->s_conn;
	C2_ASSERT(conn != NULL);

	rc = c2_rpc_session_init(session, conn, slot_cnt);
	if (rc != 0)
		goto out_free;

	rc = c2_rpc_rcv_session_establish(session);
	if (rc != 0)
		goto out_fini;

	C2_ASSERT(session->s_state == C2_RPC_SESSION_IDLE);

	reply->rser_rc = 0;    /* success */
	reply->rser_session_id = session->s_session_id;
	c2_rpc_reply_post(&fop->f_item, &fop_rep->f_item);
	fom->fo_phase = FOPH_SUCCESS;
	printf("session_establish_state:success %lu\n", (unsigned long)session->s_session_id);
	return FSO_AGAIN;

out_fini:
	C2_ASSERT(session != NULL &&
		  c2_rpc_session_invariant(session) &&
		  session->s_state == C2_RPC_SESSION_FAILED);
	c2_rpc_session_fini(session);

out_free:
	C2_ASSERT(session != NULL);
	c2_free(session);
	session = NULL;

errout:
	C2_ASSERT(rc != 0);

	printf("session_establish: failed %d\n", rc);
	reply->rser_rc = rc;
	reply->rser_session_id = SESSION_ID_INVALID;
	fom->fo_phase = FOPH_FAILURE;
	c2_rpc_reply_post(&fop->f_item, &fop_rep->f_item);
	return FSO_AGAIN;
}

void c2_rpc_fom_session_establish_fini(struct c2_fom *fom)
{
}

/*
 * FOM session terminate
 */

const struct c2_fom_ops c2_rpc_fom_session_terminate_ops = {
	.fo_fini = c2_rpc_fom_session_terminate_fini,
	.fo_state = c2_rpc_fom_session_terminate_state
};

static struct c2_fom_type_ops c2_rpc_fom_session_terminate_type_ops = {
	.fto_create = NULL
};

struct c2_fom_type c2_rpc_fom_session_terminate_type = {
	.ft_ops = &c2_rpc_fom_session_terminate_type_ops
};

int c2_rpc_fom_session_terminate_state(struct c2_fom *fom)
{
	struct c2_rpc_fop_session_terminate_rep *reply;
	struct c2_rpc_fop_session_terminate     *request;
	struct c2_rpc_fom_session_terminate     *fom_st;
	struct c2_rpc_item                      *item;
	struct c2_rpc_session                   *session;
	struct c2_rpc_conn                      *conn;
	uint64_t                                 session_id;
	int                                      rc;

	printf("session_terminate_state: called\n");
	fom_st = container_of(fom, struct c2_rpc_fom_session_terminate,
				fst_gen);

	C2_ASSERT(fom != NULL && fom_st != NULL && fom_st->fst_fop != NULL &&
			fom_st->fst_fop_rep != NULL);

	request = c2_fop_data(fom_st->fst_fop);
	C2_ASSERT(request != NULL);

	reply = c2_fop_data(fom_st->fst_fop_rep);
	C2_ASSERT(reply != NULL);

	/*
	 * Copy the same sender and session id to reply fop
	 */
	reply->rstr_sender_id = request->rst_sender_id;
	reply->rstr_session_id = session_id = request->rst_session_id;

	item = &fom_st->fst_fop->f_item;
	C2_ASSERT(item->ri_session != NULL);

	conn = item->ri_session->s_conn;
	C2_ASSERT(conn != NULL);

	c2_mutex_lock(&conn->c_mutex);
	C2_ASSERT(c2_rpc_conn_invariant(conn));
	C2_ASSERT(conn->c_state == C2_RPC_CONN_ACTIVE);

	session = c2_rpc_session_search(conn, session_id);
	if (session == NULL) {
		rc = -ENOENT;
		c2_mutex_unlock(&conn->c_mutex);
		goto errout;
	}
	c2_mutex_unlock(&conn->c_mutex);

	rc = c2_rpc_rcv_session_terminate(session);
	if (rc == -EPROTO) {
		/*
		 * session could not be terminated because session is not in
		 * IDLE state.
		 */
		goto errout;
	}
	C2_ASSERT(ergo(rc != 0, session->s_state == C2_RPC_SESSION_FAILED));
	C2_ASSERT(ergo(rc == 0, session->s_state == C2_RPC_SESSION_TERMINATED));

	c2_rpc_session_fini(session);
	c2_free(session);
	/* fall through */
errout:
	reply->rstr_rc = rc;
	fom->fo_phase = (rc == 0) ? FOPH_SUCCESS: FOPH_FAILURE;
	/*
	 * Note: request is received on SESSION_0, which is different from
	 * current session being terminated. Reply will also go on SESSION_0.
	 */
	c2_rpc_reply_post(&fom_st->fst_fop->f_item,
			  &fom_st->fst_fop_rep->f_item);
	return FSO_AGAIN;
}

void c2_rpc_fom_session_terminate_fini(struct c2_fom *fom)
{
}

/*
 * FOM RPC connection terminate
 */
const struct c2_fom_ops c2_rpc_fom_conn_terminate_ops = {
	.fo_fini = c2_rpc_fom_conn_terminate_fini,
	.fo_state = c2_rpc_fom_conn_terminate_state
};

static struct c2_fom_type_ops c2_rpc_fom_conn_terminate_type_ops = {
	.fto_create = NULL
};

struct c2_fom_type c2_rpc_fom_conn_terminate_type = {
	.ft_ops = &c2_rpc_fom_conn_terminate_type_ops
};

int c2_rpc_fom_conn_terminate_state(struct c2_fom *fom)
{
	struct c2_rpc_fop_conn_terminate_rep *reply;
	struct c2_rpc_fop_conn_terminate     *request;
	struct c2_rpc_fom_conn_terminate     *fom_ct;
	struct c2_rpc_item                   *item;
	struct c2_fop                        *fop;
	struct c2_fop                        *fop_rep;
	struct c2_rpc_conn                   *conn;
	int                                   rc;

	C2_ASSERT(fom != NULL);
	fom_ct = container_of(fom, struct c2_rpc_fom_conn_terminate, fct_gen);

	C2_ASSERT(fom_ct != NULL && fom_ct->fct_fop != NULL &&
			fom_ct->fct_fop_rep != NULL);

	fop = fom_ct->fct_fop;
	request = c2_fop_data(fop);
	C2_ASSERT(request != NULL);

	fop_rep = fom_ct->fct_fop_rep;
	reply = c2_fop_data(fop_rep);
	C2_ASSERT(reply != NULL);

	reply->ctr_sender_id = request->ct_sender_id;

	item = &fop->f_item;
	C2_ASSERT(item->ri_session != NULL);

	conn = item->ri_session->s_conn;
	C2_ASSERT(conn != NULL);
	printf("Received conn terminate req for %lu\n", (unsigned long)request->ct_sender_id);
	rc = c2_rpc_rcv_conn_terminate(conn);
	if (conn->c_state != C2_RPC_CONN_FAILED) {
		C2_ASSERT(conn->c_state == C2_RPC_CONN_ACTIVE ||
			  conn->c_state == C2_RPC_CONN_TERMINATING);
		/*
		 * In memory state of conn is not cleaned up, at this point.
		 * conn will be finalised and freed in the ->rio_sent()
		 * callback of &fop_rep->f_item item.
		 */
		reply->ctr_rc = rc; /* rc can be -EBUSY */
		fom->fo_phase = FOPH_SUCCESS;
		c2_rpc_reply_post(&fop->f_item, &fop_rep->f_item);
		return FSO_AGAIN;
	} else {
		/*
		 * conn has been moved to FAILED state. fini() and free() it.
		 * Cannot send reply back to sender. Sender will time-out and
		 * set sender side conn to FAILED state.
		 * XXX generate ADDB record here.
		 */
		c2_rpc_conn_fini(conn);
		c2_free(conn);
		fom->fo_phase = FOPH_FAILURE;
		return FSO_AGAIN;
	}
}

void c2_rpc_fom_conn_terminate_fini(struct c2_fom *fom)
{
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

