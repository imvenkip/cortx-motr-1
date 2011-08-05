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
 *		    Amit Jambure <Amit_Jambure@xyratex.com>
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
	struct c2_rpc_fop_conn_establish_rep *fop_cer;
	struct c2_rpc_fop_conn_establish     *fop_ce;
	struct c2_rpc_fom_conn_establish     *fom_ce;
	struct c2_fop                        *fop;
	struct c2_fop                        *fop_rep;
	struct c2_rpc_item                   *item;
	struct c2_rpc_session                *session0;
	struct c2_rpc_conn                   *conn;
	struct c2_rpc_slot                   *slot;
	int                                   rc;

	fom_ce = container_of(fom, struct c2_rpc_fom_conn_establish, fce_gen);

	C2_PRE(fom != NULL && fom_ce != NULL && fom_ce->fce_fop != NULL &&
			fom_ce->fce_fop_rep != NULL);

	/* Request fop */
	fop = fom_ce->fce_fop;
	fop_ce = c2_fop_data(fop);
	C2_ASSERT(fop_ce != NULL);

	/* reply fop */
	fop_rep = fom_ce->fce_fop_rep;
	fop_cer = c2_fop_data(fop_rep);
	C2_ASSERT(fop_cer != NULL);

	item = &fop->f_item;
	C2_ASSERT(item->ri_mach != NULL);

	C2_ALLOC_PTR(conn);
	if (conn == NULL) {
		rc = -ENOMEM;
		goto errout;
	}
	rc = c2_rpc_rcv_conn_init(conn, item->ri_mach,
				  &item->ri_slot_refs[0].sr_uuid);
	if (rc != 0)
		goto errout;

	rc = c2_rpc_rcv_conn_establish(conn, item->ri_src_ep);
	if (rc != 0)
		goto errout;

	/*
	 * As CONN_ESTABLISH request is directly submitted for execution
	 * add the item explicitly to the slot0. This makes the slot
	 * symmetric to sender side slot.
	 */
	c2_rpc_session_search(conn, SESSION_ID_0, &session0);
	C2_ASSERT(session0 != NULL);
	item->ri_session = session0;
	slot = session0->s_slot_table[0];
	C2_ASSERT(slot != NULL);
	c2_mutex_lock(&slot->sl_mutex);
	c2_rpc_slot_item_add_internal(slot, item);
	c2_mutex_unlock(&slot->sl_mutex);

	/*
	 * This is required. Request item has SENDER_ID_INVALID.
	 * slot_item_add_internal() overwrites it with conn->c_sender_id.
	 * But we want reply to have sender_id SENDER_ID_INVALID.
	 * c2_rpc_reply_post() simply copies sender id from req item to
	 * reply item as it is. So set sender id of request item
	 * to SENDER_ID_INVALID
	 */
	item->ri_slot_refs[0].sr_sender_id = SENDER_ID_INVALID;

	C2_ASSERT(conn->c_state == C2_RPC_CONN_ACTIVE);
	fop_cer->rcer_snd_id = conn->c_sender_id;
	printf("Received conn sender id = %lu\n", conn->c_sender_id);
	fop_cer->rcer_rc = 0;		/* successful */
	fop_cer->rcer_cookie = fop_ce->rce_cookie;
	fom->fo_phase = FOPH_DONE;
	c2_rpc_reply_post(&fop->f_item, &fop_rep->f_item);
	return FSO_AGAIN;

