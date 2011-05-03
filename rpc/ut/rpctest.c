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
	struct c2_fop			*fop;
	struct c2_fom			*fom = NULL;
	struct c2_rpc_fom_conn_create	*fom_cc;
	struct c2_rpc_conn_create	*fop_cc;
	struct c2_rpc_conn_create_rep	*fop_reply;
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
	fom_cc = (struct c2_rpc_fom_conn_create *)fom;
	fom_cc->fcc_dbenv = db;
	fom_cc->fcc_slot_table = slot_table;
	fom->fo_ops->fo_state(fom);
	fop_reply = c2_fop_data(fom_cc->fcc_fop_rep);
	C2_ASSERT(fop_reply != NULL);
	printf("Main: sender id %lu\n", fop_reply->rccr_snd_id);
	fom->fo_ops->fo_fini(fom);
	traverse_slot_table();	
	c2_fini();
	printf("program end\n");
	return 0;
}
