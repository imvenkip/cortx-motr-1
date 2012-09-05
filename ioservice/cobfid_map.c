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
 * Original author: Carl Braganza <Carl_Braganza@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 08/23/2011
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/stat.h>
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"  /* SET0 */
#include "lib/arith.h" /* C2_3WAY */
#include "ioservice/cobfid_map.h"
#include "colibri/colibri_setup.h"
#include "lib/refs.h"
#include "reqh/reqh.h"

/**
 * @addtogroup cobfidmap
 * @{
 */

enum {
	CFM_MAP_MAGIC  = 0x6d4d4643000a7061,
	CFM_ITER_MAGIC = 0x694d46430a726574,
	CFM_ITER_THUNK = 16 /* #records in an iter buffer */
};

/**
  Internal data structure used as a key for the record in cobfid_map table
 */
struct cobfid_map_key {
	uint64_t	cfk_ci;  /**< container id */
	struct c2_fid	cfk_fid; /**< global file id */
};

/**
  Internal data structure used to store a record in the iterator buffer.
 */
struct cobfid_map_record {
	/** key combining container id and global file id */
	struct cobfid_map_key	cfr_key;
	/**  cob id */
	struct c2_uint128	cfr_cob;
};

/**
  Compare the cobfid_map table keys
 */
static int cfm_key_cmp(struct c2_table *table, const void *key0,
		       const void *key1)
{
	const struct cobfid_map_key *map_key0 = key0;
	const struct cobfid_map_key *map_key1 = key1;

	C2_PRE(table != NULL);
	C2_PRE(key0 != NULL);
	C2_PRE(key1 != NULL);

	if (c2_fid_eq(&map_key0->cfk_fid, &map_key1->cfk_fid) &&
		      C2_3WAY(map_key0->cfk_ci, map_key1->cfk_ci) == 0)
		return 0;
	else if (C2_3WAY(map_key0->cfk_ci, map_key1->cfk_ci) == 0)
		return map_key0->cfk_fid.f_key - map_key1->cfk_fid.f_key;
	else
		return map_key0->cfk_ci - map_key1->cfk_ci;
}

static const struct c2_table_ops cfm_table_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(struct cobfid_map_key)
		},
		[TO_REC] = {
			.max_size = sizeof(struct c2_uint128)
		}
	},
	.key_cmp = cfm_key_cmp
};

/** ADDB instrumentation for cobfidmap */
static const struct c2_addb_ctx_type cfm_ctx_type = {
	        .act_name = "cobfid_map"
};

static const struct c2_addb_loc cfm_addb_loc = {
	        .al_name = "cobfid_map"
};

C2_ADDB_EV_DEFINE(cfm_func_fail, "cobfid_map_func_fail",
		  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

/*
 *****************************************************************************
 struct c2_cobfid_map
 *****************************************************************************
 */

/**
   Invariant for the c2_cobfid_map.
 */
static bool cobfid_map_invariant(const struct c2_cobfid_map *cfm)
{
	if (cfm == NULL || cfm->cfm_magic != CFM_MAP_MAGIC ||
	    !cfm->cfm_is_initialised)
		return false;
	return true;
}

int c2_cobfid_map_init(struct c2_cobfid_map *cfm, struct c2_dbenv *db_env,
		       struct c2_addb_ctx *addb_ctx, const char *map_name)
{
	int rc;

	C2_PRE(cfm != NULL);
	C2_PRE(db_env != NULL);
	C2_PRE(map_name != NULL);

	C2_SET0(cfm);
	cfm->cfm_dbenv = db_env;
	cfm->cfm_addb  = addb_ctx;

	C2_ALLOC_ARR(cfm->cfm_map_name, strlen(map_name) + 1);
	if (cfm->cfm_map_name == NULL) {
		C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, c2_addb_oom);
		c2_table_fini(&cfm->cfm_table);
		return -ENOMEM;
	}

	strcpy(cfm->cfm_map_name, map_name);

	rc = c2_table_init(&cfm->cfm_table, cfm->cfm_dbenv, cfm->cfm_map_name,
			   0, &cfm_table_ops);
	if (rc != 0) {
		C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
			    "c2_table_init", rc);
		c2_free(cfm->cfm_map_name);
		return rc;
	}

	cfm->cfm_last_mod = c2_time_now();
	cfm->cfm_magic = CFM_MAP_MAGIC;
	c2_mutex_init(&cfm->cfm_mutex);
	cfm->cfm_is_initialised = true;
	C2_POST(cobfid_map_invariant(cfm));
	return 0;
}

