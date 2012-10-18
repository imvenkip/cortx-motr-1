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
 * Original creation date: 04-Mar-2012
 */
#pragma once
#ifndef __COLIBRI_CONF_REG_H__
#define __COLIBRI_CONF_REG_H__

#include "conf/obj.h"  /* c2_conf_objtype */
#include "lib/tlist.h" /* c2_tl, C2_TL_DESCR_DECLARE */

/**
 * @page conf-fspec-reg Registry of Cached Configuration Objects
 *
 * A registry of cached configuration objects, represented by
 * c2_conf_reg structure, serves two goals:
 *
 * 1) It ensures uniqueness of configuration objects in the cache.
 *    After an object is added to the registry, any attempt to add
 *    another one with similar identity will fail.
 *
 * 2) It simplifies erasing of configuration cache.  Register's
 *    destructor frees all the registered configuration objects
 *    without any sophisticated DAG traversal.
 *
 * @section conf-fspec-reg-data Data structures
 *
 * - c2_conf_reg --- a registry of cached configuration objects.
 *
 * @section conf-fspec-reg-sub Subroutines
 *
 * - c2_conf_reg_init() initialises a registry.
 * - c2_conf_reg_fini() finalises a registry.
 *
 * - c2_conf_reg_add() registers configuration object.
 * - c2_conf_reg_lookup() returns the address of registered
 *   configuration object given its identity.
 * - c2_conf_reg_del() unregisters configuration object.
 *
 * @section conf-fspec-reg-thread Concurrency control
 *
 * A user must guarantee that c2_conf_reg_*() calls performed against
 * the same registry are not concurrent.
 *
 * @see @ref conf_dfspec_reg "Detailed Functional Specification"
 */

/**
 * @defgroup conf_dfspec_reg Registry of Cached Configuration Objects
 * @brief Detailed Functional Specification.
 *
 * @see @ref conf, @ref conf-fspec-reg "Functional Specification"
 *
 * @{
 */

/** Registry of cached configuration objects. */
struct c2_conf_reg {
	/** List of c2_conf_obj-s, linked through c2_conf_obj::co_reg_link. */
	struct c2_tl r_objs;
	/** Magic value. */
	uint64_t     r_magic;
};

C2_TL_DESCR_DECLARE(c2_conf_reg, extern);
C2_TL_DECLARE(c2_conf_reg, , struct c2_conf_obj);

/** Initialises a registry. */
void c2_conf_reg_init(struct c2_conf_reg *reg);

/**
 * Finalises a registry.
 *
 * This function c2_conf_obj_delete()s every registered configuration
 * object.
 */
void c2_conf_reg_fini(struct c2_conf_reg *reg);

/**
 * Registers configuration object.
 *
 * @pre  !c2_conf_reg_tlink_is_in(obj)
 */
int c2_conf_reg_add(struct c2_conf_reg *reg, struct c2_conf_obj *obj);

/**
 * Un-registers configuration object.
 *
 * @pre  c2_conf_reg_tlist_contains(&reg->r_objs, obj)
 */
void c2_conf_reg_del(const struct c2_conf_reg *reg, struct c2_conf_obj *obj);

/**
 * Searches for a configuration object given its identity (type & id).
 *
 * Returns NULL if there is no such object in the registry.
 */
struct c2_conf_obj *c2_conf_reg_lookup(const struct c2_conf_reg *reg,
				       enum c2_conf_objtype type,
				       const struct c2_buf *id);

/** @} conf_dfspec_reg */
#endif /* __COLIBRI_CONF_REG_H__ */
