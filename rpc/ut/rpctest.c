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

char db_name[] = "rpc_test_db";

struct c2_dbenv			*db;
struct c2_table			*slot_table;
void init()
{
	int				rc;

	C2_ALLOC_PTR(db);
	C2_ALLOC_PTR(slot_table);
	C2_ASSERT(db != NULL && slot_table != NULL);

	rc = c2_dbenv_init(db, db_name, 0);
	C2_ASSERT(rc == 0);

	C2_ASSERT(rc == 0);

	rc = c2_table_init(slot_table, db, C2_RPC_SLOT_TABLE_NAME, 0,
		&c2_rpc_slot_table_ops);
	C2_ASSERT(rc == 0);

	/* XXX temporary reply cache */
	c2_list_init(&c2_reply_cache_list);

	printf("dbenv created\n");
}
void traverse_slot_table()
{
	struct c2_db_cursor		cursor;
	struct c2_db_pair		db_pair;
	struct c2_rpc_slot_table_key	key;
	struct c2_rpc_slot_table_value	value;
	struct c2_db_tx			tx;
	int				rc;

	printf("========= SLOT TABLE ==============\n");
	rc = c2_db_tx_init(&tx, db, 0);
	C2_ASSERT(rc == 0);

	rc = c2_db_cursor_init(&cursor, slot_table, &tx);
	C2_ASSERT(rc == 0);

	c2_db_pair_setup(&db_pair, slot_table, &key, sizeof key,
				&value, sizeof value);
	while ((rc = c2_db_cursor_next(&cursor, &db_pair)) == 0) {
		printf("[%lu:%lu:%u:%lu] -> [%lu]\n",
				key.stk_sender_id, key.stk_session_id, key.stk_slot_id,
				key.stk_slot_generation, value.stv_verno.vn_vc);
	}
	printf("====================================\n");
	c2_db_pair_release(&db_pair);
	c2_db_pair_fini(&db_pair);
	c2_db_cursor_fini(&cursor);
	c2_db_tx_commit(&tx);
	
}
int main(void)
{
	struct c2_fop				*fop;
	struct c2_fom				*fom = NULL;
	struct c2_rpc_fom_conn_create		*fom_cc;
	struct c2_rpc_conn_create		*fop_cc;
	struct c2_rpc_conn_create_rep		*fop_reply;

	struct c2_rpc_fom_session_create	*fom_sc;
	struct c2_rpc_session_create		*fop_sc;
	struct c2_rpc_session_create_rep	*fop_sc_reply;
	struct c2_rpc_item			*item_in;
//	struct c2_rpc_item			*item_out;

	printf("Program start\n");
	c2_init();

	init();

	fop = c2_fop_alloc(&c2_rpc_conn_create_fopt, NULL);
	C2_ASSERT(fop != NULL);

	fop_cc = c2_fop_data(fop);
	C2_ASSERT(fop_cc != NULL);

	fop_cc->rcc_cookie = 0xC00CEE;

	fop->f_type->ft_ops->fto_fom_init(fop, &fom);
	C2_ASSERT(fom != NULL);

	item_in = c2_fop_to_rpc_item(fop);
	item_in->ri_sender_id = SENDER_ID_INVALID;

	fom_cc = (struct c2_rpc_fom_conn_create *)fom;

	fom_cc->fcc_dbenv = db;
	fom_cc->fcc_slot_table = slot_table;
	c2_db_tx_init(&fom_cc->fcc_tx, db, 0);

	fom->fo_ops->fo_state(fom);

	c2_rpc_session_reply_prepare(&fom_cc->fcc_fop->f_item,
			&fom_cc->fcc_fop_rep->f_item,
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
	fom->fo_ops->fo_fini(fom);
	traverse_slot_table();	
/*=======================================================================*/
	fop = c2_fop_alloc(&c2_rpc_session_create_fopt, NULL);
	C2_ASSERT(fop != NULL);

	fop_sc = c2_fop_data(fop);
	C2_ASSERT(fop_sc != NULL);

	fop_sc->rsc_snd_id = fop_reply->rccr_snd_id;

	item_in = c2_fop_to_rpc_item(fop);
	item_in->ri_sender_id = fop_reply->rccr_snd_id;
	item_in->ri_session_id = SESSION_0;
	item_in->ri_slot_id = 0;
	item_in->ri_slot_generation = 0;

	fop->f_type->ft_ops->fto_fom_init(fop, &fom);
	C2_ASSERT(fom != NULL);

	fom_sc = (struct c2_rpc_fom_session_create *)fom;
	fom_sc->fsc_dbenv = db;
	fom_sc->fsc_slot_table = slot_table;
	c2_db_tx_init(&fom_sc->fsc_tx, db, 0);

	fom->fo_ops->fo_state(fom);

	c2_rpc_session_reply_prepare(&fom_sc->fsc_fop->f_item,
			&fom_sc->fsc_fop_rep->f_item,
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
	c2_fini();
	printf("program end\n");
	return 0;
}
