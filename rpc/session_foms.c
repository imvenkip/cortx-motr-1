/* -*- C -*- */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fop/fop.h"
#include "rpc/session_foms.h"
#include "rpc/session_fops.h"
#include "stob/stob.h"
#include "lib/errno.h"
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
	struct c2_rpc_item			*item;
	struct c2_rpcmachine			*machine;
	struct c2_fop				*fop_rep;
	struct c2_rpc_conn_create		*fop_in;
	struct c2_rpc_conn_create_rep		*fop_out;
	struct c2_rpc_fom_conn_create		*fom_cc;
	uint64_t				sender_id;
	struct c2_rpc_slot_table_key		key;
	struct c2_rpc_inmem_slot_table_value	inmem_value;
	struct c2_db_pair			inmem_pair;
	struct c2_db_tx				*tx;
	struct c2_cob_domain			*dom;
	struct c2_cob				*conn_cob = NULL;
	struct c2_cob				*session0_cob = NULL;
	struct c2_cob				*slot0_cob = NULL;
	int					rc;

	fom_cc = container_of(fom, struct c2_rpc_fom_conn_create, fcc_gen);

	printf("Called conn_create_state\n");
	C2_PRE(fom != NULL && fom_cc != NULL && fom_cc->fcc_fop != NULL &&
			fom_cc->fcc_fop_rep != NULL &&
			fom_cc->fcc_dbenv != NULL);

	fop = fom_cc->fcc_fop;
	fop_in = c2_fop_data(fop);

	C2_ASSERT(fop_in != NULL);

	fop_rep = fom_cc->fcc_fop_rep;
	fop_out = c2_fop_data(fop_rep);

	C2_ASSERT(fop_out != NULL);

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL && item->ri_mach != NULL);

	machine = item->ri_mach;

	dom = machine->cr_rcache.rc_dom;
	C2_ASSERT(dom != NULL);

	tx = &fom_cc->fcc_tx;
	printf("Cookie = %lx\n", fop_in->rcc_cookie);

	/*
	 * XXX Decide how to calculate sender_id
	 */
retry:
	sender_id = c2_rpc_sender_id_get();

	/*
	 * Create entry for session0/slot0 in in core
	 * slot table
	 */
	key.stk_sender_id = sender_id;
	key.stk_session_id = SESSION_0;
	key.stk_slot_id = 0;
	key.stk_slot_generation = 0;

	inmem_value.istv_busy = false;

	c2_db_pair_setup(&inmem_pair, machine->cr_rcache.rc_inmem_slot_table,
				&key, sizeof key,
				&inmem_value, sizeof inmem_value);

	rc = c2_table_insert(tx, &inmem_pair);

	c2_db_pair_release(&inmem_pair);
	c2_db_pair_fini(&inmem_pair);

	if (rc == -EEXIST) {
		goto retry;
	}
	if (rc != 0)  {
		printf("conn_create_state: error while inserting record\n");
		goto errout;
	}

	C2_ASSERT(rc == 0);

	rc = c2_rpc_rcv_conn_create(dom, sender_id, &conn_cob, tx);
	if (rc != 0) {
		printf("Error during conn_create() %d\n", rc);
		goto errout;
	}
	rc = c2_rpc_rcv_session_create(conn_cob, SESSION_0, &session0_cob, tx);
	if (rc != 0) {
		printf("Error during session_create() %d\n", rc);
		c2_cob_put(conn_cob);
		goto errout;
	}

	rc = c2_rpc_rcv_slot_create(session0_cob,
					0,	/* Slot id */
					0,	/* slot generation */
					&slot0_cob, tx);
	if (rc != 0) {
		printf("Error during slot_create() %d\n", rc);
		c2_cob_put(session0_cob);
		c2_cob_put(conn_cob);
		goto errout;
	}

	C2_ASSERT(rc == 0);

	c2_cob_put(slot0_cob);
	c2_cob_put(session0_cob);
	c2_cob_put(conn_cob);

	fop_out->rccr_snd_id = sender_id;
	fop_out->rccr_rc = 0;		/* successful */
	fop_out->rccr_cookie = fop_in->rcc_cookie; 

	printf("conn_create_state: conn created\n");
	fom->fo_phase = FOPH_DONE;
	return FSO_AGAIN;