errout:
	C2_ASSERT(rc != 0);

	printf("conn_establish_state: failed %d\n", rc);
	fop_cer->rcer_snd_id = SENDER_ID_INVALID;
	fop_cer->rcer_rc = rc;
	fop_cer->rcer_cookie = fop_ce->rce_cookie;

	fom->fo_phase = FOPH_FAILED;
	c2_rpc_reply_post(&fop->f_item, &fop_rep->f_item);
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
	struct c2_rpc_fop_session_establish_rep *fop_out;
	struct c2_rpc_fop_session_establish     *fop_in;
	struct c2_rpc_fom_session_establish     *fom_se;
	struct c2_rpc_item                      *item;
	struct c2_fop                           *fop;
	struct c2_fop                           *fop_rep;
	struct c2_rpc_session                   *session;
	struct c2_rpc_conn                      *conn;
	uint64_t                                 sender_id;
	uint32_t                                 slot_cnt;
	int                                      rc;

	fom_se = container_of(fom, struct c2_rpc_fom_session_establish, fse_gen);

	C2_PRE(fom != NULL && fom_se != NULL && fom_se->fse_fop != NULL &&
			fom_se->fse_fop_rep != NULL);

	fop = fom_se->fse_fop;
	fop_in = c2_fop_data(fop);
	C2_ASSERT(fop_in != NULL);

	fop_rep = fom_se->fse_fop_rep;
	fop_out = c2_fop_data(fop_rep);
	C2_ASSERT(fop_out != NULL);

	sender_id = fop_in->rse_snd_id;
	slot_cnt = fop_in->rse_slot_cnt;
	fop_out->rser_sender_id = sender_id;
	printf("session_establish_state: sender_id %lu slot_cnt %u\n", sender_id,
				slot_cnt);

	item = &fop->f_item;
	C2_ASSERT(item->ri_mach != NULL &&
		  item->ri_session != NULL);

	conn = item->ri_session->s_conn;
	C2_ASSERT(conn != NULL && conn->c_state == C2_RPC_CONN_ACTIVE &&
			conn->c_sender_id == sender_id &&
			slot_cnt > 0 &&
			c2_rpc_conn_invariant(conn));

	C2_ALLOC_PTR(session);
	if (session == NULL) {
		printf("scs: failed to allocate session\n");
		rc = -ENOMEM;
		goto errout;
	}

	rc = c2_rpc_session_init(session, conn, slot_cnt);
	if (rc != 0) {
		printf("scs: failed to init session %d\n", rc);
		goto errout;
	}
	rc = c2_rpc_rcv_session_establish(session);
	if (rc != 0) {
		printf("scs: failed to create session: %d\n", rc);
		goto errout;
	}

	C2_ASSERT(session->s_state == C2_RPC_SESSION_IDLE &&
			c2_rpc_session_invariant(session));

	fop_out->rser_rc = 0;		/* success */
	fop_out->rser_session_id = session->s_session_id;
	c2_rpc_reply_post(&fop->f_item, &fop_rep->f_item);
	fom->fo_phase = FOPH_DONE;
	printf("session_establish_state:success %lu\n", session->s_session_id);
	return FSO_AGAIN;

