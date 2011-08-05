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
 * Original author: Amit Jambure <Amit_Jambure@xyratex.com>,
 * Original creation date: 05/02/2011
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <rpc/xdr.h>
#include "colibri/init.h"
#include "lib/memory.h"
#include "lib/bitstring.h"
#include "lib/misc.h"
#include "db/db.h"
#include "cob/cob.h"
#include "fop/fop.h"
#include "fop/fop_format_def.h"
#include "net/usunrpc/usunrpc.h"
#include "net/net.h"
#include "rpc/session_u.h"
#include "rpc/session_fops.h"
#include "rpc/session_foms.h"
#include "rpc/session_internal.h"


char db_name[] = "test_db";

struct c2_dbenv			*db;
struct c2_cob_domain		*dom;
struct c2_rpcmachine		*machine;

extern void c2_rpc_session_search(const struct c2_rpc_conn    *conn,
                           uint64_t                     session_id,
                           struct c2_rpc_session        **out);
struct c2_rpc_conn	*connp;
void init()
{
	struct c2_cob_domain_id dom_id = { 42 };
	int				rc;

	C2_ALLOC_PTR(db);
	C2_ALLOC_PTR(dom);
	C2_ALLOC_PTR(machine);
	C2_ASSERT(db != NULL && dom != NULL && machine != NULL);

	rc = c2_dbenv_init(db, db_name, 0);
	C2_ASSERT(rc == 0);

	rc = c2_cob_domain_init(dom, db, &dom_id);
	C2_ASSERT(rc == 0 && dom->cd_dbenv == db);
	printf("dom = %p\n", dom);

	rc = c2_rpcmachine_init(machine, dom, NULL);
	C2_ASSERT(rc == 0);
}
void test_session_terminate(uint64_t sender_id, uint64_t session_id)
{
	struct c2_fop				*fop;
	struct c2_rpc_fop_session_terminate	*fop_in;
	struct c2_fom				*fom;
	struct c2_rpc_item			*item;
	struct c2_rpc_session			*session0;

	/*
	 * Allocate and fill FOP
	 */
	fop = c2_fop_alloc(&c2_rpc_fop_session_terminate_fopt, NULL);
	C2_ASSERT(fop != NULL);

	fop_in = c2_fop_data(fop);
	C2_ASSERT(fop_in != NULL);

	fop_in->rst_sender_id = sender_id;
	fop_in->rst_session_id = session_id;

	/*
	 * Initialize rpc item
	 */
	item = c2_fop_to_rpc_item(fop);
	item->ri_slot_refs[0].sr_sender_id = sender_id;
	item->ri_slot_refs[0].sr_session_id = SESSION_ID_0;
	item->ri_mach = machine;

	c2_rpc_session_search(connp, SESSION_ID_0, &session0);
	C2_ASSERT(session0 != NULL);
	item->ri_session = session0;

	fop->f_type->ft_ops->fto_fom_init(fop, &fom);
	C2_ASSERT(fom != NULL);

	fom->fo_ops->fo_state(fom);

	C2_ASSERT(fom->fo_phase == FOPH_DONE ||
			fom->fo_phase == FOPH_FAILED);
	/*
	 * test reply contents
	 */
	c2_cob_namespace_traverse(dom);
	c2_cob_fb_traverse(dom);
	C2_ASSERT(c2_rpc_conn_invariant(connp));
}

