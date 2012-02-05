/* -*- c -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 25-Sep-2012
 */

#include "conf/onwire.h"
#include "conf/onwire_xc.h"
#include "conf/obj.h" /* c2_conf_objtype: @todo: should it be in other place */

#include "conf/conf_xcode.h"
#include "conf/obj.h"

#include "lib/buf_xc.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/arith.h"
#include "lib/misc.h"
#include "lib/buf.h"

#include "db/db.h"


/* ------------------------------------------------------------------
 * xcoders: confx_object -> raw buffer, raw buffer -> confx_object
 * ------------------------------------------------------------------ */

static int xcode_ctx_init(struct confx_object *obj, struct c2_xcode_ctx *ctx,
			  bool decode)
{
	switch(obj->o_conf.u_type) {
	case C2_CO_PROFILE:
		c2_xcode_ctx_init(ctx, &(struct c2_xcode_obj)
		  { confx_profile_xc,
		    decode ? NULL : &obj->o_conf.u.u_profile });
		break;
	case C2_CO_FILESYSTEM:
		c2_xcode_ctx_init(ctx, &(struct c2_xcode_obj)
		  { confx_filesystem_xc,
		    decode ? NULL : &obj->o_conf.u.u_filesystem });
		break;
	case C2_CO_SERVICE:
		c2_xcode_ctx_init(ctx, &(struct c2_xcode_obj)
		  { confx_service_xc,
		    decode ? NULL : &obj->o_conf.u.u_service });
		break;
	case C2_CO_NODE:
		c2_xcode_ctx_init(ctx, &(struct c2_xcode_obj)
		  { confx_node_xc,
		    decode ? NULL : &obj->o_conf.u.u_node });
		break;
	case C2_CO_NIC:
		c2_xcode_ctx_init(ctx, &(struct c2_xcode_obj)
		  { confx_nic_xc,
		    decode ? NULL : &obj->o_conf.u.u_nic });
		break;
	case C2_CO_SDEV:
		c2_xcode_ctx_init(ctx, &(struct c2_xcode_obj)
		  { confx_sdev_xc,
		    decode ? NULL : &obj->o_conf.u.u_sdev });
		break;
	case C2_CO_PARTITION:
		c2_xcode_ctx_init(ctx, &(struct c2_xcode_obj)
		  { confx_partition_xc,
		    decode ? NULL : &obj->o_conf.u.u_partition });
		break;

	case C2_CO_DIR:
	default:
		return -EINVAL;
	}

	return 0;
}

static void *confx_alloc(struct c2_xcode_cursor *ctx __attribute__((unused)),
			 size_t nob)
{
	return c2_alloc(nob);
}

int c2_confx_decode(struct c2_conf_xcode_pair *kv,
		    struct confx_object *obj_out)
{
	struct c2_xcode_ctx ctx;
	struct c2_bufvec    bvec = C2_BUFVEC_INIT_BUF(&kv->xp_val.b_addr,
						      &kv->xp_val.b_nob);
	int                 result;

	result = xcode_ctx_init(obj_out, &ctx, true);
	if (result != 0)
		return result;
	ctx.xcx_alloc = confx_alloc;

	c2_bufvec_cursor_init(&ctx.xcx_buf, &bvec);
	result = c2_xcode_decode(&ctx);
	if (result != 0)
		return result;

	/* @todo: check if 'deep copy' is needed ... */
	switch(obj_out->o_conf.u_type) {
	case C2_CO_PROFILE:
		obj_out->o_conf.u.u_profile = *(struct confx_profile *)
			ctx.xcx_it.xcu_stack[0].s_obj.xo_ptr;
		break;
	case C2_CO_FILESYSTEM:
		obj_out->o_conf.u.u_filesystem = *(struct confx_filesystem *)
			ctx.xcx_it.xcu_stack[0].s_obj.xo_ptr;
		break;
	case C2_CO_SERVICE:
		obj_out->o_conf.u.u_service = *(struct confx_service *)
			ctx.xcx_it.xcu_stack[0].s_obj.xo_ptr;
		break;
	case C2_CO_NODE:
		obj_out->o_conf.u.u_node = *(struct confx_node *)
			ctx.xcx_it.xcu_stack[0].s_obj.xo_ptr;
		break;
	case C2_CO_NIC:
		obj_out->o_conf.u.u_nic = *(struct confx_nic *)
			ctx.xcx_it.xcu_stack[0].s_obj.xo_ptr;
		break;
	case C2_CO_SDEV:
		obj_out->o_conf.u.u_sdev = *(struct confx_sdev *)
			ctx.xcx_it.xcu_stack[0].s_obj.xo_ptr;
		break;
	case C2_CO_PARTITION:
		obj_out->o_conf.u.u_partition = *(struct confx_partition *)
			ctx.xcx_it.xcu_stack[0].s_obj.xo_ptr;
		break;
	case C2_CO_DIR:
	default:
		result = -EINVAL;
	}

