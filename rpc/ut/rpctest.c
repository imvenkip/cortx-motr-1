/* -*- C -*- */

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
#include "rpc/session_int.h"


char db_name[] = "test_db";

struct c2_dbenv			*db;
struct c2_cob_domain		*dom;
struct c2_rpcmachine		*machine;

struct c2_service_id		svc_id;

enum {
	SESSION_CREATE_VC = 0,
	SESSION_DESTROY_VC = 1
};

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
	C2_ASSERT(rc == 0);

//	rc = c2_rpc_reply_cache_init(&c2_rpc_reply_cache, db);
//	C2_ASSERT(rc == 0);

	c2_rpcmachine_init(machine, dom);
	printf("dbenv created\n");
}
void test_session_terminate(uint64_t sender_id, uint64_t session_id)
{
	struct c2_fop				*fop;
	struct c2_rpc_fop_session_terminate	*fop_in;
	struct c2_fom				*fom;
	struct c2_rpc_item			*item;
	enum c2_rpc_session_seq_check_result	sc;

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
	item->ri_sender_id = sender_id;
	item->ri_session_id = SESSION_0;
	item->ri_slot_id = 0;
	item->ri_slot_generation = 0;
	item->ri_verno.vn_lsn = C2_LSN_RESERVED_NR + 10;
	item->ri_verno.vn_vc = SESSION_DESTROY_VC;
	item->ri_mach = machine;

	/*
	 * "Receive" the item
	 */
	//sc = c2_rpc_session_item_received(item, &cached_item);
	sc = SCR_ACCEPT_ITEM;
	/*
	 * Instantiate fom
	 */
	if (sc == SCR_ACCEPT_ITEM) {
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
	}

}

void test_conn_terminate(uint64_t sender_id)
{
	struct c2_fop				*fop;
	struct c2_rpc_fop_conn_terminate	*fop_in;
	struct c2_fom				*fom;
	struct c2_rpc_fom_conn_terminate	*fom_ct;
	struct c2_rpc_item			*item;
	enum c2_rpc_session_seq_check_result	sc;

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
	item->ri_sender_id = SENDER_ID_INVALID;
	item->ri_session_id = SESSION_ID_NOSESSION;
	item->ri_slot_id = 0;
	item->ri_slot_generation = 0;
	item->ri_verno.vn_lsn = 0;
	item->ri_verno.vn_vc = 0;
	item->ri_mach = machine;

	/*
	 * "Receive" the item
	 */
	//sc = c2_rpc_session_item_received(item, &cached_item);
	sc = SCR_ACCEPT_ITEM;
	/*
	 * Instantiate fom
	 */
	if (sc == SCR_ACCEPT_ITEM) {
		fop->f_type->ft_ops->fto_fom_init(fop, &fom);
		C2_ASSERT(fom != NULL);
		fom_ct = container_of(fom, struct c2_rpc_fom_conn_terminate,
					fct_gen);
		C2_ASSERT(fom_ct != NULL);
		fom->fo_ops->fo_state(fom);

		C2_ASSERT(fom->fo_phase == FOPH_DONE ||
				fom->fo_phase == FOPH_FAILED);

		/*
		 * test reply contents
		 */
		c2_cob_namespace_traverse(dom);
		c2_cob_fb_traverse(dom);
	}

}
uint64_t	g_sender_id;
uint64_t	g_session_id;

void test_conn_create()
{
	struct c2_fop				*fop;
	struct c2_fom				*fom = NULL;
	struct c2_rpc_fom_conn_create		*fom_cc;
	struct c2_rpc_fop_conn_create		*fop_cc;
	struct c2_rpc_fop_conn_create_rep	*fop_reply;
	struct c2_rpc_item			*item_in;
	enum c2_rpc_session_seq_check_result	sc;

	/* create conn_create fop */
	fop = c2_fop_alloc(&c2_rpc_fop_conn_create_fopt, NULL);
	C2_ASSERT(fop != NULL);

	fop_cc = c2_fop_data(fop);
	C2_ASSERT(fop_cc != NULL);

	fop_cc->rcc_cookie = 0xC00CEE;

	/* Processing that happens when fop is submitted */
	item_in = c2_fop_to_rpc_item(fop);
	item_in->ri_sender_id = SENDER_ID_INVALID;
	item_in->ri_session_id = SESSION_ID_NOSESSION;
	item_in->ri_mach = machine;
	/* item is received on receiver side */
	//sc = c2_rpc_session_item_received(item_in, &cached_item);
	sc = SCR_ACCEPT_ITEM;
	if (sc == SCR_ACCEPT_ITEM) {
		/* If item is accepted then fop is created and executed */
		fop->f_type->ft_ops->fto_fom_init(fop, &fom);
		C2_ASSERT(fom != NULL);
		fom_cc = container_of(fom, struct c2_rpc_fom_conn_create, fcc_gen);

		fom->fo_ops->fo_state(fom);

		C2_ASSERT(fom->fo_phase == FOPH_DONE ||
				fom->fo_phase == FOPH_FAILED);

		fop_reply = c2_fop_data(fom_cc->fcc_fop_rep);
		C2_ASSERT(fop_reply != NULL);
		printf("test_conn_create: sender id %lu\n", fop_reply->rccr_snd_id);
		g_sender_id = fop_reply->rccr_snd_id;
		fom->fo_ops->fo_fini(fom);
	}
	c2_cob_namespace_traverse(dom);
	c2_cob_fb_traverse(dom);
}