void test_conn_terminate(uint64_t sender_id)
{
	struct c2_fop				*fop;
	struct c2_rpc_fop_conn_terminate	*fop_in;
	struct c2_fom				*fom;
	struct c2_rpc_fom_conn_terminate	*fom_ct;
	struct c2_rpc_item			*item;
	struct c2_rpc_session			*session0;

	/*
	 * Allocate and fill FOP
	 */
	fop = c2_fop_alloc(&c2_rpc_fop_conn_terminate_fopt, NULL);
	C2_ASSERT(fop != NULL);

	fop_in = c2_fop_data(fop);
	C2_ASSERT(fop_in != NULL);

	fop_in->ct_sender_id = sender_id;

	/*
	 * Initialize rpc item
	 */
	item = c2_fop_to_rpc_item(fop);
	item->ri_slot_refs[0].sr_sender_id = SENDER_ID_INVALID;
	item->ri_mach = machine;
	c2_rpc_session_search(connp, SESSION_ID_0, &session0);
	C2_ASSERT(session0 != NULL);
	item->ri_session = session0;

	fop->f_type->ft_ops->fto_fom_init(fop, &fom);
	C2_ASSERT(fom != NULL);
	fom_ct = container_of(fom, struct c2_rpc_fom_conn_terminate,
				fct_gen);
	C2_ASSERT(fom_ct != NULL);
	fom->fo_ops->fo_state(fom);

	C2_ASSERT(fom->fo_phase == FOPH_DONE ||
			fom->fo_phase == FOPH_FAILED);

	if (fom->fo_phase == FOPH_DONE) {
		printf("tct: conn terminate successful\n");
	} else {
		printf("tct: conn terminate failed\n");
	}
	/*
	 * test reply contents
	 */
	c2_cob_namespace_traverse(dom);
	c2_cob_fb_traverse(dom);
}
uint64_t	g_sender_id;
uint64_t	g_session_id;


void test_conn_establish()
{
	struct c2_fop				*fop;
	struct c2_fom				*fom = NULL;
	struct c2_rpc_fom_conn_establish		*fom_ce;
	struct c2_rpc_fop_conn_establish		*fop_ce;
	struct c2_rpc_fop_conn_establish_rep	*fop_reply;
	struct c2_rpc_item			*item;

	/* create conn_establish fop */
	fop = c2_fop_alloc(&c2_rpc_fop_conn_establish_fopt, NULL);
	C2_ASSERT(fop != NULL);

	fop_ce = c2_fop_data(fop);
	C2_ASSERT(fop_ce != NULL);

	fop_ce->rce_cookie = 0xC00CEE;

	item = c2_fop_to_rpc_item(fop);
	item->ri_mach = machine;
	item->ri_src_ep = (void *)0xBADBAD;
	fop->f_type->ft_ops->fto_fom_init(fop, &fom);
	C2_ASSERT(fom != NULL);

	fom->fo_ops->fo_state(fom);
	C2_ASSERT(fom->fo_phase == FOPH_DONE ||
			fom->fo_phase == FOPH_FAILED);

	fom_ce = container_of(fom, struct c2_rpc_fom_conn_establish, fce_gen);
	fop_reply = c2_fop_data(fom_ce->fce_fop_rep);
	C2_ASSERT(fop_reply != NULL);
	printf("test_conn_establish: sender id %lu\n", fop_reply->rcer_snd_id);
	if (fop_reply->rcer_rc == 0) {
		struct c2_list_link	*link;
		C2_ASSERT(c2_list_length(&machine->cr_incoming_conns) == 1);
		link = c2_list_first(&machine->cr_incoming_conns);
		C2_ASSERT(link != NULL);
		connp = container_of(link, struct c2_rpc_conn, c_link);
		printf("conn->sender_id == %lu\n", connp->c_sender_id);
		C2_ASSERT(connp->c_state == C2_RPC_CONN_ACTIVE &&
			  connp->c_sender_id == fop_reply->rcer_snd_id &&
			  c2_rpc_conn_invariant(connp));
	} else {
		printf("TEST: conn create failed %d\n", fop_reply->rcer_rc);
	}
	g_sender_id = fop_reply->rcer_snd_id;
	fom->fo_ops->fo_fini(fom);

	c2_cob_namespace_traverse(dom);
	c2_cob_fb_traverse(dom);
}