	C2_SET0(&obj_out->o_id);
	c2_buf_copy(&obj_out->o_id, &kv->xp_key);

	c2_free(ctx.xcx_it.xcu_stack[0].s_obj.xo_ptr);
	return result;
}

int c2_confx_encode(struct confx_object *obj,
		    struct c2_conf_xcode_pair *out_kv)
{
	void                *vec;
	c2_bcount_t          count;
	struct c2_bufvec     bvec = C2_BUFVEC_INIT_BUF(&vec, &count);
	struct c2_xcode_ctx  ctx;
	int		     result;

	result = xcode_ctx_init(obj, &ctx, false);
	if (result != 0)
		return result;

	count = c2_xcode_length(&ctx);

	result = xcode_ctx_init(obj, &ctx, false);
	if (result != 0)
		return result;

	C2_ALLOC_ARR(vec, count);
	if (vec == NULL)
		return -ENOMEM;

	c2_bufvec_cursor_init(&ctx.xcx_buf, &bvec);
	result = c2_xcode_encode(&ctx);
	if (result != 0) {
		c2_free(vec);
		return result;
	}

	out_kv->xp_val.b_addr = vec;
	out_kv->xp_val.b_nob  = count;
	out_kv->xp_key	      = obj->o_id;

	return 0;
}

int c2_confx_types_init(void)
{
	c2_xc_onwire_init();
	return 0;
}

void c2_confx_types_fini(void)
{
	c2_xc_onwire_fini();
}


/* ------------------------------------------------------------------
 * confdb: common
 * ------------------------------------------------------------------ */

enum {
	C2_CONF_XCODE_SRV_EP_MAX = 16,
	C2_CONF_XCODE_FS_MAX     = 16,
	C2_CONF_XCODE_NICS_MAX   = 16,
	C2_CONF_XCODE_SDEVS_MAX  = 16,
	C2_CONF_XCODE_PART_MAX   = 16,
	C2_CONF_XCODE_UUID_SIZE	 = 40,
	C2_CONF_XCODE_NAME_LEN	 = 256,

	/* @todo: a very inaccurate estimations: */
	C2_CONF_XCODE_VAL_MAX    = sizeof(struct confx_object)    +
					C2_CONF_XCODE_NAME_LEN    *
					(C2_CONF_XCODE_SRV_EP_MAX +
					 C2_CONF_XCODE_FS_MAX     +
					 C2_CONF_XCODE_NICS_MAX   +
					 C2_CONF_XCODE_SDEVS_MAX  +
					 C2_CONF_XCODE_PART_MAX)
};

struct c2_confx_db_key {
	uint8_t cdk_length;
	char    cdk_key[C2_CONF_XCODE_UUID_SIZE];
};

static const char *db_tables[] = {
	[C2_CO_DIR]		= NULL,
	[C2_CO_PROFILE]		= "profile",
	[C2_CO_FILESYSTEM]	= "filesystem",
	[C2_CO_SERVICE]		= "service",
	[C2_CO_NODE]		= "node",
	[C2_CO_NIC]		= "nic",
	[C2_CO_SDEV]		= "sdev",
	[C2_CO_PARTITION]	= "partition",
};

static int key_cmp(struct c2_table *table,
		   const void *key0, const void *key1)
{
	const struct c2_confx_db_key *v0 = key0;
	const struct c2_confx_db_key *v1 = key1;

	return memcmp(v0->cdk_key, v1->cdk_key,
		      min_check(v0->cdk_length, v1->cdk_length));
}

