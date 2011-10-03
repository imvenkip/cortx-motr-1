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
#include <config.h>
#endif

#include <sys/stat.h>
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"  /* SET0 */
#include "lib/arith.h" /* C2_3WAY */
#include "ioservice/cobfid_map.h"

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
	/**< key combining container id and global file id */
	struct cobfid_map_key	cfr_key;
	/** < cob id */
	struct c2_uint128	cfr_cob;
};

/**
  Compare the cobfid_map table keys
 */
static int cfm_cmp(struct c2_table *table, const void *key0, const void *key1)
{
	int rc;
	const struct cobfid_map_key *map_key0 = key0;
	const struct cobfid_map_key *map_key1 = key1;

	C2_PRE(table != NULL);
	C2_PRE(key0 != NULL);
	C2_PRE(key1 != NULL);

	rc = c2_fid_eq(&map_key0->cfk_fid, &map_key1->cfk_fid);
	return rc ?: C2_3WAY(map_key0->cfk_ci, map_key1->cfk_ci);
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
	.key_cmp = cfm_cmp
};

/** ADDB Instrumentation for cobfidmap */
static const struct c2_addb_ctx_type cfm_ctx_type = {
	        .act_name = "cfm"
};

static const struct c2_addb_loc cfm_addb_loc = {
	        .al_name = "cfm"
};

C2_ADDB_EV_DEFINE(cfm_func_fail, "cfm_func_fail",
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
	if (cfm == NULL || cfm->cfm_magic != CFM_MAP_MAGIC)
		return false;
	return true;
}

int c2_cobfid_map_init(struct c2_cobfid_map *cfm, struct c2_dbenv *db_env,
		       struct c2_addb_ctx *addb_ctx, const char *map_path,
		       const char *map_name)
{
	int      rc;
	char    *opath = NULL;
	char    *dpath = NULL;

	C2_PRE(cfm != NULL);
	C2_PRE(db_env != NULL);
	C2_PRE(addb_ctx != NULL);
	C2_PRE(map_path != NULL);
	C2_PRE(map_name != NULL);

	C2_SET0(cfm);
	cfm->cfm_magic = CFM_MAP_MAGIC;
	cfm->cfm_addb = addb_ctx;
	cfm->cfm_map_path = map_path;
	cfm->cfm_map_name = map_name;
	cfm->cfm_dbenv = db_env;

        c2_addb_ctx_init(cfm->cfm_addb, &cfm_ctx_type, &c2_addb_global_ctx);

	C2_ALLOC_ARR(opath, strlen(map_path) + 2);
        if (opath == NULL) {
                C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, c2_addb_oom);
                rc = -ENOMEM;
                goto cleanup;
        }

        C2_ALLOC_ARR(dpath, strlen(map_path) + 2);
        if (dpath == NULL) {
                C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, c2_addb_oom);
                rc = -ENOMEM;
                goto cleanup;
        }

	rc = mkdir(map_path, 0700);
	if (rc != 0 && errno != EEXIST) {
		rc = -errno;
		C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
			    "mkdir", rc);
		goto cleanup;
	}

        sprintf(opath, "%s/o", map_path);

	rc = mkdir(opath, 0700);
	if (rc != 0 && errno != EEXIST) {
		rc = -errno;
		C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
			    "mkdir", rc);
		goto cleanup;
	}

        sprintf(dpath, "%s/d", map_path);

        rc = c2_dbenv_init(cfm->cfm_dbenv, map_path, 0);
	if (rc != 0) {
		C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
			    "c2_dbenv_init", rc);
		goto cleanup;
	}

	c2_free(opath);
	c2_free(dpath);

	c2_time_set(&cfm->cfm_last_mod, 0, 0);
	c2_time_add(c2_time_now(), cfm->cfm_last_mod);

	C2_POST(cobfid_map_invariant(cfm));

	return 0;

cleanup:
	c2_free(opath);
        c2_free(dpath);
        c2_addb_ctx_fini(cfm->cfm_addb);
	return rc;
}
C2_EXPORTED(c2_cobfid_map_init);

void c2_cobfid_map_fini(struct c2_cobfid_map *cfm)
{
	C2_PRE(cobfid_map_invariant(cfm));

	c2_dbenv_fini(cfm->cfm_dbenv);
	c2_addb_ctx_fini(cfm->cfm_addb);

}
C2_EXPORTED(c2_cobfid_map_fini);

