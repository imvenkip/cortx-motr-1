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

enum {
	SESSION_CREATE_VC = 0,
	SESSION_DESTROY_VC = 1
};

void init()
{
	struct c2_cob_domain_id dom_id = { 42 };
	struct c2_cob			*cob = NULL;
	struct c2_db_tx			tx;
	int				rc;

	C2_ALLOC_PTR(db);
	C2_ALLOC_PTR(dom);
	C2_ALLOC_PTR(machine);
	C2_ASSERT(db != NULL && dom != NULL && machine != NULL);

	rc = c2_dbenv_init(db, db_name, 0);
	C2_ASSERT(rc == 0);

	rc = c2_cob_domain_init(dom, db, &dom_id);
	C2_ASSERT(rc == 0);

	c2_db_tx_init(&tx, db, 0);
	rc = c2_rpc_cob_create_helper(dom, NULL, "SESSIONS", &cob, &tx);
	C2_ASSERT(rc == 0 || rc == -EEXIST);
	c2_db_tx_commit(&tx);
	if(rc == 0)
		c2_cob_put(cob);
	cob = NULL;

	rc = c2_rpc_reply_cache_init(&c2_rpc_reply_cache, db);
	C2_ASSERT(rc == 0);

	c2_rpcmachine_init(machine);
	printf("dbenv created\n");
}
void traverse_slot_table()
{
	struct c2_table			*slot_table;
	struct c2_db_cursor		cursor;
	struct c2_db_pair		db_pair;
	struct c2_rpc_slot_table_key	key;
	struct c2_rpc_inmem_slot_table_value	value;
	struct c2_db_tx			tx;
	int				rc;
	printf("========= SLOT TABLE ==============\n");

	slot_table = c2_rpc_reply_cache.rc_inmem_slot_table;

	rc = c2_db_tx_init(&tx, db, 0);
	C2_ASSERT(rc == 0);

	rc = c2_db_cursor_init(&cursor, slot_table, &tx);
	C2_ASSERT(rc == 0);

	c2_db_pair_setup(&db_pair, slot_table, &key, sizeof key,
				&value, sizeof value);
	while ((rc = c2_db_cursor_next(&cursor, &db_pair)) == 0) {
		printf("[%lu:%lu:%u:%lu] -> [%s]\n",
				key.stk_sender_id, key.stk_session_id, key.stk_slot_id,
				key.stk_slot_generation, value.istv_busy ? "true" : "false");
	}
	printf("====================================\n");
	c2_db_pair_release(&db_pair);
	c2_db_pair_fini(&db_pair);
	c2_db_cursor_fini(&cursor);
	c2_db_tx_commit(&tx);
	
}
void test_session_destroy(uint64_t sender_id, uint64_t session_id)
{
	struct c2_fop				*fop;
	struct c2_rpc_session_destroy		*fop_in;
	struct c2_fom				*fom;
	struct c2_rpc_fom_session_destroy	*fom_sd;
	struct c2_rpc_item			*item;
	struct c2_rpc_item			*cached_item;
	enum c2_rpc_session_seq_check_result	sc;

	/*
	 * Allocate and fill FOP
	 */
	fop = c2_fop_alloc(&c2_rpc_session_destroy_fopt, NULL);
	C2_ASSERT(fop != NULL);

	fop_in = c2_fop_data(fop);
	C2_ASSERT(fop_in != NULL);

	fop_in->rsd_sender_id = sender_id;
	fop_in->rsd_session_id = session_id;

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
	sc = c2_rpc_session_item_received(item, dom, &cached_item);
	//sc = SCR_ACCEPT_ITEM;

	/*
	 * Instantiate fom
	 */
	if (sc == SCR_ACCEPT_ITEM) {
		fop->f_type->ft_ops->fto_fom_init(fop, &fom);
		C2_ASSERT(fom != NULL);

		/*
		 * Initialize type-specific fields of fom along with tx
	 	 */
		fom_sd = container_of(fom, struct c2_rpc_fom_session_destroy,
					fsd_gen);
		C2_ASSERT(fom_sd != NULL);
		fom_sd->fsd_dbenv = db;
		c2_db_tx_init(&fom_sd->fsd_tx, db, 0);
		fom_sd->fsd_dom = dom;

		/*
		 * Execute fom
		 */
		fom->fo_ops->fo_state(fom);

		/*
		 * store reply in reply-cache
		 */

		c2_rpc_session_reply_prepare(&fom_sd->fsd_fop->f_item,
				&fom_sd->fsd_fop_rep->f_item, dom,
				&fom_sd->fsd_tx);

		/*
		 * commit/abort tx
		 */
		C2_ASSERT(fom->fo_phase == FOPH_DONE ||
				fom->fo_phase == FOPH_FAILED);

		if (fom->fo_phase == FOPH_DONE) {
			c2_db_tx_commit(&fom_sd->fsd_tx);
		} else if (fom->fo_phase == FOPH_FAILED) {
			c2_db_tx_abort(&fom_sd->fsd_tx);
		}
	
		/*
		 * test reply contents
		 */
		traverse_slot_table();
		c2_cob_namespace_traverse(dom);
		c2_cob_fb_traverse(dom);
	}
	
}