void test_session_establish()
{
	struct c2_fop				*fop;
	struct c2_fom				*fom = NULL;
	struct c2_rpc_fom_session_establish	*fom_sc;
	struct c2_rpc_fop_session_establish	*fop_se;
	struct c2_rpc_fop_session_establish_rep	*fop_se_reply;
	struct c2_rpc_item			*item_in;
	struct c2_rpc_session			*session0;

	fop = c2_fop_alloc(&c2_rpc_fop_session_establish_fopt, NULL);
	C2_ASSERT(fop != NULL);

	fop_se = c2_fop_data(fop);
	C2_ASSERT(fop_se != NULL);

	fop_se->rse_snd_id = g_sender_id;
	item_in = c2_fop_to_rpc_item(fop);
/*
	item_in->ri_sender_id = g_sender_id;
	item_in->ri_session_id = SESSION_ID_0;
*/	item_in->ri_mach = machine;
	c2_rpc_session_search(connp, SESSION_ID_0, &session0);
	C2_ASSERT(session0 != NULL);
	item_in->ri_session = session0;

	fop->f_type->ft_ops->fto_fom_init(fop, &fom);
	C2_ASSERT(fom != NULL);

	fom->fo_ops->fo_state(fom);
	C2_ASSERT(fom->fo_phase == FOPH_DONE ||
			fom->fo_phase == FOPH_FAILED);

	fom_sc = container_of(fom, struct c2_rpc_fom_session_establish, fse_gen);
	fop_se_reply = c2_fop_data(fom_sc->fse_fop_rep);
	C2_ASSERT(fop_se_reply != NULL);
	printf("test_session_establish: session id %lu\n", fop_se_reply->rser_session_id);
	g_session_id = fop_se_reply->rser_session_id;
	fom->fo_ops->fo_fini(fom);

	c2_cob_namespace_traverse(dom);
	c2_cob_fb_traverse(dom);
}
#if 0
struct c2_rpc_conn		conn;
struct c2_rpc_session		session;
struct c2_thread		thread;
void conn_status_check(void *arg)
{
	c2_time_t		timeout;
	bool			got_event;

	printf("Thread about to start wait on conn ACTIVE\n");
	c2_time_now(&timeout);
	c2_time_set(&timeout, c2_time_seconds(timeout) + 3,
				c2_time_nanoseconds(timeout));
	got_event = c2_rpc_conn_timedwait(&conn, C2_RPC_CONN_ACTIVE,
			timeout);
	if (got_event && conn.c_state == C2_RPC_CONN_ACTIVE) {
		printf("thread: conn is active %lu\n", conn.c_sender_id);
	} else {
		printf("thread: time out during conn creation\n");
	}
}