void c2_cobfid_map_fini(struct c2_cobfid_map *cfm)
{
	C2_PRE(cobfid_map_invariant(cfm));

	c2_addb_ctx_fini(cfm->cfm_addb);
	c2_table_fini(&cfm->cfm_table);
	c2_free(cfm->cfm_map_name);
	c2_mutex_fini(&cfm->cfm_mutex);
}

int c2_cobfid_map_add(struct c2_cobfid_map *cfm, const uint64_t container_id,
		      const struct c2_fid file_fid, struct c2_uint128 cob_fid)
{
	int			 rc;
	bool			 table_update_failed = false;
	struct c2_db_pair	 db_pair;
	struct cobfid_map_key	 key;
	struct c2_db_tx		 tx;

	C2_PRE(cobfid_map_invariant(cfm));

	key.cfk_ci = container_id;
	key.cfk_fid = file_fid;

	rc = c2_db_tx_init(&tx, cfm->cfm_dbenv, 0);
	if (rc != 0) {
		C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
			    "c2_db_tx_init", rc);
		return rc;
	}

	c2_db_pair_setup(&db_pair, &cfm->cfm_table, &key,
			 sizeof(struct cobfid_map_key),
			 &cob_fid, sizeof(struct c2_uint128));

	rc = c2_table_update(&tx, &db_pair);
	if (rc != 0) {
		table_update_failed = true;
		C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
			    "c2_table_update", rc);
	}

	c2_db_pair_release(&db_pair);
	c2_db_pair_fini(&db_pair);
	if (!table_update_failed) {
		cfm->cfm_last_mod = c2_time_now();
		c2_db_tx_commit(&tx);
	} else
		c2_db_tx_abort(&tx);

	C2_POST(cobfid_map_invariant(cfm));

	return rc;
}

int c2_cobfid_map_del(struct c2_cobfid_map *cfm, const uint64_t container_id,
		      const struct c2_fid file_fid)
{
	int			 rc;
	struct c2_db_pair	 db_pair;
	struct cobfid_map_key	 key;
	bool			 table_op_failed = false;
	struct c2_db_tx		 tx;

	C2_PRE(cobfid_map_invariant(cfm));

	rc = c2_db_tx_init(&tx, cfm->cfm_dbenv, 0);
	if (rc != 0) {
		C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
			    "c2_db_tx_init", rc);
		return rc;
	}

	key.cfk_ci = container_id;
	key.cfk_fid = file_fid;

	c2_db_pair_setup(&db_pair, &cfm->cfm_table, &key,
			 sizeof(struct cobfid_map_key), NULL, 0);

	rc = c2_table_delete(&tx, &db_pair);
	if (rc != 0) {
		C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
			    "c2_table_delete", rc);
		table_op_failed = true;
	}

	c2_db_pair_release(&db_pair);
	c2_db_pair_fini(&db_pair);

	if (!table_op_failed) {
		cfm->cfm_last_mod = c2_time_now();
		c2_db_tx_commit(&tx);
	} else
		c2_db_tx_abort(&tx);

	C2_POST(cobfid_map_invariant(cfm));

	return rc;
}

/*
 *****************************************************************************
 struct c2_cobfid_map_iter
 *****************************************************************************
 */

/**
   Invariant for the c2_cobfid_map_iter.
 */
