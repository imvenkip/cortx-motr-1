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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 30-Aug-2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/objs/common.h"
#include "mero/magic.h" /* M0_CONF_OBJ_MAGIC, M0_CONF_DIR_MAGIC */

M0_TL_DESCR_DEFINE(m0_conf_dir, "m0_conf_dir::cd_items", M0_INTERNAL,
		   struct m0_conf_obj, co_dir_link, co_gen_magic,
		   M0_CONF_OBJ_MAGIC, M0_CONF_DIR_MAGIC);
M0_TL_DEFINE(m0_conf_dir, M0_INTERNAL, struct m0_conf_obj);

static bool dir_check(const void *bob)
{
	const struct m0_conf_dir *self = bob;
	const struct m0_conf_obj *self_obj = &self->cd_obj;
	const struct m0_conf_obj *parent = self_obj->co_parent;

	M0_PRE(m0_conf_obj_tid(self_obj) == M0_CO_DIR);

	return ergo(self_obj->co_mounted, /* check relations */
		 _0C(parent->co_mounted && parent->co_status == M0_CS_READY) &&
		 _0C(M0_IN(m0_conf_obj_tid(parent),
			   (M0_CO_FILESYSTEM, M0_CO_NODE, M0_CO_SDEV))) &&
		 ergo(self_obj->co_status == M0_CS_READY,
			 m0_tl_forall(m0_conf_dir, child, &self->cd_items,
				      child_check(self_obj, child,
						      self->cd_item_type))));
}

M0_CONF__BOB_DEFINE(m0_conf_dir, M0_CONF_DIR_MAGIC, dir_check);

M0_CONF__INVARIANT_DEFINE(dir_invariant, m0_conf_dir);

static int dir_decode(struct m0_conf_obj *dest M0_UNUSED,
		      const struct m0_confx_obj *src M0_UNUSED,
		      struct m0_conf_cache *cache M0_UNUSED)
{
	M0_IMPOSSIBLE("m0_conf_dir is not supposed to be decoded");
	return -1;
}

static int dir_encode(struct m0_confx_obj *dest M0_UNUSED,
		      const struct m0_conf_obj *src M0_UNUSED)
{
	M0_IMPOSSIBLE("m0_conf_dir is not supposed to be encoded");
	return -1;
}

static bool dir_match(const struct m0_conf_obj *cached M0_UNUSED,
		      const struct m0_confx_obj *flat M0_UNUSED)
{
	M0_IMPOSSIBLE("m0_conf_dir should not be compared with m0_confx_obj");
	return false;
}

static bool
belongs(const struct m0_conf_obj *entry, const struct m0_conf_dir *dir)
{
	return  m0_conf_obj_type(entry) == dir->cd_item_type &&
		entry->co_parent == &dir->cd_obj;
}

/**
 * Precondition for m0_conf_obj_ops::coo_readdir().
 *
 * @param dir     The 1st argument of ->coo_readdir(), typecasted.
 * @param entry   The 2nd argument of ->coo_readdir(), dereferenced
 *                before the function is called (*pptr).
 *
 * @see m0_conf_obj_ops::coo_readdir()
 */
static bool
readdir_pre(const struct m0_conf_dir *dir, const struct m0_conf_obj *entry)
{
	return  dir->cd_obj.co_status == M0_CS_READY &&
		dir->cd_obj.co_nrefs > 0 &&
		ergo(entry != NULL, m0_conf_obj_invariant(entry) &&
		     belongs(entry, dir) && entry->co_nrefs > 0);
}

/**
 * Postcondition for m0_conf_obj_ops::coo_readdir().
 *
 * @param retval  The value returned by ->coo_readdir().
 * @param dir     The 1st argument of ->coo_readdir(), typecasted.
 * @param entry   The 2nd argument of ->coo_readdir(), dereferenced
 *                after the function is called (*pptr).
 *
 * @see m0_conf_obj_ops::coo_readdir()
 */
