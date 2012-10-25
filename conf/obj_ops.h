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
 * Original creation date: 09-Mar-2012
 */
#pragma once
#ifndef __COLIBRI_CONF_OBJOPS_H__
#define __COLIBRI_CONF_OBJOPS_H__

#include "conf/obj.h"  /* c2_conf_objtype */
#include "lib/tlist.h" /* C2_TL_DESCR_DECLARE, C2_TL_DECLARE */

/**
 * @page conf-fspec-objops Configuration Object Operations
 *
 * Data structures and functions declared in "conf/obj_ops.h" are used
 * by confc and confd implementations. "conf/obj_ops.h" is not
 * supposed to be #included by the application (aka configuration
 * consumer).
 *
 * @section conf-fspec-objops-data Data Structures
 *
 * - c2_conf_obj_ops --- configuration object operations vector.
 *
 * @section conf-fspec-objops-sub Subroutines
 *
 * - c2_conf_obj_create() allocates and initialises configuration object.
 * - c2_conf_obj_find() finds registered object or creates and a stub.
 * - c2_conf_obj_delete() finalises and frees configuration object.
 * - c2_conf_obj_invariant() validates given configuration object.
 * - c2_conf_obj_get() increases and c2_conf_obj_put() decreases
 *   object's number of references.
 * - c2_conf_obj_fill() enriches a stub with configuration data.
 * - c2_conf_obj_match() compares cached configuration object with its
 *   on-wire representation.
 *
 * @see @ref conf_dfspec_objops "Detailed Functional Specification"
 */

/**
 * @defgroup conf_dfspec_objops Configuration Object Operations
 * @brief Detailed Functional Specification.
 *
 * @see @ref conf, @ref conf-fspec-objops "Functional Specification"
 *
 * @{
 */

/* export */
C2_TL_DESCR_DECLARE(c2_conf_reg, extern);
C2_TL_DECLARE(c2_conf_reg, , struct c2_conf_obj);

/* import */
struct c2_conf_reg;
struct confx_object;

C2_TL_DESCR_DECLARE(c2_conf_dir, extern);
C2_TL_DECLARE(c2_conf_dir, , struct c2_conf_obj);

/** Symbolic names for c2_conf_obj_ops::coo_readdir() return values. */
enum c2_conf_dirval {
	/** End of directory is reached. */
	C2_CONF_DIREND = 0,
	/** The next directory entry is available immediately. */
	C2_CONF_DIRNEXT,
	/**
	 * The next directory entry is missing from configuration
	 * cache or is a stub.
	 */
	C2_CONF_DIRMISS
};

/** Configuration object operations. */
struct c2_conf_obj_ops {
	/** Validates concrete fields of given configuration object. */
	bool (*coo_invariant)(const struct c2_conf_obj *obj);

	/**
	 * Populates concrete object with configuration data taken
	 * from confx_object.
	 *
	 * Creates stubs of object's neighbours, if necessary.
	 *
	 * @pre   dest->co_nrefs == 0 && dest->co_status != C2_CS_READY
	 * @post  dest->co_status == (retval == 0 ? C2_CS_READY : C2_CS_MISSING)
	 * @post  ergo(retval == 0, dest->co_mounted)
	 */
	int (*coo_fill)(struct c2_conf_obj *dest,
			const struct confx_object *src,
			struct c2_conf_reg *reg);

	/**
	 * Returns false iff cached configuration object and "flat" object
	 * have conflicting data.
	 */
	bool (*coo_match)(const struct c2_conf_obj *cached,
			  const struct confx_object *flat);

	/**
	 * Finds a child of given object.
	 *
	 * @param parent  The object being searched.
	 * @param name    Name of the relation leading to a child object.
	 *                Identifier of the child object, if parent is
	 *                a directory.
	 * @param out     If the function succeeds, *out will point to the
	 *                sought-for object.
	 *
	 * @pre  parent->co_status == C2_CS_READY
	 */
	int (*coo_lookup)(struct c2_conf_obj *parent, const struct c2_buf *name,
			  struct c2_conf_obj **out);