int c2_cobfid_map_add(struct c2_cobfid_map *cfm, uint64_t container_id,
		      struct c2_fid file_fid, struct c2_uint128 cob_fid)
{
	int			 rc;
	bool			 table_update_failed = false;
	struct c2_table		 table;
	struct c2_db_tx		 tx;
	struct c2_db_pair	 db_pair;
	struct cobfid_map_key	*key;

	C2_PRE(cobfid_map_invariant(cfm));

	key->cfk_ci = container_id;
	key->cfk_fid = file_fid;

	rc = c2_table_init(&table, cfm->cfm_dbenv, cfm->cfm_map_name,
			   0, &cfm_table_ops);
	if (rc != 0) {
		C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
			    "c2_table_init", rc);
		return rc;
	}

	rc = c2_db_tx_init(&tx, cfm->cfm_dbenv, 0);
	if (rc != 0) {
		C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
			    "c2_db_tx_init", rc);
		c2_table_fini(&table);
		return rc;
	}

	c2_db_pair_setup(&db_pair, &table, &key, sizeof(struct cobfid_map_key),
			 &cob_fid, sizeof(struct c2_uint128));

	rc = c2_table_update(&tx, &db_pair);
	if (rc != 0)
		C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
			    "c2_table_update", rc);

	c2_db_pair_release(&db_pair);
	c2_db_pair_fini(&db_pair);
	if (table_update_failed)
		c2_db_tx_abort(&tx);
	else {
		c2_time_set(&cfm->cfm_last_mod, 0, 0);
		c2_time_add(c2_time_now(), cfm->cfm_last_mod);
		c2_db_tx_commit(&tx);
	}

	c2_table_fini(&table);

	C2_POST(cobfid_map_invariant(cfm));

	return rc;
}
C2_EXPORTED(c2_cobfid_map_add);

int c2_cobfid_map_del(struct c2_cobfid_map *cfm, uint64_t container_id,
		      struct c2_fid file_fid)
{
	int			 rc;
	struct c2_table		 table;
	struct c2_db_tx		 tx;
	struct c2_db_pair	 db_pair;
	struct cobfid_map_key	*key;
	struct c2_uint128	 cob_fid;
	bool			 table_op_failed = false;

	C2_PRE(cobfid_map_invariant(cfm));

	key->cfk_ci = container_id;
	key->cfk_fid = file_fid;

	rc = c2_table_init(&table, cfm->cfm_dbenv, cfm->cfm_map_name,
			   0, &cfm_table_ops);
	if (rc != 0) {
		C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
			    "c2_table_init", rc);
		return rc;
	}

	rc = c2_db_tx_init(&tx, cfm->cfm_dbenv, 0);
	if (rc != 0) {
		C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
			    "c2_db_tx_init", rc);
		c2_table_fini(&table);
		return rc;
	}

	c2_db_pair_setup(&db_pair, &table, &key, sizeof(struct cobfid_map_key),
			 &cob_fid, sizeof(struct c2_uint128));

	rc = c2_table_lookup(&tx, &db_pair);
	if (rc != 0) {
		C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
			    "c2_table_lookup", rc);
		table_op_failed = true;
		goto cleanup;
	}

	c2_db_pair_release(&db_pair);

	c2_db_pair_setup(&db_pair, &table, &key, sizeof(struct cobfid_map_key),
			 &cob_fid, sizeof(struct c2_uint128));

	rc = c2_table_delete(&tx, &db_pair);
	if (rc != 0) {
		C2_ADDB_ADD(cfm->cfm_addb, &cfm_addb_loc, cfm_func_fail,
			    "c2_table_delete", rc);
		table_op_failed = true;
	}

cleanup:
	c2_db_pair_release(&db_pair);
	c2_db_pair_fini(&db_pair);
	if (table_op_failed)
		c2_db_tx_abort(&tx);
	else {
		c2_db_tx_commit(&tx);
		c2_time_set(&cfm->cfm_last_mod, 0, 0);
		c2_time_add(c2_time_now(), cfm->cfm_last_mod);
	}
	c2_table_fini(&table);

	C2_POST(cobfid_map_invariant(cfm));

	return rc;
}
C2_EXPORTED(c2_cobfid_map_del);

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
	    iter->cfmi_ops->cfmio_at_end == NULL ||
	    iter->cfmi_ops->cfmio_reload == NULL)
		return false;
	if (iter->cfmi_rec_idx > iter->cfmi_num_recs)
		return false;
	return true;
}