void test_session_create()
{
	struct c2_fop				*fop;
	struct c2_fom				*fom = NULL;
	struct c2_rpc_fom_session_create	*fom_sc;
	struct c2_rpc_fop_session_create	*fop_sc;
	struct c2_rpc_fop_session_create_rep	*fop_sc_reply;
	struct c2_rpc_item			*item_in;
	enum c2_rpc_session_seq_check_result	sc;

	fop = c2_fop_alloc(&c2_rpc_fop_session_create_fopt, NULL);
	C2_ASSERT(fop != NULL);

	fop_sc = c2_fop_data(fop);
	C2_ASSERT(fop_sc != NULL);

	fop_sc->rsc_snd_id = g_sender_id;

	item_in = c2_fop_to_rpc_item(fop);
	item_in->ri_sender_id = g_sender_id;
	item_in->ri_session_id = SESSION_0;
	item_in->ri_slot_id = 0;
	item_in->ri_slot_generation = 0;
	item_in->ri_verno.vn_lsn = C2_LSN_RESERVED_NR + 10;
	item_in->ri_verno.vn_vc = SESSION_CREATE_VC;
	item_in->ri_mach = machine;

	//sc = c2_rpc_session_item_received(item_in, &cached_item);
	sc = SCR_ACCEPT_ITEM;
	if (sc == SCR_ACCEPT_ITEM) {
		fop->f_type->ft_ops->fto_fom_init(fop, &fom);
		C2_ASSERT(fom != NULL);

		fom_sc = (struct c2_rpc_fom_session_create *)fom;
		fom->fo_ops->fo_state(fom);
		C2_ASSERT(fom->fo_phase == FOPH_DONE ||
			fom->fo_phase == FOPH_FAILED);

		fop_sc_reply = c2_fop_data(fom_sc->fsc_fop_rep);
		C2_ASSERT(fop_sc_reply != NULL);
		printf("test_session_create: session id %lu\n", fop_sc_reply->rscr_session_id);
		g_session_id = fop_sc_reply->rscr_session_id;
		fom->fo_ops->fo_fini(fom);

		c2_cob_namespace_traverse(dom);
		c2_cob_fb_traverse(dom);
	}

}
struct c2_rpc_conn		conn;
struct c2_rpc_session		session;
struct c2_thread		thread;
#if 0
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
void test_snd_conn_create()
{
	struct c2_fop				*fop;
	struct c2_rpc_fop_conn_create_rep	*fop_ccr;
	struct c2_rpc_item			*item;

	C2_SET0(&svc_id);
	strcpy(svc_id.si_uuid, "rpc_test_uuid");

	printf("testing conn_create: conn %p\n", &conn);
	c2_rpc_conn_create(&conn, &svc_id, machine);
	C2_ASSERT(conn.c_state == C2_RPC_CONN_INITIALISING ||
			conn.c_state == C2_RPC_CONN_FAILED);

	c2_thread_init(&thread, NULL, conn_status_check, NULL,
		       "conn_status_check");

	sleep(1);

	fop = c2_fop_alloc(&c2_rpc_fop_conn_create_rep_fopt, NULL);
	C2_ASSERT(fop != NULL);

	fop_ccr = c2_fop_data(fop);
	C2_ASSERT(fop_ccr != NULL);

	fop_ccr->rccr_rc = 0;
	fop_ccr->rccr_snd_id = 20;
	fop_ccr->rccr_cookie = (uint64_t)&conn;

	item = c2_fop_to_rpc_item(fop);
	item->ri_mach = machine;

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
	got_event = c2_rpc_session_timedwait(&session, C2_RPC_SESSION_ALIVE,
			timeout);
	if (got_event && session.s_state == C2_RPC_SESSION_ALIVE) {
		printf("thread: session got created %lu\n", session.s_session_id);
	} else {
		printf("thread: time out during session creation\n");
	}
}
void test_snd_session_create()
{
	struct c2_fop				*fop;
	struct c2_rpc_fop_session_create_rep	*fop_scr;
	struct c2_rpc_item			*item;
	int					rc;

	C2_SET0(&session);
	rc = c2_rpc_session_create(&session, &conn);
	if (rc != 0) {
		printf("test_sc: failed to create session\n");
		return;
	}
	c2_thread_init(&thread, NULL, thread_entry, NULL, "thread_entry");
	sleep(1);
	fop = c2_fop_alloc(&c2_rpc_fop_session_create_rep_fopt, NULL);
	C2_ASSERT(fop != NULL);

	fop_scr = c2_fop_data(fop);
	C2_ASSERT(fop_scr != NULL);

	fop_scr->rscr_rc = 0;
	fop_scr->rscr_sender_id = conn.c_sender_id;
	fop_scr->rscr_session_id = 100;

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL);
	item->ri_mach = machine;
	item->ri_sender_id = conn.c_sender_id;
	item->ri_session_id = SESSION_0;
	item->ri_slot_id = 0;
	item->ri_slot_generation = 0;
	item->ri_verno.vn_vc = 0;

	//c2_rpc_session_reply_item_received(item, &req_item);
	//printf("test_snd_session_create: req item %p\n", req_item);
	fop->f_type->ft_ops->fto_execute(fop, NULL);

	c2_thread_join(&thread);
	c2_thread_fini(&thread);
}

