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
#include "colibri/magic.h" /* C2_CONF_OBJ_MAGIC, C2_CONF_DIR_MAGIC */

C2_TL_DESCR_DEFINE(c2_conf_dir, "c2_conf_dir::cd_items", C2_INTERNAL,
		   struct c2_conf_obj, co_dir_link, co_gen_magic,
		   C2_CONF_OBJ_MAGIC, C2_CONF_DIR_MAGIC);
C2_TL_DEFINE(c2_conf_dir, C2_INTERNAL, struct c2_conf_obj);

static bool dir_check(const void *bob)
{
	const struct c2_conf_dir *self = bob;
	const struct c2_conf_obj *self_obj = &self->cd_obj;
	const struct c2_conf_obj *parent = self_obj->co_parent;

	C2_PRE(self_obj->co_type == C2_CO_DIR);

	return ergo(self_obj->co_status == C2_CS_READY,
		    C2_IN(self->cd_item_type, (C2_CO_SERVICE, C2_CO_NIC,
					       C2_CO_SDEV, C2_CO_PARTITION))) &&
		ergo(self_obj->co_mounted, /* check relations */
		     parent->co_mounted && parent->co_status == C2_CS_READY &&
		     C2_IN(parent->co_type,
			   (C2_CO_FILESYSTEM, C2_CO_NODE, C2_CO_SDEV)) &&
		     ergo(self_obj->co_status == C2_CS_READY,
			  c2_tl_forall(c2_conf_dir, child, &self->cd_items,
				       child_check(self_obj, child,
						   self->cd_item_type))));
}

C2_CONF__BOB_DEFINE(c2_conf_dir, C2_CONF_DIR_MAGIC, dir_check);

C2_CONF__INVARIANT_DEFINE(dir_invariant, c2_conf_dir);

static int dir_fill(struct c2_conf_obj *dest __attribute__((unused)),
		    const struct confx_object *src __attribute__((unused)),
		    struct c2_conf_reg *reg __attribute__((unused)))
{
	C2_IMPOSSIBLE("c2_conf_dir should not be filled explicitly");
	return -1;
}

static bool dir_match(const struct c2_conf_obj *cached __attribute__((unused)),
		      const struct confx_object *flat __attribute__((unused)))
{
	C2_IMPOSSIBLE("c2_conf_dir should not be compared with confx_object");
	return false;
}

/* static bool */
/* belongs(const struct c2_conf_obj *entry, const struct c2_conf_obj *dir) */
/* { */
/* 	const struct c2_conf_dir *d = bob_of(dir, const struct c2_conf_dir, */
/* 					     c2_conf_dir_cast_field, */
/* 					     &c2_conf_dir_bob); */
/* 	return d->cd_item_type == entry->co_type && entry->co_parent == dir; */
/* } */

/* /\** */
/*  * Precondition for c2_conf_obj_ops::coo_readdir(). */
/*  * */
/*  * @param dir     The 1st argument of ->coo_readdir(). */
/*  * @param entry   The 2nd argument of ->coo_readdir(), dereferenced */
/*  *                before the function is called (*pptr). */
/*  * */
/*  * @see c2_conf_obj_ops::coo_readdir() */
/*  *\/ */
/* static bool */
/* readdir_pre(const struct c2_conf_obj *dir, const struct c2_conf_obj *entry) */
/* { */
/* 	return obj_invariant(dir) && obj_invariant(entry) && */
/* 		dir->co_type == C2_CO_DIR && dir->co_nrefs > 0 && */
/* 		ergo(entry != NULL, belongs(entry, dir) && entry->co_nrefs > 0); */
/* } */

