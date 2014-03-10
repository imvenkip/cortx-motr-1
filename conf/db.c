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
#include "lib/finject.h"

#include "conf/db.h"
#include "conf/onwire.h"     /* m0_confx_obj, m0_confx */
#include "conf/onwire_xc.h"
#include "conf/obj.h"        /* m0_conf_objtype */
#include "xcode/xcode.h"
#include "db/db.h"
#include "be/btree.h"
#include "be/tx.h"
#include "be/op.h"
#include "lib/memory.h"      /* m0_alloc, m0_free */
#include "lib/errno.h"       /* EINVAL */
#include "lib/misc.h"        /* M0_SET0 */

static int
confdb_objs_count(struct m0_be_btree *btrees[], size_t *result);

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

static int confx_to_xcode_obj(struct m0_confx_obj *xobj,
			      struct m0_xcode_obj *out, bool decode)
{
	M0_PRE(xobj != NULL && out != NULL);

	switch(xobj_type(xobj)) {
#define _CASE(type, abbrev)                                                         \
	case type:                                                                  \
		*out =  M0_XCODE_OBJ(m0_confx_ ## abbrev ## _xc,                    \
				     decode ? NULL : &xobj->o_conf.u.u_ ## abbrev); \
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

	return M0_RC(0);
}

/* Note: m0_xcode_ctx_init() doesn't allow `xobj' to be const. Sigh. */
static int
xcode_ctx_init(struct m0_xcode_ctx *ctx, struct m0_confx_obj *xobj, bool decode)
{
	struct m0_xcode_obj obj;

	M0_ENTRY();

	confx_to_xcode_obj(xobj, &obj, decode);
	m0_xcode_ctx_init(ctx, &obj);
	if (decode)
		ctx->xcx_alloc = _conf_xcode_alloc;

	return M0_RC(0);
}

static int confx_obj_measure(struct m0_confx_obj *xobj, m0_bcount_t *result)
{
	struct m0_xcode_ctx ctx;
	int                 rc;

	M0_ENTRY();

	rc = xcode_ctx_init(&ctx, xobj, false);
	if (rc != 0)
		return M0_ERR(rc);

	rc = m0_xcode_length(&ctx);
	if (rc < 0)
		return M0_ERR(rc);
	M0_ASSERT(rc != 0); /* XXX How can we be so sure? */

	*result = rc;
	return M0_RC(0);
}

/* ------------------------------------------------------------------
 * confdb_key, confdb_obj
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
confdb_key_cmp(const void *key0, const void *key1)
{
	const struct confdb_key *k0 = key0;
	const struct confdb_key *k1 = key1;

	return memcmp(k0->cdk_key, k1->cdk_key,
		      min_check(k0->cdk_len, k1->cdk_len));
}

static m0_bcount_t confdb_ksize(const void *key)
{
	return sizeof(struct confdb_key);
}

static m0_bcount_t confdb_vsize(const void *val)
{
	return sizeof(struct m0_confx_u);
}

static const struct m0_be_btree_kv_ops confdb_ops = {
	.ko_ksize   = confdb_ksize,
	.ko_vsize   = confdb_vsize,
	.ko_compare = confdb_key_cmp
};

/**
 * Database representation of a configuration object.
 *
 * confdb_obj can be stored in a database.
 */
struct confdb_obj {
	struct m0_buf do_key; /*< Object identifier. */
	struct m0_buf do_rec; /*< Object fields. */
};

static void confx_to_dbkey(struct m0_confx_obj *src, struct confdb_key *key)
{
	key->cdk_len = src->o_id.b_nob;
	memcpy(key->cdk_key, src->o_id.b_addr, key->cdk_len);
}

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
static void confx_to_db(struct m0_confx_obj *src, struct confdb_key *key,
			struct confdb_obj *dest)
{
	M0_ENTRY();

	m0_buf_init(&dest->do_key, key, sizeof(struct confdb_key));
	m0_buf_init(&dest->do_rec, &src->o_conf, sizeof(struct m0_confx_u));
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
	struct confdb_key *key;
	struct m0_buf         buf;
	int                   rc;
	M0_ENTRY();
	M0_PRE(M0_CO_DIR < type && type < M0_CO_NR);

	M0_SET0(&dest->o_id); /* to satisfy the precondition of m0_buf_copy() */
	key = (struct confdb_key *)src->do_key.b_addr;
	m0_buf_init(&buf, key->cdk_key, key->cdk_len);
	rc = m0_buf_copy(&dest->o_id, &buf);
	if (rc == 0)
		dest->o_conf = *(struct m0_confx_u *)src->do_rec.b_addr;

	return M0_RCN(rc);
}

/* ------------------------------------------------------------------
 * Tables
 * ------------------------------------------------------------------ */
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

static void confdb_tables_fini(struct m0_be_seg *seg)
{
	struct m0_be_btree *btree;
	int                 i;
	int                 rc;

	M0_ENTRY();

	for (i = 0; i < ARRAY_SIZE(table_names); ++i) {
		if (table_names[i] == NULL)
			continue;
		rc = m0_be_seg_dict_lookup(seg, table_names[i],
					   (void **)&btree);
		if (rc == 0)
			m0_be_btree_fini(btree);
	}

	M0_LEAVE();
}

static int
confdb_tables_init(struct m0_be_seg *seg, struct m0_be_btree *btrees[],
		   struct m0_be_tx *tx)
{
	int                 i;
	int                 rc = 0;

	M0_ENTRY();

	for (i = 0; i < M0_CO_NR; ++i) {
		if (table_names[i] == NULL)
			continue;
		M0_BE_ALLOC_PTR_SYNC(btrees[i], seg, tx);
		m0_be_btree_init(btrees[i], seg, &confdb_ops);
		M0_BE_OP_SYNC(op, m0_be_btree_create(btrees[i], tx, &op));
		rc = m0_be_seg_dict_insert(seg, tx, table_names[i], btrees[i]);
		if (rc != 0)
			break;
	}

	if (M0_FI_ENABLED("ut_confdb_create_failure"))
		rc = -EINVAL;
	if (rc != 0) {
		confdb_tables_fini(seg);
		m0_confdb_destroy(seg, tx);
	}

	return M0_RCN(rc);
}

/* ------------------------------------------------------------------
 * Database operations
 * ------------------------------------------------------------------ */
static int confx_obj_dup(struct m0_confx_obj *dest, struct m0_confx_obj *src,
			 struct m0_be_seg *seg, struct m0_be_tx *tx)
{
	struct m0_xcode_obj    src_obj;
	struct m0_xcode_obj    dest_obj;
	int                    rc;

	M0_BE_ALLOC_ARR_SYNC(dest->o_id.b_addr, src->o_id.b_nob, seg, tx);
	dest->o_id.b_nob = src->o_id.b_nob;
	memcpy(dest->o_id.b_addr, src->o_id.b_addr, src->o_id.b_nob);
	dest->o_conf.u_type = src->o_conf.u_type;
	rc = confx_to_xcode_obj(dest, &dest_obj, false) ?:
		confx_to_xcode_obj(src, &src_obj, false);
	if (M0_FI_ENABLED("ut_confx_obj_dup_failure"))
		rc = -EINVAL;
	if (rc == 0)
		rc = m0_xcode_be_dup(&dest_obj, &src_obj, seg, tx);

	return rc;
}

M0_INTERNAL int m0_confdb_create_credit(struct m0_be_seg *seg,
					const struct m0_confx *conf,
					struct m0_be_tx_credit *accum)
{
	struct m0_be_btree   btree = { .bb_seg = seg };
	m0_bcount_t          len;
	m0_bcount_t          ksize;
	struct m0_confx_obj *obj;
	int                  rc;
	int                  i;

	m0_be_btree_create_credit(&btree, ARRAY_SIZE(table_names), accum);
	for (i = 0; i < ARRAY_SIZE(table_names); ++i) {
		if (table_names[i] == NULL)
			continue;
		M0_BE_ALLOC_CREDIT_PTR(&btree, seg, accum);
		m0_be_seg_dict_insert_credit(seg, table_names[i], accum);
	}

	for (i = 0; i < conf->cx_nr; ++i) {
		obj = &conf->cx_objs[i];
		M0_ASSERT(IS_IN_ARRAY(xobj_type(obj), table_names));
		rc = confx_obj_measure(obj, &len);
		if (rc != 0)
			return M0_ERR(rc);
		len += sizeof(obj->o_conf.u_type);
		ksize = sizeof(struct confdb_key);
		m0_be_btree_insert_credit(&btree, 1, ksize, len, accum);
	}

	return M0_RC(0);
}

M0_INTERNAL int m0_confdb_destroy_credit(struct m0_be_seg *seg,
					 struct m0_be_tx_credit *accum)
{
	struct m0_be_btree *btree;
	int                 i;
	int                 rc = 0;

	for (i = 0; i < ARRAY_SIZE(table_names); ++i) {
		if (table_names[i] == NULL)
			continue;
		rc = m0_be_seg_dict_lookup(seg, table_names[i],
					   (void **)&btree);
		if (rc != 0)
			return M0_ERR(rc);
		m0_be_btree_destroy_credit(btree, 1, accum);
		M0_BE_FREE_CREDIT_PTR(btree, seg, accum);
	}

	return M0_RCN(rc);
}

M0_INTERNAL int m0_confdb_destroy(struct m0_be_seg *seg, struct m0_be_tx *tx)
{
	struct m0_be_btree     *btree;
	int                     i;
	int                     rc = 0;

	/*
	 * FIXME: Does not free the internal be objects allocated during
	 *        confdb_create as part of xcode_dup operation.
	 */

	for (i = 0; i < ARRAY_SIZE(table_names); ++i) {
		if (table_names[i] == NULL)
			continue;
		rc = m0_be_seg_dict_lookup(seg, table_names[i],
					   (void **)&btree);
		if (rc != 0)
			return M0_ERR(rc);
		M0_BE_OP_SYNC(op, m0_be_btree_destroy(btree, tx, &op));
		M0_BE_FREE_PTR_SYNC(btree, seg, tx);
		rc = m0_be_seg_dict_delete(seg, tx, table_names[i]);
		if (rc != 0)
			return M0_ERR(rc);
	}

	return M0_RCN(rc);
}

M0_INTERNAL void m0_confdb_fini(struct m0_be_seg *seg)
{
	confdb_tables_fini(seg);
}

M0_INTERNAL int m0_confdb_create(struct m0_be_seg *seg, struct m0_be_tx *tx,
				 const struct m0_confx *conf)
{
	struct m0_be_btree  *btrees[ARRAY_SIZE(table_names)];
	struct m0_be_btree  *btree;
	struct m0_confx_obj *confx_objs;
	struct confdb_key    db_key;
	struct confdb_obj    db_obj;
	m0_bcount_t          nr_objs;
	int                  i;
	int                  rc;

	M0_ENTRY();
	M0_PRE(conf->cx_nr > 0);

	rc = confdb_tables_init(seg, btrees, tx);
	if (rc != 0)
		return rc;
	M0_ALLOC_ARR(confx_objs, conf->cx_nr);
	for (i = 0; i < conf->cx_nr && rc == 0; ++i) {
		M0_ASSERT(IS_IN_ARRAY(xobj_type(&conf->cx_objs[i]),
				      table_names));
		rc = confx_obj_dup(&confx_objs[i], &conf->cx_objs[i], seg, tx);
		if (rc != 0)
			break;
		M0_SET0(&db_obj);
		confx_to_dbkey(&confx_objs[i], &db_key);
		confx_to_db(&confx_objs[i], &db_key, &db_obj);
		btree = btrees[xobj_type(&conf->cx_objs[i])];
		M0_BE_OP_SYNC(op, m0_be_btree_insert(btree, tx, &op,
						     &db_obj.do_key,
						     &db_obj.do_rec));
	}
	if (rc == 0) {
		rc = confdb_objs_count(btrees, &nr_objs);
		M0_ASSERT(rc == 0);
	}
	if (rc !=0) {
		confdb_tables_fini(seg);
		m0_confdb_destroy(seg, tx);
	}
	m0_free(confx_objs);
	return M0_RCN(rc);
}

/* XXX FIXME: Other functions receive both `tables' and the number of tables.
 * confdb_objs_count() shouldn't be different. */
static int
confdb_objs_count(struct m0_be_btree *btrees[], size_t *result)
{
	struct m0_be_btree_cursor  bcur;
	int                        t;
	int                        rc;

	M0_ENTRY();

	/* Too large to be allocated on stack. */
	*result = 0;
	for (t = M0_CO_PROFILE; t < M0_CO_NR; ++t) {
		m0_be_btree_cursor_init(&bcur, btrees[t]);
		for (rc = m0_be_btree_cursor_first_sync(&bcur); rc == 0;
		     rc = m0_be_btree_cursor_next_sync(&bcur)) {
			++*result;
		}
		m0_be_btree_cursor_fini(&bcur);
		/* Make sure we are at the end of the table. */
		M0_ASSERT(rc == -ENOENT);
		rc = 0;
	}

	return M0_RCN(rc);
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
confx_fill(struct m0_confx *dest, struct m0_be_btree *btrees[])
{
	struct m0_be_btree_cursor bcur;
	size_t                    ti;    /* index in tables[] */
	int                       rc;
	size_t                    i = 0; /* index in dest->cx_objs[] */

	M0_ENTRY();
	M0_PRE(dest->cx_nr > 0);

	for (ti = M0_CO_PROFILE; ti <= M0_CO_PARTITION; ++ti) {
		m0_be_btree_cursor_init(&bcur, btrees[ti]);
		for (rc = m0_be_btree_cursor_first_sync(&bcur); rc == 0;
		     rc = m0_be_btree_cursor_next_sync(&bcur), ++i) {
			struct confdb_obj dbobj;
			m0_be_btree_cursor_kv_get(&bcur, &dbobj.do_key,
						  &dbobj.do_rec);
			M0_ASSERT(i < dest->cx_nr);
			rc = confx_from_db(&dest->cx_objs[i], ti, &dbobj);
			if (rc != 0) {
				m0_be_btree_cursor_fini(&bcur);
				goto out;
			}
		}
		m0_be_btree_cursor_fini(&bcur);
		M0_ASSERT(rc == -ENOENT); /* end of the table */
		rc = 0;
	}
out:
	if (rc == 0) {
		M0_POST(i == dest->cx_nr);
	} else {
		for (; i > 0; --i)
			m0_free(dest->cx_objs[i].o_id.b_addr);
		confx_free(dest);
		M0_SET0(dest);
	}
	return M0_RCN(rc);
}

M0_INTERNAL int m0_confdb_read(struct m0_be_seg *seg, struct m0_confx **out)
{
	struct m0_be_btree *btrees[ARRAY_SIZE(table_names)];
	int                 i;
	int                 rc;
	size_t              nr_objs = 0;

	M0_ENTRY();

	for (i = 0; i < ARRAY_SIZE(table_names); ++i) {
		if (table_names[i] == NULL)
			continue;
		rc = m0_be_seg_dict_lookup(seg, table_names[i],
					   (void **)&btrees[i]);
		if (rc != 0)
			return rc;
	}
	rc = confdb_objs_count(btrees, &nr_objs);
	if (rc != 0)
		goto out;
	if (nr_objs == 0) {
		rc = -ENODATA;
		goto out;
	}
	*out = confx_alloc(nr_objs);
	if (*out == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	rc = confx_fill(*out, btrees);
	if (rc != 0) {
		confx_free(*out);
		*out = NULL;
	}
out:
	return M0_RCN(rc);
}

#undef M0_TRACE_SUBSYSTEM