void test_snd_session_terminate()
{
	struct c2_fop				*fop;
	struct c2_rpc_fop_session_terminate_rep	*fop_str;
	struct c2_rpc_item			*item;
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
	item->ri_session_id = SESSION_0;
	item->ri_slot_id = 0;
	item->ri_slot_generation = 0;
	item->ri_verno.vn_vc = 1;

	//c2_rpc_session_reply_item_received(item, &req_item);
	//printf("test_snd_session_create: req item %p\n", req_item);

	fop->f_type->ft_ops->fto_execute(fop, NULL);
	printf("snd_session_create_test: state %d rc %d\n", session.s_state,
			session.s_rc);
	c2_rpc_session_fini(&session);
}
void test_snd_conn_terminate()
{
	struct c2_fop				*fop;
	struct c2_rpc_fop_conn_terminate_rep	*fop_ctr;
	struct c2_rpc_item			*item;
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

	fop->f_type->ft_ops->fto_execute(fop, NULL);

	printf("conn_terminate_test: state = %d, rc = %d\n", conn.c_state,
				conn.c_rc);
	c2_rpc_conn_fini(&conn);
}
#endif

#if 0
void test_item_prepare()
{
	struct c2_rpc_item	item[5];
	struct c2_rpc_item	reply_item;
	struct c2_rpc_item	*req_item = NULL;
	int			rc;
	int			i;

	for (i = 0; i < 5; i++) {
		c2_rpc_item_init(&item[i], machine);

		item[i].ri_service_id = &svc_id;
		rc = c2_rpc_session_item_prepare(&item[i]);

		printf("test_item_prepare: item_prepare() returned %d\n", rc);
	}
	reply_item.ri_sender_id = item[0].ri_sender_id;
	reply_item.ri_session_id = item[0].ri_session_id;
	reply_item.ri_slot_id = item[0].ri_slot_id;
	reply_item.ri_slot_generation = item[0].ri_slot_generation;
	reply_item.ri_verno = item[0].ri_verno;
	reply_item.ri_mach = machine;

	//c2_rpc_session_reply_item_received(&reply_item, &req_item);
	//C2_ASSERT(req_item == &item[0]);

	c2_rpc_item_init(&item[0], machine);
	item[0].ri_service_id = &svc_id;
	//rc = c2_rpc_session_item_prepare(&item[0]);

}
#endif 
int main(void)
{
	printf("Program start\n");
	c2_init();

	init();
	//test_conn_create();
	//test_session_create();
	//test_session_terminate(g_sender_id, g_session_id);
	//test_conn_terminate(g_sender_id);

	//test_snd_conn_create();
	//test_snd_session_create();
	//test_item_prepare();
	//test_snd_session_terminate();
	//test_snd_conn_terminate();

	c2_rpcmachine_fini(machine);
	c2_cob_domain_fini(dom);
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