static const struct c2_table_ops table_ops = {
	.to = {
		[TO_KEY] = { .max_size = sizeof(struct c2_confx_db_key) },
		[TO_REC] = { .max_size = C2_CONF_XCODE_VAL_MAX          }
	},
	.key_cmp = key_cmp
};


/* ------------------------------------------------------------------
 * confdb: loader
 * ------------------------------------------------------------------ */

static void xcode_pair_to_key(struct c2_conf_xcode_pair *kv,
			      struct c2_confx_db_key *out)
{
	C2_PRE(kv->xp_key.b_nob < C2_CONF_XCODE_UUID_SIZE);

	out->cdk_length = kv->xp_key.b_nob;
	memcpy(out->cdk_key, kv->xp_key.b_addr, out->cdk_length);
}

static void c2_confx_db_tables_fini(struct c2_table *table, size_t count)
{
	int i;

	for (i = 0; i < count; ++i) {
		if (db_tables[i] == NULL)
			continue;
		c2_table_fini(&table[i]);
	}
}

static int c2_confx_db_tables_init(struct c2_table *table, struct c2_dbenv *db,
				   size_t count)
{
	int i;
	int result;

	for (i = 0; i < count; ++i) {
		if (db_tables[i] == NULL)
			continue;

		result = c2_table_init(&table[i], db, db_tables[i], 0,
				       &table_ops);
		if (result != 0)
			goto tab_fini;
	}
	return 0;
tab_fini:
	c2_confx_db_tables_fini(table, i);
	return result;
}

int c2_confx_db_create(const char *db_name,
		       struct confx_object *obj, size_t obj_nr)
{
	struct c2_dbenv   db;
        struct c2_table	  table[ARRAY_SIZE(db_tables)];
        struct c2_db_tx	  tx;
        struct c2_db_pair cons;

	struct c2_conf_xcode_pair enc_kv;
	struct c2_confx_db_key    key;

	int i;
	int result;
	int tx_result;

	result = c2_dbenv_init(&db, db_name, 0);
	if (result != 0)
		goto confx_db_env_err;

	result = c2_confx_db_tables_init(table, &db, ARRAY_SIZE(table));
	if (result != 0)
		goto confx_db_table_err;

        result = c2_db_tx_init(&tx, &db, 0);
	if (result != 0)
		goto confx_db_tx_err;

	for (i = 0; i < obj_nr; ++i) {
		C2_ASSERT(obj[i].o_conf.u_type < ARRAY_SIZE(table));
		C2_ASSERT(obj[i].o_conf.u_type > C2_CO_DIR);

		result = c2_confx_encode(&obj[i], &enc_kv);
		if (result != 0)
			goto confx_db_enc_err;

		C2_SET0(&key);
		xcode_pair_to_key(&enc_kv, &key);
		c2_db_pair_setup(&cons, &table[obj[i].o_conf.u_type],
				 &key, sizeof key,
				 enc_kv.xp_val.b_addr, enc_kv.xp_val.b_nob);

		result = c2_table_update(&tx, &cons);
		c2_db_pair_fini(&cons);
		c2_free(enc_kv.xp_val.b_addr);

		if (result != 0) {
			/* ignore result: update failed, try to clean up */
			c2_db_tx_abort(&tx);
			goto confx_db_tx_err;
		}
	}

confx_db_enc_err:
	tx_result = c2_db_tx_commit(&tx);
	result = (result == 0) ? tx_result : result;
confx_db_tx_err:
	c2_confx_db_tables_fini(table, ARRAY_SIZE(table));
confx_db_table_err:
	c2_dbenv_fini(&db);
confx_db_env_err:
	return result;
}


/* ------------------------------------------------------------------
 * confdb: reader
 * ------------------------------------------------------------------ */

