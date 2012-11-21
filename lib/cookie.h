/* -*- C -*- */
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
 * Original author: Nachiket Sahasrabudhe <Nachiket_Sahasrabudhe@xyratex.com>
 * Original creation date: 10/07/2012
 */

#pragma once

#ifndef __COLIBRI_LIB_COOKIE_H__
#define __COLIBRI_LIB_COOKIE_H__

#include "lib/types.h"
#include "xcode/xcode.h"

/**
 * @defgroup cookie Cookie
 *
 * In a network-file-system, when a client queries for an in-memory object to
 * a server, server searches through a set of data-structures to retrieve the
 * object. Multiple queries asking for the same object lead to a repeated
 * search.
 *
 * Cookie mechanism avoids such redundant search operations. When the first
 * query for an object arrives, server searches the object, embeds it's address
 * in a cookie, and then sends this cookie to a client. Client then uses this
 * cookie in subsequent queries for the same object.
 *
 * As client is unaware of memory-updates at server-end, its necessary for
 * server to verify that received cookie is not a stale one. Server achieves
 * this by maintaining a global counter called generation-count. It embeds
 * a same value of generation-count in an object and a cookie associated with
 * it. On reception of a cookie, before returning a required object,
 * server ensures that value of generation-count in the cookie matches
 * with the one in the object.
 * @{
 */

/**
 * Holds an address of a remote object and its generation count.
 */
struct c2_cookie {
	uint64_t co_addr;
	uint64_t co_generation;
} C2_XCA_RECORD;

/**
 * Initializes the gencount. Gets called during colibri initialization.
 */
C2_INTERNAL int c2_cookie_global_init(void);

C2_INTERNAL void c2_cookie_global_fini(void);

/**
 * Increments generation-count by one and assigns the same to *gen.
 */
C2_INTERNAL void c2_cookie_new(uint64_t * gen);

/**
 * Embeds address of an object along with a generation-count in a cookie.
 *
 * @param obj (in)	 address of an object
 * @param cookie (out)   address of a cookie in which obj gets embedded
 */
C2_INTERNAL void c2_cookie_init(struct c2_cookie *cookie, uint64_t * obj);

/**
 * Retrieves address of an object from a cookie.
 *
 * @param cookie (in)   address of a cookie that holds the address of an object
 * @param addr (out)    pointer to a memory location which holds retrieved
 *                      address
 */
C2_INTERNAL int c2_cookie_dereference(const struct c2_cookie *cookie,
				      uint64_t ** addr);

/**
 * Checks if address is aligned to 8-byte address and is pointing to a valid
 * memory location.
 */
C2_INTERNAL bool c2_addr_is_sane(const uint64_t * addr);

/**
 * A macro to retrive address of a parent structure, associated with an object
 * embedded in a cookie.
 */
#define c2_cookie_of(cookie, type, field)		      \
({							      \
	uint64_t	 *__gen;			      \
	struct c2_cookie *__cookie = (cookie);		      \
	c2_cookie_dereference(__cookie, &__gen) != 0 ? NULL : \
			container_of(__gen, type, field);     \
})

/** @} end of cookie group */
/*__C2_LIB_COOKIE_H__*/
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