void test_snd_conn_establish()
{
	struct c2_fop				*fop;
	struct c2_rpc_fop_conn_establish_rep	*fop_cer;
	struct c2_rpc_item			*item;
	int					rc;

	C2_SET0(&conn);
	c2_rpc_conn_init(&conn, machine);
	printf("testing conn_establish: conn %p\n", &conn);
	rc = c2_rpc_conn_establish(&conn, (void *)0xBADBAD5);
	C2_ASSERT(rc == 0);
	C2_ASSERT(conn.c_state == C2_RPC_CONN_CONNECTING);

	c2_thread_init(&thread, NULL, conn_status_check, NULL,
		       "conn_status_check");

	sleep(1);

	fop = c2_fop_alloc(&c2_rpc_fop_conn_establish_rep_fopt, NULL);
	C2_ASSERT(fop != NULL);

	fop_cer = c2_fop_data(fop);
	C2_ASSERT(fop_cer != NULL);

	fop_cer->rcer_rc = 0;
	fop_cer->rcer_snd_id = 20;
	fop_cer->rcer_cookie = (uint64_t)&conn;

	item = c2_fop_to_rpc_item(fop);
	item->ri_mach = machine;
	item->ri_uuid = conn.c_uuid;
	item->ri_sender_id = SENDER_ID_INVALID;
	fop->f_type->ft_ops->fto_execute(fop, NULL);
	c2_thread_join(&thread);
	c2_thread_fini(&thread);
}
static void thread_entry(void *arg)
{
	c2_time_t		timeout;
	bool			got_event;

	printf("Thread about to start wait on session ALIVE\n");
	c2_time_now(&timeout);
	c2_time_set(&timeout, c2_time_seconds(timeout) + 3,
				c2_time_nanoseconds(timeout));
	got_event = c2_rpc_session_timedwait(&session, C2_RPC_SESSION_IDLE,
			timeout);
	if (got_event && session.s_state == C2_RPC_SESSION_IDLE) {
		printf("thread: session got created %lu IDLE\n", session.s_session_id);
	} else {
		printf("thread: time out during session creation\n");
	}
}
void test_snd_session_establish()
{
	struct c2_fop				*fop;
	struct c2_rpc_fop_session_establish_rep	*fop_ser;
	struct c2_rpc_item			*item;
	struct c2_rpc_session			*session0;
	int					rc;

	C2_SET0(&session);
	rc = c2_rpc_session_init(&session, &conn, DEFAULT_SLOT_COUNT);
	C2_ASSERT(rc == 0);
	rc = c2_rpc_session_establish(&session);
	if (rc != 0) {
		printf("test_sc: failed to create session\n");
		return;
	}
	c2_thread_init(&thread, NULL, thread_entry, NULL, "thread_entry");
	sleep(1);
	fop = c2_fop_alloc(&c2_rpc_fop_session_establish_rep_fopt, NULL);
	C2_ASSERT(fop != NULL);

	fop_ser = c2_fop_data(fop);
	C2_ASSERT(fop_ser != NULL);

	fop_ser->rser_rc = 0;
	fop_ser->rser_sender_id = conn.c_sender_id;
	fop_ser->rser_session_id = 100;

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL);
	item->ri_mach = machine;
	item->ri_sender_id = conn.c_sender_id;
	item->ri_session_id = SESSION_ID_0;
	c2_rpc_session_search(&conn, SESSION_ID_0, &session0);
	C2_ASSERT(session0 != NULL);
	item->ri_session = session0;

	fop->f_type->ft_ops->fto_execute(fop, NULL);

	c2_thread_join(&thread);
	c2_thread_fini(&thread);
}
void test_snd_session_terminate()
{
	struct c2_fop				*fop;
	struct c2_rpc_fop_session_terminate_rep	*fop_str;
	struct c2_rpc_item			*item;
	struct c2_rpc_session			*session0;
	int					rc;

	rc = c2_rpc_session_terminate(&session);
	if (rc != 0) {
		printf("test Session terminate failed\n");
		return;
	}

	fop = c2_fop_alloc(&c2_rpc_fop_session_terminate_rep_fopt, NULL);
	C2_ASSERT(fop != NULL);

	fop_str = c2_fop_data(fop);
	C2_ASSERT(fop_str != NULL);

	fop_str->rstr_rc = 0;
	fop_str->rstr_sender_id = conn.c_sender_id;
	fop_str->rstr_session_id = session.s_session_id;

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL);
	item->ri_mach = machine;
	item->ri_sender_id = conn.c_sender_id;
	item->ri_session_id = SESSION_ID_0;
	c2_rpc_session_search(&conn, SESSION_ID_0, &session0);
	C2_ASSERT(session0 != NULL);
	item->ri_session = session0;

	fop->f_type->ft_ops->fto_execute(fop, NULL);
	printf("snd_session_establish_test: state %d rc %d\n", session.s_state,
			session.s_rc);
	c2_rpc_session_fini(&session);
}
void test_snd_conn_terminate()
{
	struct c2_fop				*fop;
	struct c2_rpc_fop_conn_terminate_rep	*fop_ctr;
	struct c2_rpc_item			*item;
	struct c2_rpc_session			*session0;
	int					rc;

	rc = c2_rpc_conn_terminate(&conn);
	if (rc != 0) {
		printf("test: conn terminate failed with %d\n", rc);
		return;
	}

	fop = c2_fop_alloc(&c2_rpc_fop_conn_terminate_rep_fopt, NULL);
	C2_ASSERT(fop != NULL);

	fop_ctr = c2_fop_data(fop);
	C2_ASSERT(fop_ctr != NULL);

	fop_ctr->ctr_rc = 0;
	fop_ctr->ctr_sender_id = conn.c_sender_id;

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL);
	item->ri_mach = machine;
	item->ri_sender_id = conn.c_sender_id;
	item->ri_session_id = SESSION_ID_0;
	c2_rpc_session_search(&conn, SESSION_ID_0, &session0);
	C2_ASSERT(session0 != NULL);
	item->ri_session = session0;

	fop->f_type->ft_ops->fto_execute(fop, NULL);

	printf("conn_terminate_test: state = %d, rc = %d\n", conn.c_state,
				conn.c_rc);
	c2_rpc_conn_fini(&conn);
}
extern struct c2_rpc_slot_ops c2_rpc_rcv_slot_ops;