	/**
	 * Gets next directory entry.
	 *
	 * @param      dir   Directory.
	 * @param[in]  pptr  "Current" entry.
	 * @param[out] pptr  "Next" entry.
	 *
	 * @retval C2_CONF_DIRMISS  The next directory entry is missing from
	 *                          configuration cache or is a stub.
	 * @retval C2_CONF_DIRNEXT  *pptr now points to the next entry.
	 * @retval C2_CONF_DIREND   End of directory is reached.
	 * @retval -Exxx            Error.
	 *
	 * ->coo_readdir() puts (c2_conf_obj_put()) configuration object
	 * referred to via `pptr' input parameter.
	 *
	 * ->coo_readdir() pins (c2_conf_obj_get()) the resulting
	 * object in case of C2_CONF_DIRNEXT.
	 */
	int (*coo_readdir)(struct c2_conf_obj *dir, struct c2_conf_obj **pptr);

	/**
	 * Finalises concrete fields of given configuration object and
	 * frees it.
	 *
	 * @see c2_conf_obj_delete()
	 */
	void (*coo_delete)(struct c2_conf_obj *obj);
};

/**
 * Allocates and initialises configuration object of given type.
 *
 * Copies `id' into ->co_id of the resulting object.
 *
 * Note, that c2_conf_obj_create() does not add the resulting object
 * into configuration cache.
 *
 * @post  ergo(retval != NULL,
 *             !retval->co_mounted && retval->co_status == C2_CS_MISSING)
 */
C2_INTERNAL struct c2_conf_obj *c2_conf_obj_create(enum c2_conf_objtype type,
						   const struct c2_buf *id);

/**
 * Finds registered object with given identity or, if no object is
 * found, creates and registers a stub.
 */
C2_INTERNAL int c2_conf_obj_find(struct c2_conf_reg *reg,
				 enum c2_conf_objtype type,
				 const struct c2_buf *id,
				 struct c2_conf_obj **out);

/**
 * Finalises and frees configuration object.
 *
 * @pre  obj->co_nrefs == 0 && obj->co_status != C2_CS_LOADING
 */
C2_INTERNAL void c2_conf_obj_delete(struct c2_conf_obj *obj);

/** Validates given configuration object. */
C2_INTERNAL bool c2_conf_obj_invariant(const struct c2_conf_obj *obj);

/**
 * Increments reference counter of given configuration object.
 *
 * @pre   obj->co_status == C2_CS_READY
 * @post  obj->co_nrefs > 0
 */
C2_INTERNAL void c2_conf_obj_get(struct c2_conf_obj *obj);

/**
 * Decrements reference counter of given configuration object.
 *
 * Broadcasts obj->co_chan if the object becomes unpinned (i.e., if
 * the decremented counter reaches 0).
 *
 * @pre  obj->co_nrefs > 0 && obj->co_status == C2_CS_READY
 */
C2_INTERNAL void c2_conf_obj_put(struct c2_conf_obj *obj);

/**
 * Enriches a stub with configuration data.
 *
 * @param dest  A stub to be filled with configuration data.
 * @param src   On-wire object, providing the configuration data.
 * @param reg   Registry of cached configuration objects.
 *
 * Note, that the caller is responsible for passing valid confx_object
 * via `src' parameter.
 *
 * @pre   `src' is valid
 * @pre   c2_mutex_is_locked(&dest->co_confc->cc_lock)
 * @pre   dest->co_nrefs == 0 && dest->co_status != C2_CS_READY
 * @pre   dest->co_type == src->o_conf.u_type
 * @pre   c2_buf_eq(&dest->co_id, &src->o_id)
 *
 * @post  c2_conf_obj_invariant(dest)
 * @post  c2_mutex_is_locked(&dest->co_confc->cc_lock)
 * @post  ergo(retval == 0, dest->co_status == C2_CS_READY && dest->co_mounted)
 */
C2_INTERNAL int c2_conf_obj_fill(struct c2_conf_obj *dest,
				 const struct confx_object *src,
				 struct c2_conf_reg *reg);

/**
 * Returns false iff cached configuration object and on-wire object
 * have conflicting data.
 *
 * Note, that the caller is responsible for passing valid confx_object
 * via `flat' parameter.
 *
 * @pre  cached->co_mounted
 * @pre  `flat' is valid
 */
C2_INTERNAL bool c2_conf_obj_match(const struct c2_conf_obj *cached,
				   const struct confx_object *flat);

/** @} conf_dfspec_objops */
#endif /* __COLIBRI_CONF_OBJOPS_H__ */