void test_conn_terminate(uint64_t sender_id)
{
	struct c2_fop				*fop;
	struct c2_rpc_conn_terminate		*fop_in;
	struct c2_fom				*fom;
	struct c2_rpc_fom_conn_terminate	*fom_ct;
	struct c2_rpc_item			*item;
	struct c2_rpc_item			*cached_item;
	enum c2_rpc_session_seq_check_result	sc;

	/*
	 * Allocate and fill FOP
	 */
	fop = c2_fop_alloc(&c2_rpc_conn_terminate_fopt, NULL);
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
	sc = c2_rpc_session_item_received(item, dom, &cached_item);
	//sc = SCR_ACCEPT_ITEM;

	/*
	 * Instantiate fom
	 */
	if (sc == SCR_ACCEPT_ITEM) {
		fop->f_type->ft_ops->fto_fom_init(fop, &fom);
		C2_ASSERT(fom != NULL);

		/*
		 * Initialize type-specific fields of fom along with tx
	 	 */
		fom_ct = container_of(fom, struct c2_rpc_fom_conn_terminate,
					fct_gen);
		C2_ASSERT(fom_ct != NULL);
		fom_ct->fct_dbenv = db;
		c2_db_tx_init(&fom_ct->fct_tx, db, 0);
		fom_ct->fct_dom = dom;

		/*
		 * Execute fom
		 */
		fom->fo_ops->fo_state(fom);

		/*
		 * store reply in reply-cache
		 */
		c2_rpc_session_reply_prepare(&fom_ct->fct_fop->f_item,
				&fom_ct->fct_fop_rep->f_item, dom,
				&fom_ct->fct_tx);
		/*
		 * commit/abort tx
		 */
		C2_ASSERT(fom->fo_phase == FOPH_DONE ||
				fom->fo_phase == FOPH_FAILED);

		if (fom->fo_phase == FOPH_DONE) {
			c2_db_tx_commit(&fom_ct->fct_tx);
		} else if (fom->fo_phase == FOPH_FAILED) {
			c2_db_tx_abort(&fom_ct->fct_tx);
		}
	
		/*
		 * test reply contents
		 */
		traverse_slot_table();
		c2_cob_namespace_traverse(dom);
		c2_cob_fb_traverse(dom);
	}
	
}
uint64_t	g_sender_id;

void test_conn_create()
{
	struct c2_fop				*fop;
	struct c2_fom				*fom = NULL;
	struct c2_rpc_fom_conn_create		*fom_cc;
	struct c2_rpc_conn_create		*fop_cc;
	struct c2_rpc_conn_create_rep		*fop_reply;
	struct c2_rpc_item			*item_in;
	struct c2_rpc_item			*cached_item;
	enum c2_rpc_session_seq_check_result	sc;

	/* create conn_create fop */
	fop = c2_fop_alloc(&c2_rpc_conn_create_fopt, NULL);
	C2_ASSERT(fop != NULL);

	fop_cc = c2_fop_data(fop);
	C2_ASSERT(fop_cc != NULL);

	fop_cc->rcc_cookie = 0xC00CEE;

	/* Processing that happens when fop is submitted */
	item_in = c2_fop_to_rpc_item(fop);
	item_in->ri_sender_id = SENDER_ID_INVALID;
	item_in->ri_session_id = SESSION_ID_NOSESSION;

	/* item is received on receiver side */
	sc = c2_rpc_session_item_received(item_in, dom, &cached_item);
	//sc = SCR_ACCEPT_ITEM;

	if (sc == SCR_ACCEPT_ITEM) {
		/* If item is accepted then fop is created and executed */
		fop->f_type->ft_ops->fto_fom_init(fop, &fom);
		C2_ASSERT(fom != NULL);

		fom_cc = container_of(fom, struct c2_rpc_fom_conn_create, fcc_gen);

		fom_cc->fcc_dbenv = db;
		fom_cc->fcc_dom = dom;
		/* It is reqh generic phases that init/commit/abort a transaction */
		c2_db_tx_init(&fom_cc->fcc_tx, db, 0);

		fom->fo_ops->fo_state(fom);

		/* When reply is submitted to rpc layer, this routine is called */
		c2_rpc_session_reply_prepare(&fom_cc->fcc_fop->f_item,
				&fom_cc->fcc_fop_rep->f_item, dom,
				&fom_cc->fcc_tx);

		C2_ASSERT(fom->fo_phase == FOPH_DONE ||
				fom->fo_phase == FOPH_FAILED);

		if (fom->fo_phase == FOPH_DONE) {
			c2_db_tx_commit(&fom_cc->fcc_tx);
		} else if (fom->fo_phase == FOPH_FAILED) {
			c2_db_tx_abort(&fom_cc->fcc_tx);
		}
		
		fop_reply = c2_fop_data(fom_cc->fcc_fop_rep);
		C2_ASSERT(fop_reply != NULL);
		printf("Main: sender id %lu\n", fop_reply->rccr_snd_id);
		g_sender_id = fop_reply->rccr_snd_id;
		fom->fo_ops->fo_fini(fom);
	}
	traverse_slot_table();	
	c2_cob_namespace_traverse(dom);
	c2_cob_fb_traverse(dom);
}