/* /\** */
/*  * Postcondition for c2_conf_obj_ops::coo_readdir(). */
/*  * */
/*  * @param retval  The value returned by ->coo_readdir(). */
/*  * @param dir     The 1st argument of ->coo_readdir(). */
/*  * @param entry   The 2nd argument of ->coo_readdir(), dereferenced */
/*  *                after the function is called (*pptr). */
/*  * */
/*  * @see c2_conf_obj_ops::coo_readdir() */
/*  *\/ */
/* static bool readdir_post(int retval, const struct c2_conf_obj *dir, */
/* 			 const struct c2_conf_obj *entry) */
/* { */
/* 	return obj_invariant(dir) && obj_invariant(entry) && */
/* 		C2_IN(retval, */
/* 		     (C2_CONF_DIREND, C2_CONF_DIRNEXT, C2_CONF_DIRMISS)) && */
/* 		(retval == C2_CONF_DIRNEXT ? */
/* 		 (entry != NULL && belongs(entry, dir) && entry->co_nrefs > 0) : */
/* 		 entry == NULL); */
/* } */

static int dir_readdir(struct c2_conf_obj *dir, struct c2_conf_obj **pptr)
{
	/*
	 * struct c2_conf_obj *next;
	 * int                 ret;
	 * struct c2_conf_obj *prev = *pptr;
	 *
	 * C2_PRE(readdir_pre(dir, prev));
	 *
	 * if (prev == NULL) {
	 *     next = c2_tlist_head();
	 * } else {
	 *     next = c2_tlist_next(..., prev);
	 *     c2_conf_obj_put(prev);
	 *     *pptr = NULL;
	 * }
	 *
	 * if (next == NULL) {
	 *     ret = C2_CONF_DIREND;
	 * } else if (next->co_status != C2_CS_READY) {
	 *     ret = C2_CONF_DIRMISS;
	 * } else {
	 *     c2_conf_obj_get(next);
	 *     *pptr = next;
	 *     ret = C2_CONF_DIRNEXT;
	 * }
	 *
	 * C2_POST(readdir_post(ret, dir, *pptr));
	 * return ret;
	 */

	C2_IMPOSSIBLE("XXX not implemented");
	return C2_CONF_DIRMISS;
}

static int dir_lookup(struct c2_conf_obj *parent, const struct c2_buf *name,
		      struct c2_conf_obj **out)
{
	struct c2_conf_obj *item;
	struct c2_conf_dir *x = C2_CONF_CAST(parent, c2_conf_dir);

	C2_PRE(parent->co_status == C2_CS_READY);

	c2_tl_for(c2_conf_dir, &x->cd_items, item) {
		if (c2_buf_eq(&item->co_id, name)) {
			*out = item;
			return 0;
		}
	} c2_tl_endfor;

	return -ENOENT;
}

static void dir_delete(struct c2_conf_obj *obj)
{
	struct c2_conf_obj *item;
	struct c2_conf_dir *x = C2_CONF_CAST(obj, c2_conf_dir);

	c2_tl_for(c2_conf_dir, &x->cd_items, item) {
		c2_conf_dir_tlist_del(item);
		/* `item' is deleted by c2_conf_reg_fini(). */
	} c2_tl_endfor;
	c2_conf_dir_tlist_fini(&x->cd_items);
	c2_conf_dir_bob_fini(x);
	c2_free(x);
}

static const struct c2_conf_obj_ops dir_ops = {
	.coo_invariant = dir_invariant,
	.coo_fill      = dir_fill,
	.coo_match     = dir_match,
	.coo_lookup    = dir_lookup,
	.coo_readdir   = dir_readdir,
	.coo_delete    = dir_delete
};

C2_INTERNAL struct c2_conf_obj *c2_conf__dir_create(void)
{
	struct c2_conf_dir *x;
	struct c2_conf_obj *ret;

	C2_ALLOC_PTR(x);
	if (x == NULL)
		return NULL;
	c2_conf_dir_bob_init(x);

	/* Initialise concrete fields. */
	c2_conf_dir_tlist_init(&x->cd_items);

	ret = &x->cd_obj;
	ret->co_ops = &dir_ops;
	return ret;
}