static bool cobfid_map_iter_invariant(const struct c2_cobfid_map_iter *iter)
{
	if (iter == NULL || iter->cfmi_magic != CFM_ITER_MAGIC)
		return false;
	if (!cobfid_map_invariant(iter->cfmi_cfm))
		return false;
	if (iter->cfmi_qt == 0 || iter->cfmi_qt >= C2_COBFID_MAP_QT_NR)
		return false;
	if (iter->cfmi_ops == NULL ||
	    iter->cfmi_ops->cfmio_fetch == NULL ||
	    iter->cfmi_ops->cfmio_is_at_end == NULL ||
	    iter->cfmi_ops->cfmio_reload == NULL)
		return false;
	 /* Number of records (cfmi_num_recs is set to the buffer size and
	    will be held constant for the life of the iterator */
	if (iter->cfmi_num_recs == 0)
		return false;
	/* Last record (cfmi_last_rec field) cannot exceed the count of
	   the number of records. Both fields are 0 based */
	if (iter->cfmi_last_rec > iter->cfmi_num_recs)
		return false;
	/* A valid index into records in the buffer is in the range
	   [0, cfmi_last_rec]. The "next" index (cfmi_rec_idx) should also be
	   allowed to both identify a valid record, and to point beyond the
	   valid range to indicate that the buffer is exhausted,
	   which makes the valid range of the "next" index
	   [0, (cfmi_last_rec + 1)]. */
	if (iter->cfmi_rec_idx > iter->cfmi_last_rec + 1)
		return false;
	if (iter->cfmi_buffer == NULL)
		return false;
	return true;
}

void c2_cobfid_map_iter_fini(struct c2_cobfid_map_iter *iter)
{
	C2_PRE(cobfid_map_iter_invariant(iter));

	c2_free(iter->cfmi_buffer);
	iter->cfmi_magic = 0;

	C2_POST(!cobfid_map_iter_invariant(iter));
}

/**
   Internal sub to initialize an iterator.
 */
static int cobfid_map_iter_init(struct c2_cobfid_map *cfm,
				struct c2_cobfid_map_iter *iter,
				const struct c2_cobfid_map_iter_ops *ops,
				const enum c2_cobfid_map_query_type qt)
{

	C2_PRE(iter != NULL);
	C2_PRE(ops != NULL);
	C2_PRE(cobfid_map_invariant(cfm));

	C2_SET0(iter);

	/* allocate buffer */
	C2_ALLOC_ARR(iter->cfmi_buffer, CFM_ITER_THUNK *
		     sizeof(struct cobfid_map_record));
	if (iter->cfmi_buffer == NULL) {
                C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, c2_addb_oom);
		return -ENOMEM;
	}

	iter->cfmi_cfm = cfm;
	iter->cfmi_ops = ops;
	iter->cfmi_qt = qt;
	iter->cfmi_num_recs = CFM_ITER_THUNK;
	iter->cfmi_end_of_table = false;
	iter->cfmi_reload = false;

	/* force a query by positioning at the end */
	iter->cfmi_rec_idx = iter->cfmi_last_rec + 1;
	iter->cfmi_magic = CFM_ITER_MAGIC;

	C2_POST(cobfid_map_iter_invariant(iter));
	C2_POST(iter->cfmi_error == 0);
	return 0;
}

int c2_cobfid_map_iter_next(struct  c2_cobfid_map_iter *iter,
			    uint64_t *container_id_p, struct c2_fid *file_fid_p,
			    struct c2_uint128 *cob_fid_p)
{
	int			  rc;
	struct cobfid_map_record *recs;
	struct cobfid_map_record *record;

	C2_PRE(cobfid_map_iter_invariant(iter));
	C2_PRE(container_id_p != NULL);
	C2_PRE(file_fid_p != NULL);
	C2_PRE(cob_fid_p != NULL);

	recs = iter->cfmi_buffer;

	/* already in error */
	if (iter->cfmi_error != 0)
		return iter->cfmi_error;

	if (iter->cfmi_rec_idx >= iter->cfmi_last_rec) {
		iter->cfmi_rec_idx = 0;
		/* buffer empty: fetch records into the buffer
		   and then set cfmi_rec_idx to 0 */
		rc = iter->cfmi_ops->cfmio_fetch(iter);
		if (rc != 0){
			C2_ASSERT(iter->cfmi_error != 0);
			C2_ADDB_ADD(iter->cfmi_cfm->cfm_addb, &cfm_addb_loc,
				    cfm_func_fail, "cfmio_fetch", rc);
			return rc;
		}
		iter->cfmi_last_load = c2_time_now();
	} else if (iter->cfmi_last_load < iter->cfmi_cfm->cfm_last_mod) {
		/* iterator stale: reload the buffer and reset buffer index */
		iter->cfmi_rec_idx = 0;
		iter->cfmi_reload = true;
		rc = iter->cfmi_ops->cfmio_reload(iter);
		if (rc != 0){
			C2_ASSERT(iter->cfmi_error != 0);
			C2_ADDB_ADD(iter->cfmi_cfm->cfm_addb, &cfm_addb_loc,
				    cfm_func_fail, "cfmio_reload", rc);
			return rc;
		}
		iter->cfmi_last_load = c2_time_now();
	}