void test_session_create()
{
	struct c2_fop				*fop;
	struct c2_fom				*fom = NULL;
	struct c2_rpc_fom_session_create	*fom_sc;
	struct c2_rpc_session_create		*fop_sc;
	struct c2_rpc_session_create_rep	*fop_sc_reply;
	struct c2_rpc_item			*item_in;
	struct c2_rpc_item			*cached_item;
	enum c2_rpc_session_seq_check_result	sc;

	fop = c2_fop_alloc(&c2_rpc_session_create_fopt, NULL);
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

	sc = c2_rpc_session_item_received(item_in, dom, &cached_item);
	//sc = SCR_ACCEPT_ITEM;
	if (sc == SCR_ACCEPT_ITEM) {
		fop->f_type->ft_ops->fto_fom_init(fop, &fom);
		C2_ASSERT(fom != NULL);

		fom_sc = (struct c2_rpc_fom_session_create *)fom;
		fom_sc->fsc_dbenv = db;
		c2_db_tx_init(&fom_sc->fsc_tx, db, 0);
		fom_sc->fsc_dom = dom;

		fom->fo_ops->fo_state(fom);

		c2_rpc_session_reply_prepare(&fom_sc->fsc_fop->f_item,
			&fom_sc->fsc_fop_rep->f_item, dom,
			&fom_sc->fsc_tx);

		C2_ASSERT(fom->fo_phase == FOPH_DONE ||
			fom->fo_phase == FOPH_FAILED);

		if (fom->fo_phase == FOPH_DONE) {
			c2_db_tx_commit(&fom_sc->fsc_tx);
		} else if (fom->fo_phase == FOPH_FAILED) {
			c2_db_tx_abort(&fom_sc->fsc_tx);
		}

		fop_sc_reply = c2_fop_data(fom_sc->fsc_fop_rep);
		C2_ASSERT(fop_sc_reply != NULL);
		printf("Main: session id %lu\n", fop_sc_reply->rscr_session_id);

		fom->fo_ops->fo_fini(fom);

		traverse_slot_table();
		c2_cob_namespace_traverse(dom);
		c2_cob_fb_traverse(dom);
	}

}
struct c2_rpc_conn		conn;
struct c2_rpc_session			session;
struct c2_thread	thread;

