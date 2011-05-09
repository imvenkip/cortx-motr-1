#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fop/fop.h"
#include "rpc/session_foms.h"
#include "rpc/session_fops.h"
#include "stob/stob.h"
#include "lib/errno.h"
#include "net/net.h"

#ifdef __KERNEL__
#include "rpc/session_k.h"
#else
#include "rpc/session_u.h"
#endif

#include "fop/fop_format_def.h"
#include "rpc/session_int.h"

enum {
	DEFAULT_SLOT_COUNT = 4
};

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

int c2_rpc_fom_conn_create_state(struct c2_fom *fom_in)
{
	struct c2_fop			*fop;
	struct c2_fop			*fop_rep;
	struct c2_rpc_conn_create	*fop_in;
	struct c2_rpc_conn_create_rep	*fop_out;
	//struct c2_cob_domain		*dom;
	struct c2_rpc_fom_conn_create	*fom;
	uint64_t			sender_id;
	struct c2_rpc_slot_table_key	key;
	struct c2_rpc_slot_table_value	value;
	struct c2_db_pair		db_pair;
	int				rc;

	fom = (struct c2_rpc_fom_conn_create *)fom_in;

	printf("Called conn_create_state\n");
	C2_PRE(fom != NULL && fom->fcc_fop != NULL &&
			fom->fcc_fop_rep != NULL &&
			fom->fcc_dbenv != NULL);

	fop = fom->fcc_fop;
	fop_in = c2_fop_data(fop);
	C2_ASSERT(fop_in != NULL);

	fop_rep = fom->fcc_fop_rep;
	fop_out = c2_fop_data(fop_rep);
	C2_ASSERT(fop_out != NULL);

	printf("Cookie = %lx\n", fop_in->rcc_cookie);

	/*
	 * XXX Decide how to calculate sender_id
	 */
	sender_id = 20;

	/*
	 * Create entry for session0/slot0
	 */
	key.stk_sender_id = sender_id;
	key.stk_session_id = SESSION_0;
	key.stk_slot_id = 0;
	key.stk_slot_generation = 0;

	value.stv_verno.vn_lsn = 0;
	value.stv_verno.vn_vc = 0;
	value.stv_reply_len = 0;

	c2_db_pair_setup(&db_pair, c2_rpc_reply_cache.rc_slot_table,
				&key, sizeof key,
				&value, sizeof value);

	rc = c2_table_insert(&fom->fcc_tx, &db_pair);
	if (rc != 0 && rc != -EEXIST) {
		printf("conn_create_state: error while inserting record\n");
		goto errout;
	}
	c2_db_pair_release(&db_pair);
	c2_db_pair_fini(&db_pair);

	fop_out->rccr_snd_id = sender_id;
	fop_out->rccr_rc = 0;		/* successful */
	fop_out->rccr_cookie = fop_in->rcc_cookie; 

	printf("conn_create_state: conn created\n");
	fom_in->fo_phase = FOPH_DONE;
	return FSO_AGAIN;

errout:
	fop_out->rccr_snd_id = SENDER_ID_INVALID;
	fop_out->rccr_rc = rc;
	fop_out->rccr_cookie = fop_in->rcc_cookie;
	fom_in->fo_phase = FOPH_FAILED;
	return FSO_AGAIN;
}
void c2_rpc_fom_conn_create_fini(struct c2_fom *fom)
{
	printf("called conn_create_fini\n");
}

//=============================

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

int c2_rpc_fom_session_create_state(struct c2_fom *fom_in)
{
	struct c2_fop				*fop;
	struct c2_fop				*fop_rep;
	struct c2_rpc_session_create		*fop_in;
	struct c2_rpc_session_create_rep	*fop_out;
	struct c2_rpc_fom_session_create	*fom;
	uint64_t				session_id;
	uint64_t				sender_id;
	struct c2_rpc_slot_table_key		key;
	struct c2_rpc_slot_table_value		value;
	struct c2_db_pair			db_pair;
	int					rc;
	int					i;

	fom = (struct c2_rpc_fom_session_create *)fom_in;

	printf("Called session_create_state\n");
	C2_PRE(fom != NULL && fom->fsc_fop != NULL &&
			fom->fsc_fop_rep != NULL &&
			fom->fsc_dbenv != NULL); 

	fop = fom->fsc_fop;
	fop_in = c2_fop_data(fop);
	C2_ASSERT(fop_in != NULL);

	fop_rep = fom->fsc_fop_rep;
	fop_out = c2_fop_data(fop_rep);
	C2_ASSERT(fop_out != NULL);

	/*
	 * XXX Decide how to calculate session_id
	 */
	session_id = 100;
	sender_id = fop_in->rsc_snd_id;

	/*
	 * Create entry for session0/slot0
	 */
	key.stk_sender_id = sender_id;
	key.stk_session_id = session_id;
	key.stk_slot_generation = 0;

	value.stv_verno.vn_lsn = 0;
	value.stv_verno.vn_vc = 0;
	value.stv_reply_len = 0;

	c2_db_pair_setup(&db_pair, c2_rpc_reply_cache.rc_slot_table,
				&key, sizeof key,
				&value, sizeof value);
	for (i = 0; i < DEFAULT_SLOT_COUNT; i++) {
		key.stk_slot_id = i;
		rc = c2_table_insert(&fom->fsc_tx, &db_pair);
		if (rc != 0 && rc != -EEXIST) {
			printf("conn_create_state: error while inserting record\n");
			goto errout;
		}
	}
	c2_db_pair_release(&db_pair);
	c2_db_pair_fini(&db_pair);

	fop_out->rscr_rc = 0; 		/* success */
	fop_out->rscr_session_id = session_id;
	fom_in->fo_phase = FOPH_DONE;
	printf("Session create finished\n");
	return FSO_AGAIN;

errout:
	fop_out->rscr_rc = rc;
	fop_out->rscr_session_id = SESSION_ID_INVALID;
	fom_in->fo_phase = FOPH_FAILED;
	return FSO_AGAIN;
}
void c2_rpc_fom_session_create_fini(struct c2_fom *fom)
{
	printf("called session_create_fini\n");
}
