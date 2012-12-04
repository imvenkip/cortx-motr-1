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

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/onwire.h"
#include "conf/onwire_xc.h"
#include "conf/obj.h" /* c2_conf_objtype (TODO: relocate the definition) */

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

#define _XCODE_CTX_INIT(ctx, abbrev, xobj, decode)     \
	c2_xcode_ctx_init((ctx),                       \
		&C2_XCODE_OBJ(confx_ ## abbrev ## _xc, \
			((decode) ? NULL : &(xobj)->o_conf.u.u_ ## abbrev)))

static void *
confx_alloc(struct c2_xcode_cursor *ctx __attribute__((unused)), size_t nob)
{
	return c2_alloc(nob);
}

static int
xcode_ctx_init(struct c2_xcode_ctx *ctx, struct confx_object *obj, bool decode)
{
	C2_ENTRY();

	switch(obj->o_conf.u_type) {
	case C2_CO_PROFILE:
		_XCODE_CTX_INIT(ctx, profile, obj, decode);
		break;
	case C2_CO_FILESYSTEM:
		_XCODE_CTX_INIT(ctx, filesystem, obj, decode);
		break;
	case C2_CO_SERVICE:
		_XCODE_CTX_INIT(ctx, service, obj, decode);
		break;
	case C2_CO_NODE:
		_XCODE_CTX_INIT(ctx, node, obj, decode);
		break;
	case C2_CO_NIC:
		_XCODE_CTX_INIT(ctx, nic, obj, decode);
		break;
	case C2_CO_SDEV:
		_XCODE_CTX_INIT(ctx, sdev, obj, decode);
		break;
	case C2_CO_PARTITION:
		_XCODE_CTX_INIT(ctx, partition, obj, decode);
		break;
	case C2_CO_DIR:
	default:
		C2_RETURN(-EINVAL);
	}

	if (decode)
		ctx->xcx_alloc = confx_alloc;

	C2_RETURN(0);
}

C2_INTERNAL int
c2_confx_decode(struct c2_conf_xcode_pair *src, struct confx_object *dest)
{
	struct c2_xcode_ctx ctx;
	int                 rc;
	struct c2_bufvec    bvec = C2_BUFVEC_INIT_BUF(&src->xp_val.b_addr,
						      &src->xp_val.b_nob);
	C2_ENTRY();

	rc = xcode_ctx_init(&ctx, dest, true);
	if (rc != 0)
		C2_RETURN(rc);

	c2_bufvec_cursor_init(&ctx.xcx_buf, &bvec);
	rc = c2_xcode_decode(&ctx);
	if (rc != 0)
		C2_RETURN(rc);

	/* XXX TODO: check if "deep copy" is needed */
	switch(dest->o_conf.u_type) {
#define _CASE(type, abbrev)                                                \
	case type:                                                         \
		dest->o_conf.u.u_ ## abbrev = *(struct confx_ ## abbrev *) \
			ctx.xcx_it.xcu_stack[0].s_obj.xo_ptr;              \
		break

	_CASE(C2_CO_PROFILE,    profile);
	_CASE(C2_CO_FILESYSTEM, filesystem);
	_CASE(C2_CO_SERVICE,    service);
	_CASE(C2_CO_NODE,       node);
	_CASE(C2_CO_NIC,        nic);
	_CASE(C2_CO_SDEV,       sdev);
	_CASE(C2_CO_PARTITION,  partition);
#undef _CASE
	case C2_CO_DIR:
	default:
		rc = -EINVAL;
	}

	C2_SET0(&dest->o_id);
	c2_buf_copy(&dest->o_id, &src->xp_key);

	c2_free(ctx.xcx_it.xcu_stack[0].s_obj.xo_ptr);
	C2_RETURN(rc);
}

static int confx_object_measure(struct confx_object *xobj, c2_bcount_t *len)
{
	struct c2_xcode_ctx ctx;
	int ret;

	C2_ENTRY();

	ret = xcode_ctx_init(&ctx, xobj, false);
	if (ret == 0) {
		ret = c2_xcode_length(&ctx);
		C2_ASSERT(ret != 0);
		if (ret > 0) {
			*len = ret;
			ret = 0;
		}
	}
	C2_RETURN(ret);
}

C2_INTERNAL int
c2_confx_encode(struct confx_object *src, struct c2_conf_xcode_pair *dest)
{
	struct c2_xcode_ctx ctx;
	int                 rc;
	void               *vec;
	c2_bcount_t         len;
	struct c2_bufvec    bvec = C2_BUFVEC_INIT_BUF(&vec, &len);

	C2_ENTRY();

	rc = confx_object_measure(src, &len) ?:
		xcode_ctx_init(&ctx, src, false);
	if (rc != 0)
		C2_RETURN(rc);

	C2_ASSERT(len > 0);
	C2_ALLOC_ARR(vec, len);
	if (vec == NULL)
		C2_RETURN(-ENOMEM);

	c2_bufvec_cursor_init(&ctx.xcx_buf, &bvec);
	rc = c2_xcode_encode(&ctx);
	if (rc != 0) {
		c2_free(vec);
		C2_RETURN(rc);
	}

	dest->xp_val.b_addr = vec;
	dest->xp_val.b_nob  = len;
	dest->xp_key = src->o_id;

	C2_RETURN(0);
}

C2_INTERNAL int c2_confx_types_init(void)
{
	C2_ENTRY();
	c2_xc_onwire_init();
	C2_RETURN(0);
}

C2_INTERNAL void c2_confx_types_fini(void)
{
	C2_ENTRY();
	c2_xc_onwire_fini();
	C2_LEAVE();
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
	C2_CONF_XCODE_UUID_SIZE  = 40,
	C2_CONF_XCODE_NAME_LEN   = 256,

	/* XXX FIXME: very inaccurate estimations */
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
	[C2_CO_DIR]        = NULL,
	[C2_CO_PROFILE]    = "profile",
	[C2_CO_FILESYSTEM] = "filesystem",
	[C2_CO_SERVICE]    = "service",
	[C2_CO_NODE]       = "node",
	[C2_CO_NIC]        = "nic",
	[C2_CO_SDEV]       = "sdev",
	[C2_CO_PARTITION]  = "partition"
};

static int key_cmp(struct c2_table *table, const void *key0, const void *key1)
{
	const struct c2_confx_db_key *k0 = key0;
	const struct c2_confx_db_key *k1 = key1;

	return memcmp(k0->cdk_key, k1->cdk_key,
		      min_check(k0->cdk_length, k1->cdk_length));
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

static void
xcode_pair_to_key(struct c2_conf_xcode_pair *kv, struct c2_confx_db_key *out)
{
	C2_ENTRY();
	C2_PRE(kv->xp_key.b_nob < C2_CONF_XCODE_UUID_SIZE);

	out->cdk_length = kv->xp_key.b_nob;
	memcpy(out->cdk_key, kv->xp_key.b_addr, out->cdk_length);

	C2_LEAVE();
}

static void c2_confx_db_tables_fini(struct c2_table *table, size_t count)
{
	int i;

	C2_ENTRY();
	for (i = 0; i < count; ++i) {
		if (db_tables[i] == NULL)
			continue;
		c2_table_fini(&table[i]);
	}
	C2_LEAVE();
}

static int c2_confx_db_tables_init(struct c2_table *table, struct c2_dbenv *db,
				   size_t count)
{
	int i;
	int rc = 0;

	C2_ENTRY();

	for (i = 0; i < count; ++i) {
		if (db_tables[i] == NULL)
			continue;

		rc = c2_table_init(&table[i], db, db_tables[i], 0, &table_ops);
		if (rc != 0) {
			c2_confx_db_tables_fini(table, i);
			break;
		}
	}
	C2_RETURN(rc);
}

C2_INTERNAL int
c2_confx_db_create(const char *db_name, struct confx_object *obj, size_t obj_nr)
{
	struct c2_dbenv           db;
	struct c2_table           table[ARRAY_SIZE(db_tables)];
	struct c2_db_tx           tx;
	struct c2_db_pair         cons;
	struct c2_conf_xcode_pair enc_kv;
	struct c2_confx_db_key    key;
	int                       i;
	int                       rc;

	C2_ENTRY();

	rc = c2_dbenv_init(&db, db_name, 0);
	if (rc != 0)
		goto confx_db_env_err;

	rc = c2_confx_db_tables_init(table, &db, ARRAY_SIZE(table));
	if (rc != 0)
		goto confx_db_table_err;

	rc = c2_db_tx_init(&tx, &db, 0);
	if (rc != 0)
		goto confx_db_tx_err;

	for (i = 0; i < obj_nr; ++i) {
		C2_ASSERT(obj[i].o_conf.u_type < ARRAY_SIZE(table));
		C2_ASSERT(obj[i].o_conf.u_type > C2_CO_DIR);

		rc = c2_confx_encode(&obj[i], &enc_kv);
		if (rc != 0)
			goto confx_db_enc_err;

		C2_SET0(&key);
		xcode_pair_to_key(&enc_kv, &key);
		c2_db_pair_setup(&cons, &table[obj[i].o_conf.u_type],
				 &key, sizeof key,
				 enc_kv.xp_val.b_addr, enc_kv.xp_val.b_nob);

		rc = c2_table_update(&tx, &cons);
		c2_db_pair_fini(&cons);
		c2_free(enc_kv.xp_val.b_addr);

		if (rc != 0) {
			/* ignore rc: update failed, try to clean up */
			c2_db_tx_abort(&tx);
			goto confx_db_tx_err;
		}
	}

confx_db_enc_err:
	rc = rc ?: c2_db_tx_commit(&tx);
confx_db_tx_err:
	c2_confx_db_tables_fini(table, ARRAY_SIZE(table));
confx_db_table_err:
	c2_dbenv_fini(&db);
confx_db_env_err:
	C2_RETURN(rc);
}


/* ------------------------------------------------------------------
 * confdb: reader
 * ------------------------------------------------------------------ */

static int c2_confx_db_obj_count(struct c2_table *tables, struct c2_db_tx *tx)
{
	struct c2_db_pair      pair;
	struct c2_db_cursor    cursor;
	struct c2_confx_db_key key;
	char                  *val;
	int                    obj_nr;
	int                    i;
	int                    rc;

	C2_ENTRY();

	/* too large to be allocated in stack */
	C2_ALLOC_ARR(val, C2_CONF_XCODE_VAL_MAX);
	if (val == NULL)
		C2_RETURN(-ENOMEM);

	for (obj_nr = 0, i = C2_CO_PROFILE; i <= C2_CO_PARTITION; ++i) {
		c2_db_pair_setup(&pair, &tables[i],
				 &key, sizeof key, val, C2_CONF_XCODE_VAL_MAX);
		rc = c2_db_cursor_init(&cursor, &tables[i], tx, 0);
		if (rc != 0)
			break;

		for (rc = c2_db_cursor_first(&cursor, &pair); rc == 0;
		     rc = c2_db_cursor_next(&cursor, &pair))
			obj_nr++;

		c2_db_cursor_fini(&cursor);

		/* make sure we are in the end of the table */
		C2_ASSERT(rc == -ENOENT);
		rc = 0;
	}

	if (rc != 0)
		c2_db_cursor_fini(&cursor);
	c2_free(val);

	C2_RETURN(rc == 0 ? obj_nr : rc);
}

C2_INTERNAL int c2_confx_db_read(const char *db_name, struct confx_object **obj)
{
	struct c2_dbenv           db;
	struct c2_table           tables[ARRAY_SIZE(db_tables)];
	struct c2_db_tx           tx;
	struct c2_db_pair         pair;
	struct c2_db_cursor       cursor;
	struct c2_conf_xcode_pair xpair;
	struct c2_confx_db_key    key;
	char                     *val;
	int                       nr;
	int                       i;
	int                       rc;
	int                       obj_count = 0;

	C2_ENTRY();

	/* too large to be allocated in stack */
	C2_ALLOC_ARR(val, C2_CONF_XCODE_VAL_MAX);
	if (val == NULL)
		C2_RETURN(-ENOMEM);

	rc = c2_dbenv_init(&db, db_name, 0);
	if (rc != 0)
		goto c2_confx_db_read_env;

	rc = c2_confx_db_tables_init(tables, &db, ARRAY_SIZE(tables));
	if (rc != 0)
		goto c2_confx_db_read_tbl;

	rc = c2_db_tx_init(&tx, &db, 0);
	if (rc != 0)
		goto c2_confx_db_read_tx;

	obj_count = c2_confx_db_obj_count(tables, &tx);
	if (obj_count <= 0) {
		rc = (obj_count == 0) ? -ENOENT : obj_count;
		goto c2_confx_db_read_obj_alloc;
	}

	C2_ALLOC_ARR(*obj, obj_count);
	if (*obj == NULL) {
		rc = -ENOMEM;
		goto c2_confx_db_read_obj_alloc;
	}

	for (nr = 0, i = C2_CO_PROFILE; i <= C2_CO_PARTITION; ++i) {
		c2_db_pair_setup(&pair, &tables[i], &key, sizeof key, val,
				 C2_CONF_XCODE_VAL_MAX);
		rc = c2_db_cursor_init(&cursor, &tables[i], &tx, 0);
		if (rc != 0)
			break;

		for (rc = c2_db_cursor_first(&cursor, &pair); rc == 0;
		     rc = c2_db_cursor_next(&cursor, &pair)) {
			c2_buf_init(&xpair.xp_key, key.cdk_key, key.cdk_length);
			c2_buf_init(&xpair.xp_val, val, C2_CONF_XCODE_VAL_MAX);

			(*obj)[nr].o_conf.u_type = i;

			rc = c2_confx_decode(&xpair, &((*obj)[nr]));
			if (rc != 0)
				goto c2_confx_db_decode_err;

			nr++;
		}

		c2_db_cursor_fini(&cursor);

/* make sure we are in the end of the table */
		C2_ASSERT(rc == -ENOENT);
		rc = 0;
	}

c2_confx_db_decode_err:
	if (rc != 0)
		c2_db_cursor_fini(&cursor);
c2_confx_db_read_obj_alloc:
	rc = rc ?: c2_db_tx_commit(&tx);
c2_confx_db_read_tx:
	c2_confx_db_tables_fini(tables, ARRAY_SIZE(tables));
c2_confx_db_read_tbl:
	c2_dbenv_fini(&db);
c2_confx_db_read_env:
	c2_free(val);
	C2_RETURN(rc == 0 ? obj_count : rc);
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