void c2_cobfid_map_iter_fini(struct  c2_cobfid_map_iter *iter)
{
	C2_PRE(cobfid_map_iter_invariant(iter));
}
C2_EXPORTED(c2_cobfid_map_iter_fini);

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
	iter->cfmi_magic = CFM_ITER_MAGIC;
	iter->cfmi_cfm = cfm;
	iter->cfmi_ops = ops;
	iter->cfmi_qt = qt;

	/* allocate buffer */
	struct cobfid_map_record *recs = iter->cfmi_buffer; /* safe cast */
	C2_ALLOC_ARR(recs, CFM_ITER_THUNK);

	/* force a query by positioning at the end */
	iter->cfmi_rec_idx = iter->cfmi_num_recs;

	C2_POST(cobfid_map_iter_invariant(iter));
	return 0;
}

int c2_cobfid_map_iter_next(struct  c2_cobfid_map_iter *iter,
			    uint64_t *container_id_p,
			    struct c2_fid *file_fid_p,
			    struct c2_uint128 *cob_fid_p)
{
	C2_PRE(cobfid_map_iter_invariant(iter));
	C2_PRE(container_id_p != NULL);
	C2_PRE(file_fid_p != NULL);
	C2_PRE(cob_fid_p != NULL);

	/* already in error */
	if (iter->cfmi_error != 0)
		return iter->cfmi_error;

	if (iter->cfmi_last_load > iter->cfmi_cfm->cfm_last_mod) {
		/* iterator stale: reload the buffer and reset buffer index */
		iter->cfmi_ops->cfmio_reload(iter);
	} else if (iter->cfmi_rec_idx == iter->cfmi_num_recs) {
		/* buffer empty: fetch records into the buffer
		   and then set cfmi_rec_idx to 0 */
		iter->cfmi_ops->cfmio_fetch(iter);
	}

	/* Check if current record exhausts the iterator */
	if (iter->cfmi_ops->cfmio_at_end(iter, iter->cfmi_rec_idx))
		return -EEXIST;

	/* save value of cfmi_last_ci and cfmi_last_fid before returning */
	return 0;
}
C2_EXPORTED(c2_cobfid_map_iter_next);


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
	C2_PRE(cobfid_map_iter_invariant(iter));

	/* use c2_db_cursor_get() to read from (cfmi_next_ci, cfmi_next_fid) */

	return 0;
}

/**
   This subroutine returns true of the container_id of the current record
   is different from the value of cfmi_next_ci (which remains invariant
   for this query).
 */
static bool enum_container_at_end(struct c2_cobfid_map_iter *iter,
				  unsigned int idx)
{
	struct cobfid_map_record *recs = iter->cfmi_buffer; /* safe cast */
	if (recs[idx].cfr_key.cfk_ci != iter->cfmi_next_ci)
		return false;
	return true;
}

/**
   Reload from the position prior to the last record read.
 */
static int enum_container_reload(struct c2_cobfid_map_iter *iter)
{
	iter->cfmi_next_fid = iter->cfmi_last_fid;
	return iter->cfmi_ops->cfmio_fetch(iter);
}

static const struct c2_cobfid_map_iter_ops enum_container_ops = {
	.cfmio_fetch  = enum_container_fetch,
	.cfmio_at_end = enum_container_at_end,
	.cfmio_reload = enum_container_reload
};

int c2_cobfid_map_container_enum(struct c2_cobfid_map *cfm,
				 uint64_t container_id,
				 struct c2_cobfid_map_iter *iter)
{
	int rc;

	rc = cobfid_map_iter_init(cfm, iter, &enum_container_ops,
				  C2_COBFID_MAP_QT_ENUM_CONTAINER);
	iter->cfmi_next_ci = container_id;
	return rc;
}
C2_EXPORTED(c2_cobfid_map_container_enum);


/*
 *****************************************************************************
 Enumerate map
 *****************************************************************************
 */

static int enum_fetch(struct c2_cobfid_map_iter *iter)
{
	C2_PRE(cobfid_map_iter_invariant(iter));

	return 0;
}

static bool enum_at_end(struct c2_cobfid_map_iter *iter,
				  unsigned int idx)
{
	return true;
}

static int enum_reload(struct c2_cobfid_map_iter *iter)
{
	return 0;
}

static const struct c2_cobfid_map_iter_ops enum_ops = {
	.cfmio_fetch  = enum_fetch,
	.cfmio_at_end = enum_at_end,
	.cfmio_reload = enum_reload
};

int c2_cobfid_map_enum(struct c2_cobfid_map *cfm,
		       struct c2_cobfid_map_iter *iter)
{
	int rc;

	rc = cobfid_map_iter_init(cfm, iter, &enum_ops,
				  C2_COBFID_MAP_QT_ENUM_MAP);
	return rc;

}
C2_EXPORTED(c2_cobfid_map_enum);

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
