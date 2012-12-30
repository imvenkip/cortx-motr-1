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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/onwire.h"
#include "conf/onwire_xc.h"
#include "conf/obj.h" /* m0_conf_objtype (TODO: relocate the definition) */

#include "conf/conf_xcode.h"
#include "conf/preload.h"
#include "conf/obj.h"

#include "lib/buf_xc.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "lib/errno.h"
#include "lib/arith.h"
#include "lib/misc.h"
#include "lib/buf.h"

#include "db/db.h"

/* ------------------------------------------------------------------
 * xcoders: confx_object -> raw buffer, raw buffer -> confx_object
 * ------------------------------------------------------------------ */

#define _XCODE_CTX_INIT(ctx, abbrev, xobj, decode)     \
	m0_xcode_ctx_init((ctx),                       \
		&M0_XCODE_OBJ(confx_ ## abbrev ## _xc, \
			((decode) ? NULL : &(xobj)->o_conf.u.u_ ## abbrev)))

static void *
confx_alloc(struct m0_xcode_cursor *ctx __attribute__((unused)), size_t nob)
{
	return m0_alloc(nob);
}

static int
xcode_ctx_init(struct m0_xcode_ctx *ctx, struct confx_object *obj, bool decode)
{
	M0_ENTRY();

	switch(obj->o_conf.u_type) {
	case M0_CO_PROFILE:
		_XCODE_CTX_INIT(ctx, profile, obj, decode);
		break;
	case M0_CO_FILESYSTEM:
		_XCODE_CTX_INIT(ctx, filesystem, obj, decode);
		break;
	case M0_CO_SERVICE:
		_XCODE_CTX_INIT(ctx, service, obj, decode);
		break;
	case M0_CO_NODE:
		_XCODE_CTX_INIT(ctx, node, obj, decode);
		break;
	case M0_CO_NIC:
		_XCODE_CTX_INIT(ctx, nic, obj, decode);
		break;
	case M0_CO_SDEV:
		_XCODE_CTX_INIT(ctx, sdev, obj, decode);
		break;
	case M0_CO_PARTITION:
		_XCODE_CTX_INIT(ctx, partition, obj, decode);
		break;
	case M0_CO_DIR:
	default:
		M0_RETURN(-EINVAL);
	}

	if (decode)
		ctx->xcx_alloc = confx_alloc;

	M0_RETURN(0);
}

M0_INTERNAL int
m0_confx_decode(struct m0_conf_xcode_pair *src, struct confx_object *dest)
{
	struct m0_xcode_ctx ctx;
	int                 rc;
	struct m0_bufvec    bvec = M0_BUFVEC_INIT_BUF(&src->xp_val.b_addr,
						      &src->xp_val.b_nob);
	M0_ENTRY();

	rc = xcode_ctx_init(&ctx, dest, true);
	if (rc != 0)
		M0_RETURN(rc);

	m0_bufvec_cursor_init(&ctx.xcx_buf, &bvec);
	rc = m0_xcode_decode(&ctx);
	if (rc != 0)
		M0_RETURN(rc);

	/* XXX TODO: check if "deep copy" is needed */
	switch(dest->o_conf.u_type) {
#define _CASE(type, abbrev)                                                \
	case type:                                                         \
		dest->o_conf.u.u_ ## abbrev = *(struct confx_ ## abbrev *) \
			ctx.xcx_it.xcu_stack[0].s_obj.xo_ptr;              \
		break

	_CASE(M0_CO_PROFILE,    profile);
	_CASE(M0_CO_FILESYSTEM, filesystem);
	_CASE(M0_CO_SERVICE,    service);
	_CASE(M0_CO_NODE,       node);
	_CASE(M0_CO_NIC,        nic);
	_CASE(M0_CO_SDEV,       sdev);
	_CASE(M0_CO_PARTITION,  partition);
#undef _CASE
	case M0_CO_DIR:
	default:
		rc = -EINVAL;
	}

	M0_SET0(&dest->o_id);
	m0_buf_copy(&dest->o_id, &src->xp_key);

	m0_free(ctx.xcx_it.xcu_stack[0].s_obj.xo_ptr);
	M0_RETURN(rc);
}

static int confx_object_measure(struct confx_object *xobj, m0_bcount_t *len)
{
	struct m0_xcode_ctx ctx;
	int ret;

	M0_ENTRY();

	ret = xcode_ctx_init(&ctx, xobj, false);
	if (ret == 0) {
		ret = m0_xcode_length(&ctx);
		M0_ASSERT(ret != 0);
		if (ret > 0) {
			*len = ret;
			ret = 0;
		}
	}
	M0_RETURN(ret);
}

M0_INTERNAL int
m0_confx_encode(struct confx_object *src, struct m0_conf_xcode_pair *dest)
{
	struct m0_xcode_ctx ctx;
	int                 rc;
	void               *vec;
	m0_bcount_t         len;
	struct m0_bufvec    bvec = M0_BUFVEC_INIT_BUF(&vec, &len);

	M0_ENTRY();

	rc = confx_object_measure(src, &len) ?:
		xcode_ctx_init(&ctx, src, false);
	if (rc != 0)
		M0_RETURN(rc);

	M0_ASSERT(len > 0);
	M0_ALLOC_ARR(vec, len);
	if (vec == NULL)
		M0_RETURN(-ENOMEM);

	m0_bufvec_cursor_init(&ctx.xcx_buf, &bvec);
	rc = m0_xcode_encode(&ctx);
	if (rc != 0) {
		m0_free(vec);
		M0_RETURN(rc);
	}

	dest->xp_val.b_addr = vec;
	dest->xp_val.b_nob  = len;
	dest->xp_key = src->o_id;

	M0_RETURN(0);
}

M0_INTERNAL int m0_confx_types_init(void)
{
	M0_ENTRY();
	m0_xc_onwire_init();
	M0_RETURN(0);
}

M0_INTERNAL void m0_confx_types_fini(void)
{
	M0_ENTRY();
	m0_xc_onwire_fini();
	M0_LEAVE();
}

/* ------------------------------------------------------------------
 * confdb: common
 * ------------------------------------------------------------------ */

enum {
	M0_CONF_XCODE_SRV_EP_MAX = 16,
	M0_CONF_XCODE_FS_MAX     = 16,
	M0_CONF_XCODE_NICS_MAX   = 16,
	M0_CONF_XCODE_SDEVS_MAX  = 16,
	M0_CONF_XCODE_PART_MAX   = 16,
	M0_CONF_XCODE_UUID_SIZE  = 40,
	M0_CONF_XCODE_NAME_LEN   = 256,

	/* XXX FIXME: very inaccurate estimations */
	M0_CONF_XCODE_VAL_MAX    = sizeof(struct confx_object)    +
					M0_CONF_XCODE_NAME_LEN    *
					(M0_CONF_XCODE_SRV_EP_MAX +
					 M0_CONF_XCODE_FS_MAX     +
					 M0_CONF_XCODE_NICS_MAX   +
					 M0_CONF_XCODE_SDEVS_MAX  +
					 M0_CONF_XCODE_PART_MAX)
};

struct m0_confx_db_key {
	uint8_t cdk_length;
	char    cdk_key[M0_CONF_XCODE_UUID_SIZE];
};

static const char *db_tables[] = {
	[M0_CO_DIR]        = NULL,
	[M0_CO_PROFILE]    = "profile",
	[M0_CO_FILESYSTEM] = "filesystem",
	[M0_CO_SERVICE]    = "service",
	[M0_CO_NODE]       = "node",
	[M0_CO_NIC]        = "nic",
	[M0_CO_SDEV]       = "sdev",
	[M0_CO_PARTITION]  = "partition"
};

static int key_cmp(struct m0_table *table, const void *key0, const void *key1)
{
	const struct m0_confx_db_key *k0 = key0;
	const struct m0_confx_db_key *k1 = key1;

	return memcmp(k0->cdk_key, k1->cdk_key,
		      min_check(k0->cdk_length, k1->cdk_length));
}

static const struct m0_table_ops table_ops = {
	.to = {
		[TO_KEY] = { .max_size = sizeof(struct m0_confx_db_key) },
		[TO_REC] = { .max_size = M0_CONF_XCODE_VAL_MAX          }
	},
	.key_cmp = key_cmp
};

/* ------------------------------------------------------------------
 * confdb: loader
 * ------------------------------------------------------------------ */

static void
xcode_pair_to_key(struct m0_conf_xcode_pair *kv, struct m0_confx_db_key *out)
{
	M0_ENTRY();
	M0_PRE(kv->xp_key.b_nob < M0_CONF_XCODE_UUID_SIZE);

	out->cdk_length = kv->xp_key.b_nob;
	memcpy(out->cdk_key, kv->xp_key.b_addr, out->cdk_length);

	M0_LEAVE();
}

static void m0_confx_db_tables_fini(struct m0_table *table, size_t count)
{
	int i;

	M0_ENTRY();
	for (i = 0; i < count; ++i) {
		if (db_tables[i] == NULL)
			continue;
		m0_table_fini(&table[i]);
	}
	M0_LEAVE();
}

static int m0_confx_db_tables_init(struct m0_table *table, struct m0_dbenv *db,
				   size_t count)
{
	int i;
	int rc = 0;

	M0_ENTRY();

	for (i = 0; i < count; ++i) {
		if (db_tables[i] == NULL)
			continue;

		rc = m0_table_init(&table[i], db, db_tables[i], 0, &table_ops);
		if (rc != 0) {
			m0_confx_db_tables_fini(table, i);
			break;
		}
	}
	M0_RETURN(rc);
}

M0_INTERNAL int
m0_confx_db_create(const char *db_name, struct confx_object *obj, size_t obj_nr)
{
	struct m0_dbenv           db;
	struct m0_table           table[ARRAY_SIZE(db_tables)];
	struct m0_db_tx           tx;
	struct m0_db_pair         cons;
	struct m0_conf_xcode_pair enc_kv;
	struct m0_confx_db_key    key;
	int                       i;
	int                       rc;

	M0_ENTRY();

	rc = m0_dbenv_init(&db, db_name, 0);
	if (rc != 0)
		goto confx_db_env_err;

	rc = m0_confx_db_tables_init(table, &db, ARRAY_SIZE(table));
	if (rc != 0)
		goto confx_db_table_err;

	rc = m0_db_tx_init(&tx, &db, 0);
	if (rc != 0)
		goto confx_db_tx_err;

	for (i = 0; i < obj_nr; ++i) {
		M0_ASSERT(obj[i].o_conf.u_type < ARRAY_SIZE(table));
		M0_ASSERT(obj[i].o_conf.u_type > M0_CO_DIR);

		rc = m0_confx_encode(&obj[i], &enc_kv);
		if (rc != 0)
			goto confx_db_enc_err;

		M0_SET0(&key);
		xcode_pair_to_key(&enc_kv, &key);
		m0_db_pair_setup(&cons, &table[obj[i].o_conf.u_type],
				 &key, sizeof key,
				 enc_kv.xp_val.b_addr, enc_kv.xp_val.b_nob);

		rc = m0_table_update(&tx, &cons);
		m0_db_pair_fini(&cons);
		m0_free(enc_kv.xp_val.b_addr);

		if (rc != 0) {
			/* ignore rc: update failed, try to clean up */
			m0_db_tx_abort(&tx);
			goto confx_db_tx_err;
		}
	}

confx_db_enc_err:
	rc = rc ?: m0_db_tx_commit(&tx);
confx_db_tx_err:
	m0_confx_db_tables_fini(table, ARRAY_SIZE(table));
confx_db_table_err:
	m0_dbenv_fini(&db);
confx_db_env_err:
	M0_RETURN(rc);
}

/* ------------------------------------------------------------------
 * confdb: reader
 * ------------------------------------------------------------------ */

static int m0_confx_db_obj_count(struct m0_table *tables, struct m0_db_tx *tx)
{
	struct m0_db_pair      pair;
	struct m0_db_cursor    cursor;
	struct m0_confx_db_key key;
	char                  *val;
	int                    obj_nr;
	int                    i;
	int                    rc;

	M0_ENTRY();

	/* too large to be allocated in stack */
	M0_ALLOC_ARR(val, M0_CONF_XCODE_VAL_MAX);
	if (val == NULL)
		M0_RETURN(-ENOMEM);

	for (obj_nr = 0, i = M0_CO_PROFILE; i <= M0_CO_PARTITION; ++i) {
		m0_db_pair_setup(&pair, &tables[i],
				 &key, sizeof key, val, M0_CONF_XCODE_VAL_MAX);
		rc = m0_db_cursor_init(&cursor, &tables[i], tx, 0);
		if (rc != 0)
			break;

		for (rc = m0_db_cursor_first(&cursor, &pair); rc == 0;
		     rc = m0_db_cursor_next(&cursor, &pair))
			obj_nr++;

		m0_db_cursor_fini(&cursor);

		/* make sure we are in the end of the table */
		M0_ASSERT(rc == -ENOENT);
		rc = 0;
	}

	if (rc != 0)
		m0_db_cursor_fini(&cursor);
	m0_free(val);

	M0_RETURN(rc == 0 ? obj_nr : rc);
}

M0_INTERNAL int m0_confx_db_read(const char *db_name, struct confx_object **obj)
{
	struct m0_dbenv           db;
	struct m0_table           tables[ARRAY_SIZE(db_tables)];
	struct m0_db_tx           tx;
	struct m0_db_pair         pair;
	struct m0_db_cursor       cursor;
	struct m0_conf_xcode_pair xpair;
	struct m0_confx_db_key    key;
	char                     *val;
	int                       nr;
	int                       i;
	int                       rc;
	int                       obj_count = 0;

	M0_ENTRY();

	/* too large to be allocated in stack */
	M0_ALLOC_ARR(val, M0_CONF_XCODE_VAL_MAX);
	if (val == NULL)
		M0_RETURN(-ENOMEM);

	rc = m0_dbenv_init(&db, db_name, 0);
	if (rc != 0)
		goto m0_confx_db_read_env;

	rc = m0_confx_db_tables_init(tables, &db, ARRAY_SIZE(tables));
	if (rc != 0)
		goto m0_confx_db_read_tbl;

	rc = m0_db_tx_init(&tx, &db, 0);
	if (rc != 0)
		goto m0_confx_db_read_tx;

	obj_count = m0_confx_db_obj_count(tables, &tx);
	if (obj_count <= 0) {
		rc = (obj_count == 0) ? -ENOENT : obj_count;
		goto m0_confx_db_read_obj_alloc;
	}

	M0_ALLOC_ARR(*obj, obj_count);
	if (*obj == NULL) {
		rc = -ENOMEM;
		goto m0_confx_db_read_obj_alloc;
	}

	for (nr = 0, i = M0_CO_PROFILE; i <= M0_CO_PARTITION; ++i) {
		m0_db_pair_setup(&pair, &tables[i], &key, sizeof key, val,
				 M0_CONF_XCODE_VAL_MAX);
		rc = m0_db_cursor_init(&cursor, &tables[i], &tx, 0);
		if (rc != 0)
			break;

		for (rc = m0_db_cursor_first(&cursor, &pair); rc == 0;
		     rc = m0_db_cursor_next(&cursor, &pair)) {
			m0_buf_init(&xpair.xp_key, key.cdk_key, key.cdk_length);
			m0_buf_init(&xpair.xp_val, val, M0_CONF_XCODE_VAL_MAX);

			(*obj)[nr].o_conf.u_type = i;

			rc = m0_confx_decode(&xpair, &((*obj)[nr]));
			if (rc != 0)
				goto m0_confx_db_decode_err;

			nr++;
		}

		m0_db_cursor_fini(&cursor);

/* make sure we are in the end of the table */
		M0_ASSERT(rc == -ENOENT);
		rc = 0;
	}

m0_confx_db_decode_err:
	if (rc != 0)
		m0_db_cursor_fini(&cursor);
m0_confx_db_read_obj_alloc:
	rc = rc ?: m0_db_tx_commit(&tx);
m0_confx_db_read_tx:
	m0_confx_db_tables_fini(tables, ARRAY_SIZE(tables));
m0_confx_db_read_tbl:
	m0_dbenv_fini(&db);
m0_confx_db_read_env:
	m0_free(val);
	M0_RETURN(rc == 0 ? obj_count : rc);
}

#ifndef __KERNEL__
M0_INTERNAL int
m0_confx_str_read(const char *filename, struct confx_object **xobjs)
{
	static char buf[4096];
	FILE       *f;
	size_t      n;
	int         rc = 0;

	M0_ENTRY("filename=`%s'", filename);

	f = fopen(filename, "r");
	if (f == NULL)
		M0_RETURN(-errno);

	n = fread(buf, 1, sizeof buf - 1, f);
	if (ferror(f))
		rc = -errno;
	else if (!feof(f))
		rc = -EFBIG;
	else
		buf[n] = '\0';

	fclose(f);
	if (rc != 0)
		M0_RETURN(rc);

	n = m0_confx_obj_nr(buf);
	if (n == 0)
		M0_RETURN(rc);

	M0_ALLOC_ARR(*xobjs, n);
	M0_RETURN(*xobjs == NULL ? -ENOMEM : m0_conf_parse(buf, *xobjs, n));
}
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
