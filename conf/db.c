/* -*- c -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>
 * Original creation date: 25-Sep-2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/db.h"
#include "conf/onwire.h"     /* m0_confx_obj, m0_confx */
#include "conf/onwire_xc.h"
#include "conf/obj.h"        /* m0_conf_objtype */
#include "xcode/xcode.h"
#include "db/db.h"
#include "lib/memory.h"      /* m0_alloc, m0_free */
#include "lib/errno.h"       /* EINVAL */
#include "lib/misc.h"        /* M0_SET0 */

/* ------------------------------------------------------------------
 * xcoding: m0_confx_obj <--> raw buffer
 * ------------------------------------------------------------------ */

static void *
_conf_xcode_alloc(struct m0_xcode_cursor *ctx M0_UNUSED, size_t nob)
{
	return m0_alloc(nob);
}

static enum m0_conf_objtype xobj_type(const struct m0_confx_obj *xobj)
{
	uint32_t t = xobj->o_conf.u_type;

	M0_CASSERT(M0_CO_DIR == 0);
	M0_POST(t < M0_CO_NR && t != M0_CO_DIR);
	return t;
}

/* Note: m0_xcode_ctx_init() doesn't allow `xobj' to be const. Sigh. */
static int
xcode_ctx_init(struct m0_xcode_ctx *ctx, struct m0_confx_obj *xobj, bool decode)
{
	M0_ENTRY();

	switch(xobj_type(xobj)) {
#define _CASE(type, abbrev)                                                    \
	case type:                                                             \
		m0_xcode_ctx_init(                                             \
			ctx,                                                   \
			&M0_XCODE_OBJ(                                         \
				m0_confx_ ## abbrev ## _xc,                    \
				decode ? NULL : &xobj->o_conf.u.u_ ## abbrev));\
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
		M0_RETERR(-EINVAL, "Invalid object type: %u", xobj_type(xobj));
	}

	if (decode)
		ctx->xcx_alloc = _conf_xcode_alloc;

	M0_RETURN(0);
}

static int confx_obj_measure(struct m0_confx_obj *xobj, m0_bcount_t *result)
{
	struct m0_xcode_ctx ctx;
	int                 rc;

	M0_ENTRY();

	rc = xcode_ctx_init(&ctx, xobj, false);
	if (rc != 0)
		M0_RETURN(rc);

	rc = m0_xcode_length(&ctx);
	if (rc < 0)
		M0_RETURN(rc);
	M0_ASSERT(rc != 0); /* XXX How can we be so sure? */

	*result = rc;
	M0_RETURN(0);
}

/* ------------------------------------------------------------------
 * confdb_obj
 * ------------------------------------------------------------------ */

/**
 * Database representation of a configuration object.
 *
 * confdb_obj can be stored in a database.
 */
struct confdb_obj {
	struct m0_buf do_key; /*< Object identifier. */
	struct m0_buf do_rec; /*< Object fields. */
};

/**
 * Encodes m0_confx_obj into the corresponding database representation.
 *
 * If the call succeeds, the user is responsible for freeing allocated memory:
 * @code
 *         m0_buf_free(&dest->do_rec);
 * @endcode
 *
 * @note  xcode API doesn't let `src' to be const.
 */
static int confx_to_db(struct m0_confx_obj *src, struct confdb_obj *dest)
{
	struct m0_xcode_ctx ctx;
	int                 rc;
	void               *buf;
	m0_bcount_t         len;
	struct m0_bufvec    bvec = M0_BUFVEC_INIT_BUF(&buf, &len);

	M0_ENTRY();

	rc = confx_obj_measure(src, &len) ?: xcode_ctx_init(&ctx, src, false);
	if (rc != 0)
		M0_RETURN(rc);

	M0_ALLOC_ARR(buf, len);
	if (buf == NULL)
		M0_RETURN(-ENOMEM);

	m0_bufvec_cursor_init(&ctx.xcx_buf, &bvec);
	rc = m0_xcode_encode(&ctx);
	if (rc != 0) {
		m0_free(buf);
		M0_RETURN(rc);
	}

	dest->do_key = src->o_id;
	m0_buf_init(&dest->do_rec, buf, len);

	M0_RETURN(0);
}

/**
 * Decodes m0_confx_obj from its database representation.
 *
 * @note  XXX User is responsible for freeing `dest' array with
 *        m0_confx_free().
 */
static int confx_from_db(struct m0_confx_obj *dest, enum m0_conf_objtype type,
			 struct confdb_obj *src)
{
	struct m0_xcode_ctx ctx;
	int                 rc;
	struct m0_bufvec    bvec = M0_BUFVEC_INIT_BUF(&src->do_rec.b_addr,
						      &src->do_rec.b_nob);
	M0_ENTRY();
	M0_PRE(M0_CO_DIR < type && type < M0_CO_NR);

	dest->o_conf.u_type = type;

	rc = xcode_ctx_init(&ctx, dest, true);
	if (rc != 0)
		M0_RETURN(rc);

	m0_bufvec_cursor_init(&ctx.xcx_buf, &bvec);
	rc = m0_xcode_decode(&ctx);
	if (rc != 0)
		M0_RETURN(rc);

	switch(type) {
#define _CASE(type, abbrev)                                                   \
	case type:                                                            \
		dest->o_conf.u.u_ ## abbrev = *(struct m0_confx_ ## abbrev *) \
			ctx.xcx_it.xcu_stack[0].s_obj.xo_ptr;                 \
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
		M0_IMPOSSIBLE("filtered out by xcode_ctx_init()");
		M0_RETURN(-EINVAL);
	}

	M0_SET0(&dest->o_id); /* to satisfy the precondition of m0_buf_copy() */
	m0_buf_copy(&dest->o_id, &src->do_key);

	/* Free the memory allocated by m0_xcode_decode(). */
	m0_free(ctx.xcx_it.xcu_stack[0].s_obj.xo_ptr);

	M0_RETURN(rc);
}

/* ------------------------------------------------------------------
 * Tables, confdb_key
 * ------------------------------------------------------------------ */

enum {
	CONFDB_SRV_EP_MAX = 16,
	CONFDB_FS_MAX     = 16,
	CONFDB_NICS_MAX   = 16,
	CONFDB_SDEVS_MAX  = 16,
	CONFDB_PART_MAX   = 16,
	CONFDB_UUID_SIZE  = 40,
	CONFDB_NAME_LEN   = 256,

	/* XXX FIXME: very inaccurate estimations */
	CONFDB_REC_MAX    = sizeof(struct m0_confx_obj) + CONFDB_NAME_LEN * (
		CONFDB_SRV_EP_MAX + CONFDB_FS_MAX + CONFDB_NICS_MAX +
		CONFDB_SDEVS_MAX + CONFDB_PART_MAX)
};

/** XXX DOCUMENTME */
struct confdb_key {
	uint8_t cdk_len;
	char    cdk_key[CONFDB_UUID_SIZE];
};

static int
confdb_key_cmp(struct m0_table *_ M0_UNUSED, const void *key0, const void *key1)
{
	const struct confdb_key *k0 = key0;
	const struct confdb_key *k1 = key1;

	return memcmp(k0->cdk_key, k1->cdk_key,
		      min_check(k0->cdk_len, k1->cdk_len));
}

static const struct m0_table_ops table_ops = {
	.to = {
		[TO_KEY] = { .max_size = sizeof(struct confdb_key) },
		[TO_REC] = { .max_size = CONFDB_REC_MAX }
	},
	.key_cmp = confdb_key_cmp
};

static void
confdb_obj_to_key(const struct confdb_obj *src, struct confdb_key *dest)
{
	const struct m0_buf *k = &src->do_key;

	M0_ENTRY();
	M0_PRE(k->b_nob < sizeof dest->cdk_key);

	dest->cdk_len = k->b_nob;
	memcpy(dest->cdk_key, k->b_addr, k->b_nob);

	M0_LEAVE();
}

/* XXX Consider using "The X macro":
 * http://www.drdobbs.com/cpp/the-x-macro/228700289 */
static const char *table_names[] = {
	[M0_CO_DIR]        = NULL,
	[M0_CO_PROFILE]    = "profile",
	[M0_CO_FILESYSTEM] = "filesystem",
	[M0_CO_SERVICE]    = "service",
	[M0_CO_NODE]       = "node",
	[M0_CO_NIC]        = "nic",
	[M0_CO_SDEV]       = "sdev",
	[M0_CO_PARTITION]  = "partition"
};
M0_BASSERT(ARRAY_SIZE(table_names) == M0_CO_NR);

static void confdb_tables_fini(struct m0_table *tables, size_t n)
{
	M0_ENTRY();
	while (n > 0) {
		--n;
		if (table_names[n] != NULL)
			m0_table_fini(&tables[n]);
	}
	M0_LEAVE();
}

static int
confdb_tables_init(struct m0_dbenv *db, struct m0_table *tables, size_t n)
{
	int i;
	int rc = 0;

	M0_ENTRY();
	M0_PRE(n == ARRAY_SIZE(table_names));

	for (i = 0; i < n; ++i) {
		if (table_names[i] == NULL)
			continue;

		rc = m0_table_init(&tables[i], db, table_names[i], 0,
				   &table_ops);
		if (rc != 0) {
			confdb_tables_fini(tables, i);
			break;
		}
	}
	M0_RETURN(rc);
}

/* ------------------------------------------------------------------
 * Database operations
 * ------------------------------------------------------------------ */

M0_INTERNAL int
m0_confdb_create(const char *dbpath, const struct m0_confx *conf)
{
	struct m0_dbenv   db;
	struct m0_table  *tables;
	struct m0_db_tx   tx;
	struct m0_db_pair pair;
	struct confdb_obj db_obj;
	struct confdb_key key;
	int               i;
	int               rc;

	M0_ENTRY();
	M0_PRE(conf->cx_nr > 0);

	M0_ALLOC_ARR(tables, ARRAY_SIZE(table_names));
	if (tables == NULL)
		M0_RETURN(-ENOMEM);

	rc = m0_dbenv_init(&db, dbpath, 0);
	if (rc != 0)
		goto tables_free;

	rc = confdb_tables_init(&db, tables, ARRAY_SIZE(table_names));
	if (rc != 0)
		goto dbenv_fini;

	rc = m0_db_tx_init(&tx, &db, 0);
	if (rc != 0)
		goto tables_fini;

	for (i = 0; i < conf->cx_nr && rc == 0; ++i) {
		M0_ASSERT(IS_IN_ARRAY(xobj_type(&conf->cx_objs[i]),
				      table_names));
		rc = confx_to_db(&conf->cx_objs[i], &db_obj);
		if (rc != 0)
			break;

		confdb_obj_to_key(&db_obj, &key);
		m0_db_pair_setup(&pair, &tables[xobj_type(&conf->cx_objs[i])],
				 &key, sizeof key, db_obj.do_rec.b_addr,
				 db_obj.do_rec.b_nob);

		rc = m0_table_update(&tx, &pair);

		m0_db_pair_fini(&pair);
		m0_buf_free(&db_obj.do_rec);
	}
	if (rc == 0)
		rc = m0_db_tx_commit(&tx);
	else
		(void)m0_db_tx_abort(&tx);

tables_fini:
	confdb_tables_fini(tables, ARRAY_SIZE(table_names));
dbenv_fini:
	m0_dbenv_fini(&db);
tables_free:
	m0_free(tables);
	M0_RETURN(rc);
}

/* XXX FIXME: Other functions receive both `tables' and the number of tables.
 * confdb_objs_count() shouldn't be different. */
static int
confdb_objs_count(struct m0_table *tables, struct m0_db_tx *tx, size_t *result)
{
	struct m0_db_pair   pair;
	struct m0_db_cursor cur;
	struct confdb_key   key;
	char               *rec;
	int                 t;
	int                 rc;

	M0_ENTRY();

	/* Too large to be allocated on stack. */
	M0_ALLOC_ARR(rec, CONFDB_REC_MAX);
	if (rec == NULL)
		M0_RETURN(-ENOMEM);

	*result = 0;
	for (t = M0_CO_PROFILE; t < M0_CO_NR; ++t) {
		m0_db_pair_setup(&pair, &tables[t], &key, sizeof key, rec,
				 CONFDB_REC_MAX);
		rc = m0_db_cursor_init(&cur, &tables[t], tx, 0);
		if (rc != 0)
			break;

		for (rc = m0_db_cursor_first(&cur, &pair); rc == 0;
		     rc = m0_db_cursor_next(&cur, &pair))
			++*result;

		m0_db_cursor_fini(&cur);

		/* Make sure we are at the end of the table. */
		M0_ASSERT(rc == -ENOENT);
		rc = 0;
	}

	m0_free(rec);
	M0_RETURN(rc);
}

static struct m0_confx *confx_alloc(size_t nr_objs)
{
	struct m0_confx *ret;

	M0_PRE(nr_objs > 0);

	M0_ALLOC_PTR(ret);
	if (ret == NULL)
		return NULL;

	M0_ALLOC_ARR(ret->cx_objs, nr_objs);
	if (ret->cx_objs == NULL) {
		m0_free(ret);
		return NULL;
	}

	ret->cx_nr = nr_objs;
	return ret;
}

static void confx_free(struct m0_confx *enc)
{
	m0_free(enc->cx_objs);
	m0_free(enc);
}

static int
confx_fill(struct m0_confx *dest, struct m0_table *tables, struct m0_db_tx *tx)
{
	struct m0_db_cursor cur;
	struct m0_db_pair   pair;
	struct confdb_key   key;
	char               *rec;
	size_t              ti;    /* index in tables[] */
	int                 rc;
	size_t              i = 0; /* index in dest->cx_objs[] */

	M0_ENTRY();
	M0_PRE(dest->cx_nr > 0);

	/* Too large to be allocated on stack. */
	M0_ALLOC_ARR(rec, CONFDB_REC_MAX);
	if (rec == NULL)
		M0_RETURN(-ENOMEM);

	for (ti = M0_CO_PROFILE; ti <= M0_CO_PARTITION; ++ti) {
		m0_db_pair_setup(&pair, &tables[ti], &key, sizeof key, rec,
				 CONFDB_REC_MAX);
		rc = m0_db_cursor_init(&cur, &tables[ti], tx, 0);
		if (rc != 0)
			break;

		for (rc = m0_db_cursor_first(&cur, &pair); rc == 0;
		     rc = m0_db_cursor_next(&cur, &pair), ++i) {
			struct confdb_obj x = {
				.do_key = M0_BUF_INIT(key.cdk_len, key.cdk_key),
				.do_rec = M0_BUF_INIT(CONFDB_REC_MAX, rec)
			};

			M0_ASSERT(i < dest->cx_nr);
			rc = confx_from_db(&dest->cx_objs[i], ti, &x);
			if (rc != 0) {
				m0_db_cursor_fini(&cur);
				goto out;
			}
		}

		m0_db_cursor_fini(&cur);
		M0_ASSERT(rc == -ENOENT); /* end of the table */
		rc = 0;
	}
out:
	if (rc == 0) {
		M0_POST(i == dest->cx_nr);
	} else {
		for (; i > 0; --i)
			m0_xcode_free(&M0_XCODE_OBJ(m0_confx_obj_xc,
						    &dest->cx_objs[i]));
		M0_SET0(dest);
	}
	m0_free(rec);
	M0_RETURN(rc);
}

M0_INTERNAL int m0_confdb_read(const char *dbpath, struct m0_confx **out)
{
	struct m0_dbenv     db;
	struct m0_table    *tables;
	struct m0_db_tx     tx;
	int                 rc;
	size_t              nr_objs = 0;

	M0_ENTRY();

	M0_ALLOC_ARR(tables, ARRAY_SIZE(table_names));
	if (tables == NULL)
		M0_RETURN(-ENOMEM);

	rc = m0_dbenv_init(&db, dbpath, 0);
	if (rc != 0)
		goto tables_free;

	rc = confdb_tables_init(&db, tables, ARRAY_SIZE(table_names));
	if (rc != 0)
		goto dbenv_fini;

	rc = m0_db_tx_init(&tx, &db, 0);
	if (rc != 0)
		goto tables_fini;

	rc = confdb_objs_count(tables, &tx, &nr_objs);
	if (rc != 0)
		goto db_tx;
	if (nr_objs == 0) {
		rc = -ENODATA;
		goto db_tx;
	}

	*out = confx_alloc(nr_objs);
	if (*out == NULL) {
		rc = -ENOMEM;
		goto db_tx;
	}

	rc = confx_fill(*out, tables, &tx);
	if (rc != 0) {
		confx_free(*out);
		*out = NULL;
	}
db_tx:
	if (rc == 0)
		rc = m0_db_tx_commit(&tx);
	else
		(void)m0_db_tx_abort(&tx);
tables_fini:
	confdb_tables_fini(tables, ARRAY_SIZE(table_names));
dbenv_fini:
	m0_dbenv_fini(&db);
tables_free:
	m0_free(tables);
	M0_RETURN(rc);
}

#undef M0_TRACE_SUBSYSTEM