	record = &recs[iter->cfmi_rec_idx];
	/* Set output pointer values */
	*container_id_p = record->cfr_key.cfk_ci;
	file_fid_p->f_container = record->cfr_key.cfk_fid.f_container;
	file_fid_p->f_key = record->cfr_key.cfk_fid.f_key;
	cob_fid_p->u_hi = record->cfr_cob.u_hi;
	cob_fid_p->u_lo = record->cfr_cob.u_lo;

	iter->cfmi_last_ci = record->cfr_key.cfk_ci;
	iter->cfmi_last_fid = record->cfr_key.cfk_fid;

	/* Increment the next record to return */
	iter->cfmi_rec_idx++;

	/* Check if current record exhausts the iterator.
	   There is an implicit assumption here that the operations fail
	   and set cfmi_error when there are no more records left in the
	   database */
	if (iter->cfmi_ops->cfmio_is_at_end(iter, iter->cfmi_rec_idx - 1 ))
		iter->cfmi_error = -ENOENT;

	C2_POST(cobfid_map_iter_invariant(iter));

	return 0;
}

/*
 *****************************************************************************
 Enumerate map
 *****************************************************************************
 */

static int enum_fetch(struct c2_cobfid_map_iter *iter)
{
	int				 rc;
	int				 i;
	bool				 last_key_reached = false;
	struct c2_db_pair		 db_pair;
	struct c2_cobfid_map		*cfm;
	struct cobfid_map_key		 key;
	struct cobfid_map_key		 last_key;
	struct c2_uint128		 cob_fid;
	struct cobfid_map_record	*recs;
	struct c2_db_cursor		 db_cursor;
	struct c2_db_tx			 tx;

	C2_PRE(cobfid_map_iter_invariant(iter));

	recs = iter->cfmi_buffer;

	cfm = iter->cfmi_cfm;

	rc = c2_db_tx_init(&tx, cfm->cfm_dbenv, 0);
	if (rc != 0) {
		C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
			    "c2_db_tx_init", rc);
		return rc;
	}

	rc = c2_db_cursor_init(&db_cursor, &cfm->cfm_table, &tx, 0);
	if (rc != 0) {
		C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
			    "c2_db_cursor_init", rc);
		c2_db_tx_abort(&tx);
		return rc;
	}

	/* Store the last key, to check if there is overrun during iterating
	   the table */
	c2_db_pair_setup(&db_pair, &cfm->cfm_table, &last_key,
			 sizeof(struct cobfid_map_key),
			 &cob_fid, sizeof(struct c2_uint128));

	rc = c2_db_cursor_last(&db_cursor, &db_pair);
	if (rc != 0) {
		C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
			    "c2_db_cursor_last", rc);
		goto cleanup;
	}

	c2_db_pair_release(&db_pair);

	key.cfk_ci = iter->cfmi_next_ci;
	key.cfk_fid = iter->cfmi_next_fid;

	c2_db_pair_setup(&db_pair, &cfm->cfm_table, &key,
			 sizeof(struct cobfid_map_key),
			 &cob_fid, sizeof(struct c2_uint128));

	rc = c2_db_cursor_get(&db_cursor, &db_pair);
	if (rc != 0) {
		C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
			    "c2_db_cursor_get", rc);
		goto cleanup;
	}

	/* Fetch next entry while reloading the iterator. This is needed as
	   last entries in the iterator are equated to next entries */
	if (iter->cfmi_reload) {
		rc = c2_db_cursor_next(&db_cursor, &db_pair);
		if (rc != 0) {
			C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
				    "c2_db_cursor_next", rc);
			goto cleanup;
		}
		iter->cfmi_reload = false;
	}

	iter->cfmi_last_rec = 0;

	for (i = 0; i < iter->cfmi_num_recs; ++i) {
		/* Transaction should be committed even if records get exhausted
		   from the table and not all CFM_ITER_THUNK entries are
		   fetched. Iterator will be loaded with remaining records */
		if (cfm_key_cmp(&cfm->cfm_table, &last_key, &key) == 0)
			last_key_reached = true;

		recs[i].cfr_key.cfk_ci = key.cfk_ci;
		recs[i].cfr_key.cfk_fid = key.cfk_fid;
		recs[i].cfr_cob = cob_fid;
		iter->cfmi_last_rec++;

		if (last_key_reached) {
			iter->cfmi_end_of_table = true;
			break;
		}

		c2_db_pair_setup(&db_pair, &cfm->cfm_table, &key,
				 sizeof(struct cobfid_map_key),
				 &cob_fid, sizeof(struct c2_uint128));

		/* The call c2_db_cursor_next() breaks if one tries to go
		   beyond the last record, hence last_key_reached check
		   is needed */
		rc = c2_db_cursor_next(&db_cursor, &db_pair);
		if (rc != 0) {
			C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
				    "c2_db_cursor_next", rc);
			goto cleanup;
		}
	}