errout:
	C2_ASSERT(rc != 0);

	printf("conn_create_state: failed %d\n", rc);
	fop_out->rccr_snd_id = SENDER_ID_INVALID;
	fop_out->rccr_rc = rc;
	fop_out->rccr_cookie = fop_in->rcc_cookie;

	fom->fo_phase = FOPH_FAILED;

	return FSO_AGAIN;
}
void c2_rpc_fom_conn_create_fini(struct c2_fom *fom)
{
	printf("called conn_create_fini\n");
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
	struct c2_rpc_item			*item;
	struct c2_rpcmachine			*machine;
	struct c2_fop				*fop;
	struct c2_fop				*fop_rep;
	struct c2_rpc_session_create		*fop_in;
	struct c2_rpc_session_create_rep	*fop_out;
	struct c2_rpc_fom_session_create	*fom_sc;
	uint64_t				session_id;
	uint64_t				sender_id;
	struct c2_rpc_slot_table_key		key;
	struct c2_rpc_inmem_slot_table_value	inmem_value;
	struct c2_db_pair			inmem_pair;
	struct c2_db_tx				*tx;
	struct c2_cob_domain			*dom;
	struct c2_cob				*conn_cob = NULL;
	struct c2_cob				*session_cob = NULL;
	struct c2_cob				*slot_cob = NULL;
	int					rc;
	int					i;

	fom_sc = container_of(fom, struct c2_rpc_fom_session_create, fsc_gen);

	printf("Called session_create_state\n");
	C2_PRE(fom != NULL && fom_sc != NULL && fom_sc->fsc_fop != NULL &&
			fom_sc->fsc_fop_rep != NULL &&
			fom_sc->fsc_dbenv != NULL); 

	fop = fom_sc->fsc_fop;
	fop_in = c2_fop_data(fop);

	C2_ASSERT(fop_in != NULL);

	fop_rep = fom_sc->fsc_fop_rep;
	fop_out = c2_fop_data(fop_rep);

	C2_ASSERT(fop_out != NULL);

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL && item->ri_mach != NULL);

	machine = item->ri_mach;

	tx = &fom_sc->fsc_tx;

	dom = machine->cr_rcache.rc_dom;
	C2_ASSERT(dom != NULL);

	/*
	 * XXX Decide how to calculate session_id
	 */
retry:
	session_id = c2_rpc_session_id_get();
	sender_id = fop_in->rsc_snd_id;
	fop_out->rscr_sender_id = fop_in->rsc_snd_id;

	/*
	 * Create entry for session0/slot0
	 */
	key.stk_sender_id = sender_id;
	key.stk_session_id = session_id;
	key.stk_slot_generation = 0;

	inmem_value.istv_busy = false;

	c2_db_pair_setup(&inmem_pair, machine->cr_rcache.rc_inmem_slot_table,
				&key, sizeof key,
				&inmem_value, sizeof inmem_value);

	for (i = 0; i < DEFAULT_SLOT_COUNT; i++) {
		key.stk_slot_id = i;

		rc = c2_table_insert(&fom_sc->fsc_tx, &inmem_pair);
		if (rc == -EEXIST) {
			c2_db_pair_release(&inmem_pair);
			c2_db_pair_fini(&inmem_pair);
			goto retry;
		}
		if (rc != 0) {
			printf("sesssion_create_state: error while inserting record 1\n");
			goto errout;
		}
	}

	c2_db_pair_release(&inmem_pair);
	c2_db_pair_fini(&inmem_pair);

	C2_ASSERT(rc == 0);

	rc = c2_rpc_rcv_conn_lookup(dom, sender_id, &conn_cob, tx);
	if (rc != 0) {
		printf("session_create: failed to lookup conn %lu [%d]",
				sender_id, rc);
		goto errout;
	}

	rc = c2_rpc_rcv_session_create(conn_cob, session_id, &session_cob, tx);
	if (rc != 0) {
		printf("session_create: Failed to create session cob %d\n", rc);
		c2_cob_put(conn_cob);
		goto errout;
	}

	for (i = 0; i < DEFAULT_SLOT_COUNT; i++) {
		rc = c2_rpc_rcv_slot_create(session_cob, i, 0, &slot_cob, tx);
		if (rc != 0) {
			printf("session_create: Failed to create slot cob %d %d\n",
					i, rc);
			c2_cob_put(session_cob);
			c2_cob_put(conn_cob);
		}
		c2_cob_put(slot_cob);
	}

	C2_ASSERT(rc == 0);

	fop_out->rscr_rc = 0; 		/* success */
	fop_out->rscr_session_id = session_id;
	fom->fo_phase = FOPH_DONE;
	printf("Session create finished\n");
	return FSO_AGAIN;