static int c2_confx_db_obj_count(struct c2_table *tables, struct c2_db_tx *tx)
{
	struct c2_db_pair	pair;
        struct c2_db_cursor	cursor;
	struct c2_confx_db_key	key;
	char		       *val;
	int result;
	int obj_nr;
	int i;

	/* too large to be allocated in stack */
	C2_ALLOC_ARR(val, C2_CONF_XCODE_VAL_MAX);
	if (val == NULL)
		return -ENOMEM;

        for (obj_nr = 0, i = C2_CO_PROFILE; i <= C2_CO_PARTITION; ++i) {
                c2_db_pair_setup(&pair, &tables[i],
                                 &key, sizeof key, val, C2_CONF_XCODE_VAL_MAX);
                result = c2_db_cursor_init(&cursor, &tables[i], tx, 0);
                if (result != 0)
			break;

                for (result = c2_db_cursor_first(&cursor, &pair); result == 0;
                     result = c2_db_cursor_next(&cursor, &pair)) {
			obj_nr++;
                }

                c2_db_cursor_fini(&cursor);

                /* make sure we are in the end of the table */
                C2_ASSERT(result == -ENOENT);
                result = 0;
        }

        if (result != 0)
                c2_db_cursor_fini(&cursor);
	c2_free(val);

	return result == 0 ? obj_nr : result;
}

int c2_confx_db_read(const char *db_name, struct confx_object **obj)
{
	struct c2_dbenv		  db;
        struct c2_table		  tables[ARRAY_SIZE(db_tables)];
        struct c2_db_tx		  tx;
        struct c2_db_pair	  pair;
        struct c2_db_cursor	  cursor;
	struct c2_conf_xcode_pair xpair;
	struct c2_confx_db_key	  key;
	char			 *val;
	/*=0 makes compiler happy, obj_count is never used when uninitialized */
	int obj_count = 0;
	int tx_result;
        int result;
	int nr;
	int i;

	/* too large to be allocated in stack */
	C2_ALLOC_ARR(val, C2_CONF_XCODE_VAL_MAX);
	if (val == NULL)
		return -ENOMEM;

        result = c2_dbenv_init(&db, db_name, 0);
	if (result != 0)
		goto c2_confx_db_read_env;

	result = c2_confx_db_tables_init(tables, &db, ARRAY_SIZE(tables));
	if (result != 0)
		goto c2_confx_db_read_tbl;

        result = c2_db_tx_init(&tx, &db, 0);
	if (result != 0)
		goto c2_confx_db_read_tx;

	obj_count = c2_confx_db_obj_count(tables, &tx);
	if (obj_count <= 0) {
		result = (obj_count == 0) ? -ENOENT : obj_count;
		goto c2_confx_db_read_obj_alloc;
	}

	C2_ALLOC_ARR(*obj, obj_count);
	if (*obj == NULL) {
		result = -ENOMEM;
		goto c2_confx_db_read_obj_alloc;
	}

        for (nr = 0, i = C2_CO_PROFILE; i <= C2_CO_PARTITION; ++i) {
                c2_db_pair_setup(&pair, &tables[i],
                                 &key, sizeof key, val, C2_CONF_XCODE_VAL_MAX);
                result = c2_db_cursor_init(&cursor, &tables[i], &tx, 0);
                if (result != 0)
			break;

                for (result = c2_db_cursor_first(&cursor, &pair); result == 0;
                     result = c2_db_cursor_next(&cursor, &pair)) {
			c2_buf_init(&xpair.xp_key, key.cdk_key, key.cdk_length);
			c2_buf_init(&xpair.xp_val, val, C2_CONF_XCODE_VAL_MAX);

			(*obj)[nr].o_conf.u_type = i;

			result = c2_confx_decode(&xpair, &((*obj)[nr]));
			if (result != 0)
				goto c2_confx_db_decode_err;

			nr++;
                }

                c2_db_cursor_fini(&cursor);

                /* make sure we are in the end of the table */
                C2_ASSERT(result == -ENOENT);
                result = 0;
        }

c2_confx_db_decode_err:
        if (result != 0)
                c2_db_cursor_fini(&cursor);

c2_confx_db_read_obj_alloc:
        tx_result = c2_db_tx_commit(&tx);
	result = (result == 0) ? tx_result : result;
c2_confx_db_read_tx:
	c2_confx_db_tables_fini(tables, ARRAY_SIZE(tables));
c2_confx_db_read_tbl:
        c2_dbenv_fini(&db);
c2_confx_db_read_env:
	c2_free(val);
	return result == 0 ? obj_count : result;
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
