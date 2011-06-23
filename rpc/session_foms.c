/* -*- C -*- */

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
#include "rpc/session_int.h"

/**
   @addtogroup rpc_session

   @{
 */

struct c2_fom_ops c2_rpc_fom_conn_create_ops = {
	.fo_fini = &c2_rpc_fom_conn_create_fini,
	.fo_state = &c2_rpc_fom_conn_create_state
};

static struct c2_fom_type_ops c2_rpc_fom_conn_create_type_ops = {
	.fto_create = NULL
};

struct c2_fom_type c2_rpc_fom_conn_create_type = {
	.ft_ops = &c2_rpc_fom_conn_create_type_ops
};

int c2_rpc_fom_conn_create_state(struct c2_fom *fom)
{
	struct c2_fop				*fop;
	struct c2_rpc_fop_conn_create		*fop_cc;
	struct c2_fop				*fop_rep;
	struct c2_rpc_fop_conn_create_rep	*fop_ccr;
	struct c2_rpc_item			*item;
	struct c2_rpc_fom_conn_create		*fom_cc;
	struct c2_rpc_conn			*conn;
	struct c2_rpc_session			*session0;
	uint64_t				sender_id;
	int					rc;

	fom_cc = container_of(fom, struct c2_rpc_fom_conn_create, fcc_gen);

	C2_PRE(fom != NULL && fom_cc != NULL && fom_cc->fcc_fop != NULL &&
			fom_cc->fcc_fop_rep != NULL);

	/* Request fop */
	fop = fom_cc->fcc_fop;
	fop_cc = c2_fop_data(fop);
	C2_ASSERT(fop_cc != NULL);

	/* reply fop */
	fop_rep = fom_cc->fcc_fop_rep;
	fop_ccr = c2_fop_data(fop_rep);
	C2_ASSERT(fop_ccr != NULL);

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL && item->ri_mach != NULL);

	C2_ALLOC_PTR(conn);
	if (conn == NULL) {
		rc = -ENOMEM;
		goto errout;
	}
	rc = c2_rpc_rcv_conn_init(conn, item->ri_mach, &item->ri_uuid);
	if (rc != 0)
		goto errout;

	rc = c2_rpc_rcv_conn_create(conn, item->ri_src_ep);
	if (rc != 0)
		goto errout;

	/*
	 * As CONN_CREATE request is directly submitted for execution
	 * add the item explicitly to the slot0. This makes the slot
	 * symmetric to sender side slot.
	 */
	session_search(conn, SESSION_0, &session0);
	C2_ASSERT(session0 != NULL);
	item->ri_session = session0;
	c2_rpc_slot_item_add_internal(session0->s_slot_table[0], item);
	/*
	 * This is required. Request item has SENDER_ID_INVALID.
	 * slot_item_add_internal() overwrites it with conn->c_sender_id.
	 * But we want reply to have sender_id SENDER_ID_INVALID.
	 * c2_rpc_reply_post() simply copies sender id from req item to
	 * reply item as it is. So set sender id of request item 
	 * to SENDER_ID_INVALID
	 */
	item->ri_sender_id = SENDER_ID_INVALID;

	C2_ASSERT(conn->c_state == C2_RPC_CONN_ACTIVE);
	fop_ccr->rccr_snd_id = conn->c_sender_id;
	fop_ccr->rccr_rc = 0;		/* successful */
	fop_ccr->rccr_cookie = fop_cc->rcc_cookie;

	printf("conn_create_state: conn created %lu\n", sender_id);
	fom->fo_phase = FOPH_DONE;
	c2_rpc_reply_post(c2_fop_to_rpc_item(fop),
			  c2_fop_to_rpc_item(fop_rep));
	return FSO_AGAIN;

errout:
	C2_ASSERT(rc != 0);

	printf("conn_create_state: failed %d\n", rc);
	fop_ccr->rccr_snd_id = SENDER_ID_INVALID;
	fop_ccr->rccr_rc = rc;
	fop_ccr->rccr_cookie = fop_cc->rcc_cookie;

	fom->fo_phase = FOPH_FAILED;
	c2_rpc_reply_post(c2_fop_to_rpc_item(fop),
			  c2_fop_to_rpc_item(fop_rep));
	return FSO_AGAIN;
}
void c2_rpc_fom_conn_create_fini(struct c2_fom *fom)
{
}

/*
 * FOM session create
 */

struct c2_fom_ops c2_rpc_fom_session_create_ops = {
	.fo_fini = &c2_rpc_fom_session_create_fini,
	.fo_state = &c2_rpc_fom_session_create_state
};