cleanup:
	c2_db_pair_release(&db_pair);
	c2_db_pair_fini(&db_pair);
	c2_db_cursor_fini(&db_cursor);

	if (rc == 0) {
		iter->cfmi_next_ci = key.cfk_ci;
		iter->cfmi_next_fid = key.cfk_fid;
		c2_db_tx_commit(&tx);
	} else {
		iter->cfmi_error = rc;
		c2_db_tx_abort(&tx);
	}

	return rc;
}

static bool enum_is_at_end(struct c2_cobfid_map_iter *iter,
			   const unsigned int idx)
{
	struct cobfid_map_record *recs;

	C2_PRE(cobfid_map_iter_invariant(iter));

	recs = iter->cfmi_buffer;

	if (iter->cfmi_end_of_table &&
	    iter->cfmi_last_rec == iter->cfmi_rec_idx)
		return true;

	if (idx < CFM_ITER_THUNK - 1 &&
	    recs[idx].cfr_key.cfk_ci == recs[idx + 1].cfr_key.cfk_ci)
		return false;

	return false;
}

static int enum_reload(struct c2_cobfid_map_iter *iter)
{
	C2_PRE(cobfid_map_iter_invariant(iter));

	iter->cfmi_next_ci = iter->cfmi_last_ci;
	iter->cfmi_next_fid = iter->cfmi_last_fid;

	return iter->cfmi_ops->cfmio_fetch(iter);
}

static const struct c2_cobfid_map_iter_ops enum_ops = {
	.cfmio_fetch  = enum_fetch,
	.cfmio_is_at_end = enum_is_at_end,
	.cfmio_reload = enum_reload,
};

int c2_cobfid_map_enum(struct c2_cobfid_map *cfm,
		       struct c2_cobfid_map_iter *iter)
{
	int rc;

	C2_PRE(cobfid_map_invariant(cfm));

	rc = cobfid_map_iter_init(cfm, iter, &enum_ops,
				  C2_COBFID_MAP_QT_ENUM_MAP);
	if (rc != 0)
		C2_ADDB_ADD(iter->cfmi_cfm->cfm_addb, &cfm_addb_loc,
			    cfm_func_fail, "cobfid_map_iter_init", rc);
	return rc;
}

/*
 *****************************************************************************
 Enumerate container
 *****************************************************************************
 */

/**
   This subroutine fills the record buffer in the iterator.  It uses a
   database cursor to continue enumeration of the map table with starting
   key values based upon the cfmi_next_ci and cfmi_next_fid values.
   After loading the records, it sets cfmi_next_fid to the value of the
   fid in the last record read, so that the next fetch will continue
   beyond the current batch.  The value of cfmi_next_ci is not modified.
 */
static int enum_container_fetch(struct c2_cobfid_map_iter *iter)
{
	return enum_fetch(iter);
}

/**
   This subroutine returns true if the container_id of the current record
   is different from the value of cfmi_next_ci (which remains invariant
   for this query).
 */