static bool readdir_post(int retval, const struct m0_conf_dir *dir,
			 const struct m0_conf_obj *entry)
{
	return  M0_IN(retval,
		      (M0_CONF_DIREND, M0_CONF_DIRNEXT, M0_CONF_DIRMISS)) &&
		(retval == M0_CONF_DIREND) == (entry == NULL) &&
		ergo(entry != NULL,
		     m0_conf_obj_invariant(entry) && belongs(entry, dir) &&
		     (retval == M0_CONF_DIRNEXT) == (entry->co_nrefs > 0));
}

static int dir_readdir(struct m0_conf_obj *dir, struct m0_conf_obj **pptr)
{
	struct m0_conf_obj *next;
	int                 ret;
	struct m0_conf_dir *d = M0_CONF_CAST(dir, m0_conf_dir);
	struct m0_conf_obj *prev = *pptr;

	M0_ENTRY();
	M0_PRE(readdir_pre(d, prev));

	if (prev == NULL) {
		next = m0_conf_dir_tlist_head(&d->cd_items);
	} else {
		next = m0_conf_dir_tlist_next(&d->cd_items, prev);
		m0_conf_obj_put(prev);
		*pptr = NULL;
	}

	if (next == NULL) {
		ret = M0_CONF_DIREND;
	} else if (next->co_status == M0_CS_READY) {
		m0_conf_obj_get(next);
		*pptr = next;
		ret = M0_CONF_DIRNEXT;
	} else {
		*pptr = next; /* let the caller know which object is missing */
		ret = M0_CONF_DIRMISS;
	}

	M0_POST(readdir_post(ret, d, *pptr));
	M0_LEAVE("retval=%d", ret);
	return ret;
}

static int dir_lookup(struct m0_conf_obj *parent, const struct m0_fid *name,
		      struct m0_conf_obj **out)
{
	struct m0_conf_obj *item;
	struct m0_conf_dir *x = M0_CONF_CAST(parent, m0_conf_dir);

	M0_PRE(parent->co_status == M0_CS_READY);

	m0_tl_for(m0_conf_dir, &x->cd_items, item) {
		if (m0_fid_eq(&item->co_id, name)) {
			*out = item;
			M0_POST(m0_conf_obj_invariant(*out));
			return 0;
		}
	} m0_tl_endfor;

	return -ENOENT;
}

static void dir_delete(struct m0_conf_obj *obj)
{
	struct m0_conf_obj *item;
	struct m0_conf_dir *x = M0_CONF_CAST(obj, m0_conf_dir);

	m0_tl_teardown(m0_conf_dir, &x->cd_items, item) {
		/* `item' is deleted by m0_conf_cache_fini(). */
	}
	m0_conf_dir_tlist_fini(&x->cd_items);
	m0_conf_dir_bob_fini(x);
	m0_free(x);
}

static const struct m0_conf_obj_ops dir_ops = {
	.coo_invariant = dir_invariant,
	.coo_decode    = dir_decode,
	.coo_encode    = dir_encode,
	.coo_match     = dir_match,
	.coo_lookup    = dir_lookup,
	.coo_readdir   = dir_readdir,
	.coo_delete    = dir_delete
};

static struct m0_conf_obj *dir_create(void)
{
	struct m0_conf_dir *x;
	struct m0_conf_obj *ret;

	M0_ALLOC_PTR(x);
	if (x == NULL)
		return NULL;
	m0_conf_dir_bob_init(x);

	/* Initialise concrete fields. */
	m0_conf_dir_tlist_init(&x->cd_items);

	ret = &x->cd_obj;
	ret->co_ops = &dir_ops;
	return ret;
}

const struct m0_conf_obj_type M0_CONF_DIR_TYPE = {
	.cot_ftype = {
		.ft_id   = 'D',
		.ft_name = "configuration directory",
	},
	.cot_id    = M0_CO_DIR,
	.cot_ctor  = &dir_create,
	.cot_magic = M0_CONF_DIR_MAGIC
};

const struct m0_fid_type M0_CONF_RELFID_TYPE = {
	.ft_id   = '/',
	.ft_name = "relation",
};

#undef M0_TRACE_SUBSYSTEM
