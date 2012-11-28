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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 30-Aug-2012
 */

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

	M0_PRE(self_obj->co_type == M0_CO_DIR);

	return ergo(self_obj->co_status == M0_CS_READY,
		    M0_IN(self->cd_item_type, (M0_CO_SERVICE, M0_CO_NIC,
					       M0_CO_SDEV, M0_CO_PARTITION))) &&
		ergo(self_obj->co_mounted, /* check relations */
		     parent->co_mounted && parent->co_status == M0_CS_READY &&
		     M0_IN(parent->co_type,
			   (M0_CO_FILESYSTEM, M0_CO_NODE, M0_CO_SDEV)) &&
		     ergo(self_obj->co_status == M0_CS_READY,
			  m0_tl_forall(m0_conf_dir, child, &self->cd_items,
				       child_check(self_obj, child,
						   self->cd_item_type))));
}

M0_CONF__BOB_DEFINE(m0_conf_dir, M0_CONF_DIR_MAGIC, dir_check);

M0_CONF__INVARIANT_DEFINE(dir_invariant, m0_conf_dir);

static int dir_fill(struct m0_conf_obj *dest __attribute__((unused)),
		    const struct confx_object *src __attribute__((unused)),
		    struct m0_conf_reg *reg __attribute__((unused)))
{
	M0_IMPOSSIBLE("m0_conf_dir should not be filled explicitly");
	return -1;
}

static bool dir_match(const struct m0_conf_obj *cached __attribute__((unused)),
		      const struct confx_object *flat __attribute__((unused)))
{
	M0_IMPOSSIBLE("m0_conf_dir should not be compared with confx_object");
	return false;
}

/* static bool */
/* belongs(const struct m0_conf_obj *entry, const struct m0_conf_obj *dir) */
/* { */
/* 	const struct m0_conf_dir *d = bob_of(dir, const struct m0_conf_dir, */
/* 					     m0_conf_dir_cast_field, */
/* 					     &m0_conf_dir_bob); */
/* 	return d->cd_item_type == entry->co_type && entry->co_parent == dir; */
/* } */

/* /\** */
/*  * Precondition for m0_conf_obj_ops::coo_readdir(). */
/*  * */
/*  * @param dir     The 1st argument of ->coo_readdir(). */
/*  * @param entry   The 2nd argument of ->coo_readdir(), dereferenced */
/*  *                before the function is called (*pptr). */
/*  * */
/*  * @see m0_conf_obj_ops::coo_readdir() */
/*  *\/ */
/* static bool */
/* readdir_pre(const struct m0_conf_obj *dir, const struct m0_conf_obj *entry) */
/* { */
/* 	return obj_invariant(dir) && obj_invariant(entry) && */
/* 		dir->co_type == M0_CO_DIR && dir->co_nrefs > 0 && */
/* 		ergo(entry != NULL, belongs(entry, dir) && entry->co_nrefs > 0); */
/* } */

/* /\** */
/*  * Postcondition for m0_conf_obj_ops::coo_readdir(). */
/*  * */
/*  * @param retval  The value returned by ->coo_readdir(). */
/*  * @param dir     The 1st argument of ->coo_readdir(). */
/*  * @param entry   The 2nd argument of ->coo_readdir(), dereferenced */
/*  *                after the function is called (*pptr). */
/*  * */
/*  * @see m0_conf_obj_ops::coo_readdir() */
/*  *\/ */
/* static bool readdir_post(int retval, const struct m0_conf_obj *dir, */
/* 			 const struct m0_conf_obj *entry) */
/* { */
/* 	return obj_invariant(dir) && obj_invariant(entry) && */
/* 		M0_IN(retval, */
/* 		     (M0_CONF_DIREND, M0_CONF_DIRNEXT, M0_CONF_DIRMISS)) && */
/* 		(retval == M0_CONF_DIRNEXT ? */
/* 		 (entry != NULL && belongs(entry, dir) && entry->co_nrefs > 0) : */
/* 		 entry == NULL); */
/* } */

static int dir_readdir(struct m0_conf_obj *dir, struct m0_conf_obj **pptr)
{
	/*
	 * struct m0_conf_obj *next;
	 * int                 ret;
	 * struct m0_conf_obj *prev = *pptr;
	 *
	 * M0_PRE(readdir_pre(dir, prev));
	 *
	 * if (prev == NULL) {
	 *     next = m0_tlist_head();
	 * } else {
	 *     next = m0_tlist_next(..., prev);
	 *     m0_conf_obj_put(prev);
	 *     *pptr = NULL;
	 * }
	 *
	 * if (next == NULL) {
	 *     ret = M0_CONF_DIREND;
	 * } else if (next->co_status != M0_CS_READY) {
	 *     ret = M0_CONF_DIRMISS;
	 * } else {
	 *     m0_conf_obj_get(next);
	 *     *pptr = next;
	 *     ret = M0_CONF_DIRNEXT;
	 * }
	 *
	 * M0_POST(readdir_post(ret, dir, *pptr));
	 * return ret;
	 */

	M0_IMPOSSIBLE("XXX not implemented");
	return M0_CONF_DIRMISS;
}

static int dir_lookup(struct m0_conf_obj *parent, const struct m0_buf *name,
		      struct m0_conf_obj **out)
{
	struct m0_conf_obj *item;
	struct m0_conf_dir *x = M0_CONF_CAST(parent, m0_conf_dir);

	M0_PRE(parent->co_status == M0_CS_READY);

	m0_tl_for(m0_conf_dir, &x->cd_items, item) {
		if (m0_buf_eq(&item->co_id, name)) {
			*out = item;
			return 0;
		}
	} m0_tl_endfor;

	return -ENOENT;
}

static void dir_delete(struct m0_conf_obj *obj)
{
	struct m0_conf_obj *item;
	struct m0_conf_dir *x = M0_CONF_CAST(obj, m0_conf_dir);

	m0_tl_for(m0_conf_dir, &x->cd_items, item) {
		m0_conf_dir_tlist_del(item);
		/* `item' is deleted by m0_conf_reg_fini(). */
	} m0_tl_endfor;
	m0_conf_dir_tlist_fini(&x->cd_items);
	m0_conf_dir_bob_fini(x);
	m0_free(x);
}

static const struct m0_conf_obj_ops dir_ops = {
	.coo_invariant = dir_invariant,
	.coo_fill      = dir_fill,
	.coo_match     = dir_match,
	.coo_lookup    = dir_lookup,
	.coo_readdir   = dir_readdir,
	.coo_delete    = dir_delete
};

M0_INTERNAL struct m0_conf_obj *m0_conf__dir_create(void)
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