errout:
	C2_ASSERT(rc != 0);

	printf("session_create: failed %d\n", rc);

	fop_out->rscr_rc = rc;
	fop_out->rscr_session_id = SESSION_ID_INVALID;

	fom->fo_phase = FOPH_FAILED;
	return FSO_AGAIN;
}
void c2_rpc_fom_session_create_fini(struct c2_fom *fom)
{
	printf("called session_create_fini\n");
}

/*
 * FOM session destroy
 */

struct c2_fom_ops c2_rpc_fom_session_destroy_ops = {
	.fo_fini = &c2_rpc_fom_session_destroy_fini,
	.fo_state = &c2_rpc_fom_session_destroy_state
};

static struct c2_fom_type_ops c2_rpc_fom_session_destroy_type_ops = {
	.fto_create = NULL
};

struct c2_fom_type c2_rpc_fom_session_destroy_type = {
	.ft_ops = &c2_rpc_fom_session_destroy_type_ops
};

int c2_rpc_fom_session_destroy_state(struct c2_fom *fom)
{
	struct c2_rpcmachine			*machine;
	struct c2_rpc_session_destroy		*fop_in;
	struct c2_rpc_session_destroy_rep	*fop_out;
	struct c2_rpc_fom_session_destroy	*fom_sd;
	struct c2_rpc_slot_table_key		key;
	struct c2_rpc_inmem_slot_table_value	inmem_slot;
	struct c2_db_pair			pair;
	struct c2_db_cursor			cursor;
	struct c2_db_tx				*tx;
	struct c2_rpc_item			*item;
	struct c2_cob_domain			*dom;
	struct c2_cob				*conn_cob;
	struct c2_cob				*session_cob;
	struct c2_cob				*slot_cob;
	uint64_t				sender_id;
	uint64_t				session_id;
	int					count = 0;
	int					i;
	int					rc;

	printf("Session destroy state called\n");
	fom_sd = container_of(fom, struct c2_rpc_fom_session_destroy, fsd_gen);

	C2_ASSERT(fom != NULL && fom_sd != NULL && fom_sd->fsd_fop != NULL &&
			fom_sd->fsd_fop_rep != NULL &&
			fom_sd->fsd_dbenv != NULL);

	fop_in = c2_fop_data(fom_sd->fsd_fop);

	C2_ASSERT(fop_in != NULL);

	fop_out = c2_fop_data(fom_sd->fsd_fop_rep);

	C2_ASSERT(fop_out != NULL);

	item = c2_fop_to_rpc_item(fom_sd->fsd_fop);
	C2_ASSERT(item != NULL && item->ri_mach != NULL);

	machine = item->ri_mach;

	tx = &fom_sd->fsd_tx;
	dom = machine->cr_rcache.rc_dom;
	sender_id = fop_in->rsd_sender_id;
	session_id = fop_in->rsd_session_id;

	fop_out->rsdr_sender_id = sender_id;
	fop_out->rsdr_session_id = session_id;

	C2_ASSERT(tx != NULL);

	printf("destroy [%lu:%lu]\n", fop_in->rsd_sender_id,
			fop_in->rsd_session_id);

	/*
	 * Create key <sender_id, session_id>
	 */
	key.stk_sender_id = fop_in->rsd_sender_id;
	key.stk_session_id = fop_in->rsd_session_id;
	key.stk_slot_id = 0;
	key.stk_slot_generation = 0;

	/*
	 * Delete entries from in-core slot table.
	 * reuse variables pair and cursor to traverse in in-memory slot_table
	 */

	c2_db_pair_setup(&pair, machine->cr_rcache.rc_inmem_slot_table,
				&key, sizeof key,
				&inmem_slot, sizeof inmem_slot);

	rc = c2_db_cursor_init(&cursor, machine->cr_rcache.rc_inmem_slot_table, tx);
	if (rc != 0)
		goto errout;

	while ((rc = c2_db_cursor_get(&cursor, &pair)) == 0) {
		printf("Key [%lu:%lu:%u]\n", key.stk_sender_id,
			key.stk_session_id, key.stk_slot_id);
		if (key.stk_sender_id == fop_in->rsd_sender_id &&
			key.stk_session_id == fop_in->rsd_session_id) {
			rc = c2_db_cursor_del(&cursor);
			count++;
			if (rc != 0)
				goto errout;

			printf("Deleted in mem rec\n");
		} else {
			printf("Finished records in in-mem slot table\n");
			break;
		}
	}

	/*
	 * If we've ran out of records then the above loop is 
	 * complete without any error
	 */
	if (rc == DB_NOTFOUND || rc == -ENOENT)
		rc = 0;

	if (rc != 0)
		goto errout;

	/*
	 * If session with @session_id is not present then we must not
	 * have deleted any record from slot table
	 */
	if (count == 0) {
		rc = -ENOENT;
		goto errout;
	}

	C2_ASSERT(count > 0);
	c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
	c2_db_cursor_fini(&cursor);
	
	rc = c2_rpc_rcv_conn_lookup(dom, sender_id, &conn_cob, tx);
	if (rc != 0) {
		printf("session_destroy: failed to lookup conn %lu %d\n",
				sender_id, rc);
		goto errout;
	}

	rc = c2_rpc_rcv_session_lookup(conn_cob, session_id, &session_cob, tx);
	if (rc != 0) {
		printf("session_destroy: failed to lookup session %lu %d\n",
				session_id, rc);
		c2_cob_put(conn_cob);
	}

	for (i = 0; i < DEFAULT_SLOT_COUNT; i++) {
		rc = c2_rpc_rcv_slot_lookup(session_cob, i, 0, &slot_cob, tx);
		if (rc != 0) {
			printf("session_destroy: failed to lookup slot %d %d\n",
					i, rc);
			c2_cob_put(session_cob);
			c2_cob_put(conn_cob);
			goto errout;
		}
		rc = c2_cob_delete(slot_cob, tx);
		if (rc != 0) {
			printf("session_destroy: failed to delete slot %d %d\n",
					i, rc);
			c2_cob_put(session_cob);
			c2_cob_put(conn_cob);
			goto errout;
		}
		slot_cob = NULL;
	}
	rc = c2_cob_delete(session_cob, tx);
	if (rc != 0) {
		printf("session_destroy: failed to delete session cob %lu\n",
				session_id);
		c2_cob_put(session_cob);
		c2_cob_put(conn_cob);
		goto errout;
	}
	printf("session_destroy: all cobs related to session %lu deleted\n",
			session_id);
	c2_list_for_each_entry(&machine->cr_rcache.rc_item_list, item,
				struct c2_rpc_item, ri_rc_link) {
		if (item->ri_sender_id == fop_in->rsd_sender_id &&
			item->ri_session_id == fop_in->rsd_session_id) {
			c2_list_del(&item->ri_rc_link);
			printf("session_destroy_state: removed entry from reply cache\n");
		}
	}

	C2_ASSERT(rc == 0);

	fop_out->rsdr_rc = 0;	/* Report success */
	fom->fo_phase = FOPH_DONE;

	printf("Session destroy successful\n");
	return FSO_AGAIN;

errout:
	C2_ASSERT(rc != 0);

	c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
	c2_db_cursor_fini(&cursor);

	fop_out->rsdr_rc = rc;	/* Report failure */
	fom->fo_phase = FOPH_FAILED;
	printf("session destroy failed %d\n", rc);
	return FSO_AGAIN;
}

