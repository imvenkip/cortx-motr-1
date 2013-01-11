/* -*- C -*- */
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
 * Original author: Manish Honap <manish_honap@xyratex.com>
 * Original creation date: 9-Jan-2013
 */


#pragma once

#ifndef __MERO_LIB_LOCKERS_H__
#define __MERO_LIB_LOCKERS_H__

#include "lib/types.h"
#include "lib/cdefs.h"

/**
 * @defgroup lockers
 *
 * @{
 */

struct m0_lockers_type {
	uint32_t lot_max;
	uint32_t lot_index;
};

struct m0_lockers {
	void *loc_slots[0];
};

#define M0_LOCKER_DECLARE(scope, name, max)                               \
scope struct name ## _lockers_type;                                       \
                                                                          \
struct name ## _lockers {                                                 \
        struct m0_lockers __base;                                         \
        void             *__slots[(max)];                                 \
};                                                                        \
                                                                          \
M0_BASSERT(offsetof(struct name ## _lockers, __slots[0]) ==               \
           offsetof(struct m0_lockers, loc_slots[0]));                    \
                                                                          \
scope void name ## _lockers_init(struct name *par);                       \
scope void name ## _lockers_fini(struct name *par);                       \
scope int name ## _lockers_allot(void);                                   \
scope void name ## _lockers_set(struct name *par, int key, void *data);   \
scope void * name ## _lockers_get(struct name *par, int key);             \
scope void name ## _lockers_clear(struct name *par, int key);             \
scope bool name ## _lockers_is_empty(struct name *par, int key)

M0_INTERNAL void m0_lockers_init(const struct m0_lockers_type *lt,
				 struct m0_lockers            *lockers);


M0_INTERNAL void m0_lockers_fini(struct m0_lockers_type *lt,
				 struct m0_lockers      *lockers);

M0_INTERNAL int m0_lockers_allot(struct m0_lockers_type *lt);

/**
 * Stores a value in locker
 *
 * @pre m0_locker_is_empty(locker, key)
 * @post !m0_locker_is_empty(locker, key) &&
 *        m0_locker_retrieve(locker, key) == data
 */
M0_INTERNAL void m0_lockers_set(const struct m0_lockers_type *lt,
				struct m0_lockers            *lockers,
				uint32_t                      key,
				void                         *data);
/**
 * Retrieves a value stored in locker
 *
 * @pre !m0_locker_is_empty(locker, key)
 */
M0_INTERNAL void *m0_lockers_get(const struct m0_lockers_type *lt,
				 const struct m0_lockers      *lockers,
				 uint32_t                      key);

/**
 * Clears the value stored in a locker
 *
 * @pre !m0_locker_is_empty(locker, key)
 * @post m0_locker_is_empty(locker, key)
 */
M0_INTERNAL void m0_lockers_clear(const struct m0_lockers_type *lt,
				  struct m0_lockers            *lockers,
				  uint32_t                      key);

M0_INTERNAL bool m0_lockers_is_empty(const struct m0_lockers_type *lt,
				     const struct m0_lockers      *lockers,
				     uint32_t                      key);

#define M0_LOCKER_DEFINE(scope, name, field)                                   \
scope struct m0_lockers_type name ## _lockers_type = {                         \
        .lot_max = ARRAY_SIZE(M0_FIELD_VALUE(struct name ## _lockers,          \
                                             __slots)),                        \
        .lot_index = 0                                                         \
};                                                                             \
                                                                               \
scope void name ## _lockers_init(struct name *par)                             \
{                                                                              \
        m0_lockers_init(&(name ## _lockers_type), &par->field.__base);         \
}                                                                              \
                                                                               \
scope void name ## _lockers_fini(struct name *par)                             \
{                                                                              \
        m0_lockers_fini(&(name ## _lockers_type), &par->field.__base);         \
}                                                                              \
                                                                               \
scope int name ## _lockers_allot(void)                                         \
{                                                                              \
        return m0_lockers_allot(&(name ## _lockers_type));                     \
}                                                                              \
                                                                               \
scope void name ## _lockers_set(struct name *par, int key, void *data)         \
{                                                                              \
        m0_lockers_set(&(name ## _lockers_type),                               \
		       &par->field.__base, key, data);                         \
}                                                                              \
                                                                               \
scope void * name ## _lockers_get(struct name *par, int key)                   \
{                                                                              \
        return m0_lockers_get(&(name ## _lockers_type),                        \
                              &par->field.__base, key);                        \
}                                                                              \
                                                                               \
scope void name ## _lockers_clear(struct name *par, int key)                   \
{                                                                              \
        m0_lockers_clear(&(name ## _lockers_type), &par->field.__base, key);   \
}                                                                              \
                                                                               \
scope bool name ## _lockers_is_empty(struct name *par, int key)                \
{                                                                              \
        return m0_lockers_is_empty(&(name ## _lockers_type),                   \
                                   &par->field.__base, key);                   \
}                                                                              \
                                                                               \
struct __ ## type ## _semicolon_catcher

/** @} end of lockers group */

#endif /* __MERO_LIB_LOCKERS_H__ */


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