static bool enum_container_is_at_end(struct c2_cobfid_map_iter *iter,
				     const unsigned int idx)
{
	struct cobfid_map_record *recs;

	C2_PRE(cobfid_map_iter_invariant(iter));

	recs = iter->cfmi_buffer;

	if (iter->cfmi_end_of_table &&
	    iter->cfmi_last_rec == iter->cfmi_rec_idx)
		return true;

	if (idx == CFM_ITER_THUNK - 1 &&
	    recs[idx].cfr_key.cfk_ci == iter->cfmi_next_ci)
		return false;

	if (idx < CFM_ITER_THUNK - 1 &&
	    recs[idx].cfr_key.cfk_ci == recs[idx + 1].cfr_key.cfk_ci)
		return false;

	return true;
}

/**
   Reload from the position prior to the last record read.
 */
static int enum_container_reload(struct c2_cobfid_map_iter *iter)
{
	return enum_reload(iter);
}

static const struct c2_cobfid_map_iter_ops enum_container_ops = {
	.cfmio_fetch  = enum_container_fetch,
	.cfmio_is_at_end = enum_container_is_at_end,
	.cfmio_reload = enum_container_reload,
};

int c2_cobfid_map_container_enum(struct c2_cobfid_map *cfm,
				 uint64_t container_id,
				 struct c2_cobfid_map_iter *iter)
{
	int rc;

	C2_PRE(cobfid_map_invariant(cfm));

	rc = cobfid_map_iter_init(cfm, iter, &enum_container_ops,
				  C2_COBFID_MAP_QT_ENUM_CONTAINER);
	if (rc != 0) {
		C2_ADDB_ADD(iter->cfmi_cfm->cfm_addb, &cfm_addb_loc,
			    cfm_func_fail, "cobfid_map_iter_init", rc);
		return rc;
	}
	iter->cfmi_next_ci = container_id;
	/* Initialize the fid to 0, to start from first fid for a given
	   container */
	iter->cfmi_next_fid.f_container = 0;
	iter->cfmi_next_fid.f_key = 0;
	return rc;
}

/*
 * Name of cobfid_map table which stores the auxiliary database records.
 * This symbol has to be exposed to UTs and possibly other modules in future.
 */
static const char cobfid_map_name[] = "cobfid_map";
static bool cfm_key_is_initialised;
static unsigned cfm_key;

int c2_cobfid_map_get(struct c2_reqh *reqh, struct c2_cobfid_map **out)
{
	int                   rc;
        struct c2_cobfid_map *cfm;

        C2_PRE(reqh != NULL && out != NULL);

	c2_rwlock_write_lock(&reqh->rh_rwlock);
	if (!cfm_key_is_initialised) {
		cfm_key = c2_reqh_key_init();
		cfm_key_is_initialised = true;
	}

	cfm = c2_reqh_key_find(reqh, cfm_key, sizeof *cfm);
	if (!cfm->cfm_is_initialised) {
		rc = c2_cobfid_map_init(cfm, reqh->rh_dbenv, &reqh->rh_addb,
					cobfid_map_name);
		if (rc != 0) {
			c2_reqh_key_fini(reqh, cfm_key);
			c2_rwlock_write_unlock(&reqh->rh_rwlock);
			return rc;
		}
	}
	C2_ASSERT(cobfid_map_invariant(cfm));
	C2_CNT_INC(cfm->cfm_ref_cnt);
	*out = cfm;
	c2_rwlock_write_unlock(&reqh->rh_rwlock);

	return 0;
}

void c2_cobfid_map_put(struct c2_reqh *reqh)
{
	struct c2_cobfid_map *cfm;

	C2_PRE(reqh != NULL);

	c2_rwlock_write_lock(&reqh->rh_rwlock);
	cfm = c2_reqh_key_find(reqh, cfm_key, sizeof *cfm);
	C2_ASSERT(cobfid_map_invariant(cfm));
	C2_CNT_DEC(cfm->cfm_ref_cnt);
	if (cfm->cfm_ref_cnt == 0) {
		c2_cobfid_map_fini(cfm);
		c2_reqh_key_fini(reqh, cfm_key);
	}
	c2_rwlock_write_unlock(&reqh->rh_rwlock);
}

/** @} cobfidmap */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