void c2_rpc_fom_session_destroy_fini(struct c2_fom *fom)
{
	printf("Session destroy fini called\n");
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
	struct c2_rpcmachine			*machine;
	struct c2_fop				*fop;
	struct c2_rpc_conn_terminate		*fop_in;
	struct c2_fop				*fop_rep;
	struct c2_rpc_conn_terminate_rep	*fop_out;
	struct c2_rpc_fom_conn_terminate	*fom_ct;
	struct c2_rpc_slot_table_key		key;
	struct c2_rpc_inmem_slot_table_value	inmem_slot;
	struct c2_db_pair			pair;
	struct c2_db_tx				*tx;
	struct c2_db_cursor			cursor;
	struct c2_cob_domain			*dom;
	struct c2_cob				*conn_cob;
	struct c2_cob				*session0_cob;
	struct c2_cob				*slot0_cob;
	int					rc;
	uint64_t				sender_id;

	printf("conn terminate state called\n");

	C2_ASSERT(fom != NULL);

	fom_ct = container_of(fom, struct c2_rpc_fom_conn_terminate, fct_gen);

	C2_ASSERT(fom_ct != NULL && fom_ct->fct_fop != NULL &&
			fom_ct->fct_fop_rep != NULL &&
			fom_ct->fct_dbenv != NULL);

	fop = fom_ct->fct_fop;
	fop_in = c2_fop_data(fop);
	
	C2_ASSERT(fop_in != NULL);

	fop_rep = fom_ct->fct_fop_rep;
	fop_out = c2_fop_data(fop);

	C2_ASSERT(fop_out != NULL);

	sender_id = fop_in->ct_sender_id;
	fop_out->ctr_sender_id = sender_id;

	printf("conn terminate request: %lu\n", fop_in->ct_sender_id);

	C2_ASSERT(sender_id != SENDER_ID_INVALID);

	item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(item != NULL && item->ri_mach != NULL);

	machine = item->ri_mach;
	tx = &fom_ct->fct_tx;
	dom = machine->cr_rcache.rc_dom;

	/*
	 * prepare key, value and pair
	 */
	key.stk_sender_id = sender_id;
	key.stk_session_id = SESSION_0;
	key.stk_slot_id = 0;
	key.stk_slot_generation = 0;

	C2_SET0(&inmem_slot);

	c2_db_pair_setup(&pair, machine->cr_rcache.rc_inmem_slot_table,
				&key, sizeof key,
				&inmem_slot, sizeof inmem_slot);

	rc = c2_db_cursor_init(&cursor, machine->cr_rcache.rc_inmem_slot_table, tx);
	if (rc != 0)
		goto errout;

	rc = c2_db_cursor_get(&cursor, &pair);
	if (rc != 0)
		goto errout;

	if (key.stk_sender_id != sender_id) {
		rc = -ENOENT;
		goto errout;
	}

	rc = c2_db_cursor_del(&cursor);
	if (rc != 0)
		goto errout;

	/*
	 * Check is there any other record having same sender_id
	 */
	rc = c2_db_cursor_get(&cursor, &pair);
	if (rc != 0 && rc != DB_NOTFOUND && rc != -ENOENT)
		goto errout;

	if (rc == 0) {
		if (key.stk_sender_id == sender_id) {
			printf("Can't terminate conn. session [%lu]\n",
					key.stk_session_id);
			rc = -EBUSY;
			goto errout;
		}
	}
	c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
	c2_db_cursor_fini(&cursor);

	rc = c2_rpc_rcv_conn_lookup(dom, sender_id, &conn_cob, tx);
	if (rc != 0) {
		printf("conn_term: failed to lookup sender_id %lu %d\n",
				sender_id, rc);
		goto errout;
	}

	rc = c2_rpc_rcv_session_lookup(conn_cob, SESSION_0, &session0_cob, tx);
	if (rc != 0) {
		printf("conn_term: failed to lookup session 0 %d\n", rc);
		c2_cob_put(conn_cob);
		goto errout;
	}

	rc = c2_rpc_rcv_slot_lookup(session0_cob, 0, 0, &slot0_cob, tx);
	if (rc != 0) {
		printf("conn_term: failed to lookup slot 0 %d\n", rc);
		c2_cob_put(session0_cob);
		c2_cob_put(conn_cob);
		goto errout;
	}

	rc = c2_cob_delete(slot0_cob, tx);
	if (rc != 0) {
		c2_cob_put(slot0_cob);
		c2_cob_put(session0_cob);
		c2_cob_put(conn_cob);
		goto errout;
	}

	rc = c2_cob_delete(session0_cob, tx);
	if (rc != 0) {
		c2_cob_put(session0_cob);
		c2_cob_put(conn_cob);
		goto errout;
	}
	
	rc = c2_cob_delete(conn_cob, tx);
	if (rc != 0) {
		c2_cob_put(conn_cob);
		goto errout;
	}

	C2_ASSERT(rc == 0);

	printf("Conn terminate successful\n");
	fop_out->ctr_rc = 0;	/* Success */

	fom->fo_phase = FOPH_DONE;
	return FSO_AGAIN;

errout:
	C2_ASSERT(rc != 0);

	printf("Conn terminate failed rc %d\n", rc);
	c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);
	c2_db_cursor_fini(&cursor);

	fop_out->ctr_rc = rc;
	fom->fo_phase = FOPH_FAILED;
	return FSO_AGAIN;
}

void c2_rpc_fom_conn_terminate_fini(struct c2_fom *fom)
{
	printf("conn create fini called\n");
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