void test_slots()
{
	enum { COUNT = 4 };
	struct c2_rpc_slot	*slot;
	struct c2_rpc_item	*item, *r;
	struct c2_rpc_item	*req;
	struct c2_rpc_item	*items[COUNT];
	int			i;

	//C2_SET0(&slot);
	//c2_rpc_slot_init(&slot, &c2_rpc_rcv_slot_ops);
	slot = session.s_slot_table[1];
	C2_ASSERT(c2_rpc_slot_invariant(slot));

	slot->sl_max_in_flight = 2;
	c2_mutex_lock(&slot->sl_mutex);
	for (i = 0; i < COUNT; i++) {
		C2_ALLOC_PTR(item);
		(i % 2) && (item->ri_flags |= RPC_ITEM_MUTABO);
		c2_rpc_slot_item_add(slot, item);
		items[i] = item;
	}
	C2_ALLOC_PTR(r);
	memcpy(r, items[0], sizeof (struct c2_rpc_item));
	c2_rpc_slot_reply_received(slot, r, &req);
	c2_rpc_slot_reply_received(slot, r, &req);
	for (i = 0; i < COUNT; i++) {
		C2_ALLOC_PTR(r);
		memcpy(r, items[i], sizeof (struct c2_rpc_item));
		c2_rpc_slot_reply_received(slot, r, &req);
	}
	C2_ALLOC_PTR(r);
	r->ri_slot_refs[0].sr_verno.vn_lsn = slot->sl_verno.vn_lsn;
	r->ri_slot_refs[0].sr_verno.vn_vc = slot->sl_verno.vn_vc;
	r->ri_slot_refs[0].sr_xid = slot->sl_xid;
	c2_rpc_slot_item_apply(slot, r);

	C2_ALLOC_PTR(r);
	r->ri_slot_refs[0].sr_verno.vn_lsn = slot->sl_verno.vn_lsn;
	r->ri_slot_refs[0].sr_verno.vn_vc = slot->sl_verno.vn_vc;
	r->ri_slot_refs[0].sr_xid = slot->sl_xid + 1;
	c2_rpc_slot_item_apply(slot, r);

	C2_ALLOC_PTR(r);
	r->ri_slot_refs[0].sr_verno.vn_lsn = items[1]->ri_slot_refs[0].sr_verno.vn_lsn;
	r->ri_slot_refs[0].sr_verno.vn_vc = items[1]->ri_slot_refs[0].sr_verno.vn_vc;
	r->ri_slot_refs[0].sr_xid = items[1]->ri_slot_refs[0].sr_xid;
	c2_rpc_slot_item_apply(slot, r);

	c2_mutex_unlock(&slot->sl_mutex);
	C2_ASSERT(c2_rpc_slot_invariant(slot));
}
#endif
int main(void)
{
	printf("Program start\n");
	c2_init();

	init();
	test_conn_establish();
	C2_ASSERT(c2_rpc_conn_invariant(connp));
	test_session_establish();
	C2_ASSERT(c2_rpc_conn_invariant(connp));
	test_session_terminate(g_sender_id, g_session_id);
	C2_ASSERT(c2_rpc_conn_invariant(connp));
	test_conn_terminate(g_sender_id);

	//test_snd_conn_establish();
	//test_snd_session_establish();
	//test_slots();
	//test_snd_session_terminate();
	//test_snd_conn_terminate();

	c2_rpcmachine_fini(machine);
	//c2_cob_domain_fini(dom);
	c2_fini();
	printf("program end\n");
	return 0;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