errout:
	C2_ASSERT(rc != 0);

	printf("session_establish: failed %d\n", rc);
	fop_out->rser_rc = rc;
	fop_out->rser_session_id = SESSION_ID_INVALID;
	C2_ASSERT(c2_rpc_session_invariant(session));
	fom->fo_phase = FOPH_FAILED;
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
	struct c2_rpc_fop_session_terminate_rep *fop_out;
	struct c2_rpc_fop_session_terminate     *fop_in;
	struct c2_rpc_fom_session_terminate     *fom_st;
	struct c2_rpc_item                      *item;
	struct c2_rpc_session                   *session;
	struct c2_rpc_conn                      *conn;
	uint64_t                                 sender_id;
	uint64_t                                 session_id;
	int                                      rc;

	printf("session_terminate_state: called\n");
	fom_st = container_of(fom, struct c2_rpc_fom_session_terminate,
				fst_gen);

	C2_ASSERT(fom != NULL && fom_st != NULL && fom_st->fst_fop != NULL &&
			fom_st->fst_fop_rep != NULL);

	fop_in = c2_fop_data(fom_st->fst_fop);
	C2_ASSERT(fop_in != NULL);

	fop_out = c2_fop_data(fom_st->fst_fop_rep);
	C2_ASSERT(fop_out != NULL);

	/*
	 * Copy the same sender and session id to reply fop
	 */
	fop_out->rstr_sender_id = sender_id = fop_in->rst_sender_id;
	fop_out->rstr_session_id = session_id = fop_in->rst_session_id;

	item = &fom_st->fst_fop->f_item;
	C2_ASSERT(item->ri_mach != NULL);

	conn = item->ri_session->s_conn;
	C2_ASSERT(conn != NULL && conn->c_state == C2_RPC_CONN_ACTIVE &&
			conn->c_sender_id == sender_id &&
			c2_rpc_conn_invariant(conn));

	c2_rpc_session_search(conn, session_id, &session);
	if (session == NULL) {
		rc = -ENOENT;
		goto errout;
	}
	rc = c2_rpc_rcv_session_terminate(session);
	if (rc != 0) {
		goto errout;
	}
	c2_rpc_session_fini(session);

	fop_out->rstr_rc = 0;	/* Report success */
	fom->fo_phase = FOPH_DONE;
	c2_rpc_reply_post(&fom_st->fst_fop->f_item,
			  &fom_st->fst_fop_rep->f_item);
	printf("Session terminate successful\n");
	return FSO_AGAIN;

errout:
	C2_ASSERT(rc != 0);

	fop_out->rstr_rc = rc;	/* Report failure */
	fom->fo_phase = FOPH_FAILED;
	c2_rpc_reply_post(&fom_st->fst_fop->f_item,
			  &fom_st->fst_fop_rep->f_item);
	printf("session terminate failed %d\n", rc);
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
	struct c2_rpc_fop_conn_terminate_rep *fop_out;
	struct c2_rpc_fop_conn_terminate     *fop_in;
	struct c2_rpc_fom_conn_terminate     *fom_ct;
	struct c2_rpc_item                   *item;
	struct c2_fop                        *fop;
	struct c2_fop                        *fop_rep;
	struct c2_rpc_conn                   *conn;
	uint64_t                              sender_id;
	int                                   rc;

	C2_ASSERT(fom != NULL);
	fom_ct = container_of(fom, struct c2_rpc_fom_conn_terminate, fct_gen);

	C2_ASSERT(fom_ct != NULL && fom_ct->fct_fop != NULL &&
			fom_ct->fct_fop_rep != NULL);

	fop = fom_ct->fct_fop;
	fop_in = c2_fop_data(fop);
	C2_ASSERT(fop_in != NULL);

	fop_rep = fom_ct->fct_fop_rep;
	fop_out = c2_fop_data(fop_rep);
	C2_ASSERT(fop_out != NULL);

	sender_id = fop_in->ct_sender_id;
	fop_out->ctr_sender_id = sender_id;
	C2_ASSERT(sender_id != SENDER_ID_INVALID &&
		  sender_id != 0);

	item = &fop->f_item;
	C2_ASSERT(item->ri_mach != NULL);

	conn = item->ri_session->s_conn;
	C2_ASSERT(conn != NULL && conn->c_sender_id == sender_id);
	printf("Received conn terminate req for %lu\n", sender_id);
	rc = c2_rpc_rcv_conn_terminate(conn);
	if (rc != 0)
		goto errout;

	printf("Conn terminate successful\n");
	fop_out->ctr_rc = 0;	/* Success */
	c2_rpc_reply_post(&fop->f_item, &fop_rep->f_item);

	fom->fo_phase = FOPH_DONE;
	return FSO_AGAIN;

errout:
	C2_ASSERT(rc != 0);

	printf("Conn terminate failed rc %d\n", rc);
	fop_out->ctr_rc = rc;
	fom->fo_phase = FOPH_FAILED;
	c2_rpc_reply_post(&fop->f_item, &fop_rep->f_item);
	return FSO_AGAIN;
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