void conn_status_check(void *arg)
{
	struct c2_time		timeout;
	bool			got_event;

	printf("Thread about to start wait on conn ACTIVE\n");
	c2_time_now(&timeout);
	timeout.ts.tv_sec += 3;
	got_event = c2_rpc_conn_timedwait(&conn, CS_CONN_ACTIVE,
			&timeout);
	if (got_event && conn.c_state == CS_CONN_ACTIVE) {
		printf("thread: conn is active %lu\n", conn.c_sender_id);
	} else {
		printf("thread: time out during conn creation\n");
	}
}
void test_snd_conn_create()
{
	struct c2_service_id		svc_id;
	struct c2_fop			*fop;
	struct c2_rpc_conn_create_rep	*fop_ccr;

	C2_SET0(&svc_id);

	printf("testing conn_create\n");
	c2_rpc_conn_init(&conn, &svc_id, machine);
	C2_ASSERT(conn.c_state == CS_CONN_INITIALIZING ||
			conn.c_state == CS_CONN_INIT_FAILED);

	c2_thread_init(&thread, NULL, conn_status_check, NULL);

	sleep(5);

	fop = c2_fop_alloc(&c2_rpc_conn_create_rep_fopt, NULL);
	C2_ASSERT(fop != NULL);

	fop_ccr = c2_fop_data(fop);
	C2_ASSERT(fop_ccr != NULL);

	fop_ccr->rccr_rc = 0;
	fop_ccr->rccr_snd_id = 20;
	fop_ccr->rccr_cookie = (uint64_t)&conn;

	fop->f_type->ft_ops->fto_execute(fop, NULL);
	c2_thread_join(&thread);
	c2_thread_fini(&thread);
}
static void thread_entry(void *arg)
{
	struct c2_time		timeout;
	bool			got_event;

	printf("Thread about to start wait on session ALIVE\n");
	c2_time_now(&timeout);
	timeout.ts.tv_sec += 3;
	got_event = c2_rpc_session_timedwait(&session, SESSION_ALIVE,
			&timeout);
	if (got_event && session.s_state == SESSION_ALIVE) {
		printf("thread: session got created %lu\n", session.s_session_id);
	} else {
		printf("thread: time out during session creation\n");
	}
}
void test_snd_session_create()
{
	struct c2_fop				*fop;
	struct c2_rpc_session_create_rep	*fop_scr;
	struct c2_rpc_item			*item;
	int					rc;

	C2_SET0(&session);
	rc = c2_rpc_session_create(&session, &conn);
	if (rc != 0) {
		printf("test_sc: failed to create session\n");
		return;
	}
	c2_thread_init(&thread, NULL, thread_entry, NULL);
	sleep(1);
	fop = c2_fop_alloc(&c2_rpc_session_create_rep_fopt, NULL);
	C2_ASSERT(fop != NULL);

	fop_scr = c2_fop_data(fop);
	C2_ASSERT(fop_scr != NULL);

	fop_scr->rscr_rc = 0;
	fop_scr->rscr_sender_id = conn.c_sender_id;
	fop_scr->rscr_session_id = 100;

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL);
	item->ri_mach = machine;

	fop->f_type->ft_ops->fto_execute(fop, NULL);

	c2_thread_join(&thread);
	c2_thread_fini(&thread);
}

void test_snd_session_terminate()
{
	struct c2_fop				*fop;
	struct c2_rpc_session_destroy_rep	*fop_sdr;
	struct c2_rpc_item			*item;
	int					rc;

	rc = c2_rpc_session_terminate(&session);
	if (rc != 0) {
		printf("test Session terminate failed\n");
		return;
	}

	fop = c2_fop_alloc(&c2_rpc_session_destroy_rep_fopt, NULL);
	C2_ASSERT(fop != NULL);

	fop_sdr = c2_fop_data(fop);
	C2_ASSERT(fop_sdr != NULL);

	fop_sdr->rsdr_rc = 0;
	fop_sdr->rsdr_sender_id = conn.c_sender_id;
	fop_sdr->rsdr_session_id = session.s_session_id;

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL);
	item->ri_mach = machine;

	fop->f_type->ft_ops->fto_execute(fop, NULL);
	c2_rpc_session_fini(&session);
}
void test_snd_conn_terminate()
{
	struct c2_fop				*fop;
	struct c2_rpc_conn_terminate_rep	*fop_ctr;
	struct c2_rpc_item			*item;
	int					rc;

	rc = c2_rpc_conn_terminate(&conn);
	if (rc != 0) {
		printf("test: conn terminate failed with %d\n", rc);
		return;
	}

	fop = c2_fop_alloc(&c2_rpc_conn_terminate_rep_fopt, NULL);
	C2_ASSERT(fop != NULL);

	fop_ctr = c2_fop_data(fop);
	C2_ASSERT(fop_ctr != NULL);

	fop_ctr->ctr_rc = 0;
	fop_ctr->ctr_sender_id = conn.c_sender_id;

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL);
	item->ri_mach = machine;

	fop->f_type->ft_ops->fto_execute(fop, NULL);
	c2_rpc_conn_fini(&conn);
}
int main(void)
{
	printf("Program start\n");
	c2_init();

	init();
/*
	test_conn_create();
	test_session_create();
	test_session_destroy(20, 100);
	test_conn_terminate(20);
*/
	test_snd_conn_create();
	test_snd_session_create();
	test_snd_session_terminate();
	test_snd_conn_terminate();

	c2_cob_domain_fini(dom);
	c2_rpc_reply_cache_fini(&c2_rpc_reply_cache);
	c2_fini();
	printf("program end\n");
	return 0;
}