static struct c2_fom_type_ops c2_rpc_fom_session_create_type_ops = {
	.fto_create = NULL
};

struct c2_fom_type c2_rpc_fom_session_create_type = {
	.ft_ops = &c2_rpc_fom_session_create_type_ops
};

int c2_rpc_fom_session_create_state(struct c2_fom *fom)
{
	struct c2_fop				*fop;
	struct c2_rpc_fop_session_create	*fop_in;
	struct c2_fop				*fop_rep;
	struct c2_rpc_fop_session_create_rep	*fop_out;
	struct c2_rpc_item			*item;
	struct c2_rpc_fom_session_create	*fom_sc;
	struct c2_rpc_conn			*conn;
	struct c2_rpc_session			*session;
	uint64_t				sender_id;
	int					rc;

	fom_sc = container_of(fom, struct c2_rpc_fom_session_create, fsc_gen);

	C2_PRE(fom != NULL && fom_sc != NULL && fom_sc->fsc_fop != NULL &&
			fom_sc->fsc_fop_rep != NULL);

	fop = fom_sc->fsc_fop;
	fop_in = c2_fop_data(fop);
	C2_ASSERT(fop_in != NULL);

	fop_rep = fom_sc->fsc_fop_rep;
	fop_out = c2_fop_data(fop_rep);
	C2_ASSERT(fop_out != NULL);

	sender_id = fop_in->rsc_snd_id;

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL && item->ri_mach != NULL &&
			item->ri_session != NULL);

	conn = item->ri_session->s_conn;
	C2_ASSERT(conn != NULL && conn->c_state == C2_RPC_CONN_ACTIVE &&
			conn->c_sender_id == sender_id &&
			c2_rpc_conn_invariant(conn));

	C2_ALLOC_PTR(session);
	if (session == NULL) {
		printf("scs: failed to allocate session\n");
		rc = -ENOMEM;
		goto errout;
	}

	rc = c2_rpc_session_init(session, conn, DEFAULT_SLOT_COUNT);
	if (rc != 0) {
		printf("scs: failed to init session %d\n", rc);
		goto errout;
	}
	rc = c2_rpc_rcv_session_create(session);
	if (rc != 0) {
		printf("scs: failed to create session: %d\n", rc);
		c2_mutex_unlock(&conn->c_mutex);
		goto errout;
	}

	C2_ASSERT(session->s_state == C2_RPC_SESSION_IDLE &&
			c2_rpc_session_invariant(session));

	fop_out->rscr_rc = 0; 		/* success */
	fop_out->rscr_session_id = session->s_session_id;
	c2_rpc_reply_post(c2_fop_to_rpc_item(fop),
			  c2_fop_to_rpc_item(fop_rep));
	fom->fo_phase = FOPH_DONE;
	printf("Session create finished %lu\n", session->s_session_id);
	return FSO_AGAIN;

errout:
	C2_ASSERT(rc != 0);

	printf("session_create: failed %d\n", rc);
	fop_out->rscr_rc = rc;
	fop_out->rscr_session_id = SESSION_ID_INVALID;
	C2_ASSERT(c2_rpc_session_invariant(session));
	fom->fo_phase = FOPH_FAILED;
	c2_rpc_reply_post(c2_fop_to_rpc_item(fop),
			  c2_fop_to_rpc_item(fop_rep));
	return FSO_AGAIN;
}
void c2_rpc_fom_session_create_fini(struct c2_fom *fom)
{
}

/*
 * FOM session terminate
 */

struct c2_fom_ops c2_rpc_fom_session_terminate_ops = {
	.fo_fini = &c2_rpc_fom_session_terminate_fini,
	.fo_state = &c2_rpc_fom_session_terminate_state
};

static struct c2_fom_type_ops c2_rpc_fom_session_terminate_type_ops = {
	.fto_create = NULL
};

struct c2_fom_type c2_rpc_fom_session_terminate_type = {
	.ft_ops = &c2_rpc_fom_session_terminate_type_ops
};

int c2_rpc_fom_session_terminate_state(struct c2_fom *fom)
{
	struct c2_rpc_fop_session_terminate	*fop_in;
	struct c2_rpc_fop_session_terminate_rep	*fop_out;
	struct c2_rpc_fom_session_terminate	*fom_st;
	struct c2_rpc_item			*item;
	struct c2_rpc_conn			*conn;
	struct c2_rpc_session			*session;
	uint64_t				sender_id;
	uint64_t				session_id;
	int					rc;

	printf("session_terminate_state: called\n");
	fom_st = container_of(fom, struct c2_rpc_fom_session_terminate, fst_gen);

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

	item = c2_fop_to_rpc_item(fom_st->fst_fop);
	C2_ASSERT(item != NULL && item->ri_mach != NULL);

	conn = item->ri_session->s_conn;
	C2_ASSERT(conn != NULL && conn->c_state == C2_RPC_CONN_ACTIVE &&
			conn->c_sender_id == sender_id &&
			c2_rpc_conn_invariant(conn));

	session_search(conn, session_id, &session);
	if (session == NULL) {
		rc = -ENOENT;
		goto errout;
	}
	c2_mutex_lock(&session->s_mutex);
	rc = c2_rpc_rcv_session_terminate(session);	
	if (rc != 0) {
		c2_mutex_unlock(&session->s_mutex);
		goto errout;
	}
	c2_mutex_unlock(&session->s_mutex);
	c2_rpc_session_fini(session);

	fop_out->rstr_rc = 0;	/* Report success */
	fom->fo_phase = FOPH_DONE;
	c2_rpc_reply_post(c2_fop_to_rpc_item(fom_st->fst_fop),
			  c2_fop_to_rpc_item(fom_st->fst_fop_rep));
	printf("Session terminate successful\n");
	return FSO_AGAIN;

errout:
	C2_ASSERT(rc != 0);

	fop_out->rstr_rc = rc;	/* Report failure */
	fom->fo_phase = FOPH_FAILED;
	c2_rpc_reply_post(c2_fop_to_rpc_item(fom_st->fst_fop),
			  c2_fop_to_rpc_item(fom_st->fst_fop_rep));
	printf("session terminate failed %d\n", rc);
	return FSO_AGAIN;
}

void c2_rpc_fom_session_terminate_fini(struct c2_fom *fom)
{
}

/*
 * FOM RPC connection terminate
 */
struct c2_fom_ops c2_rpc_fom_conn_terminate_ops = {
	.fo_fini = &c2_rpc_fom_conn_terminate_fini,
	.fo_state = &c2_rpc_fom_conn_terminate_state
};

static struct c2_fom_type_ops c2_rpc_fom_conn_terminate_type_ops = {
	.fto_create = NULL
};

struct c2_fom_type c2_rpc_fom_conn_terminate_type = {
	.ft_ops = &c2_rpc_fom_conn_terminate_type_ops
};

int c2_rpc_fom_conn_terminate_state(struct c2_fom *fom)
{
	struct c2_rpc_item			*item;
	struct c2_fop				*fop;
	struct c2_rpc_fop_conn_terminate	*fop_in;
	struct c2_fop				*fop_rep;
	struct c2_rpc_fop_conn_terminate_rep	*fop_out;
	struct c2_rpc_fom_conn_terminate	*fom_ct;
	struct c2_rpc_conn			*conn;
	int					rc;
	uint64_t				sender_id;

	C2_ASSERT(fom != NULL);
	fom_ct = container_of(fom, struct c2_rpc_fom_conn_terminate, fct_gen);

	C2_ASSERT(fom_ct != NULL && fom_ct->fct_fop != NULL &&
			fom_ct->fct_fop_rep != NULL);

	fop = fom_ct->fct_fop;
	fop_in = c2_fop_data(fop);
	C2_ASSERT(fop_in != NULL);

	fop_rep = fom_ct->fct_fop_rep;
	fop_out = c2_fop_data(fop);
	C2_ASSERT(fop_out != NULL);

	sender_id = fop_in->ct_sender_id;
	fop_out->ctr_sender_id = sender_id;
	C2_ASSERT(sender_id != SENDER_ID_INVALID);

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL && item->ri_mach != NULL);

	conn = item->ri_session->s_conn;
	C2_ASSERT(conn != NULL && conn->c_sender_id == sender_id);

	rc = c2_rpc_rcv_conn_terminate(conn);
	if (rc != 0)
		goto errout;

	printf("Conn terminate successful\n");
	fop_out->ctr_rc = 0;	/* Success */
	c2_rpc_reply_post(c2_fop_to_rpc_item(fop),
			  c2_fop_to_rpc_item(fop_rep));
	
	fom->fo_phase = FOPH_DONE;
	//conn_terminate_reply_sent(conn);
	return FSO_AGAIN;

errout:
	C2_ASSERT(rc != 0);

	printf("Conn terminate failed rc %d\n", rc);
	fop_out->ctr_rc = rc;
	fom->fo_phase = FOPH_FAILED;
	c2_rpc_reply_post(c2_fop_to_rpc_item(fop),
			  c2_fop_to_rpc_item(fop_rep));
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

