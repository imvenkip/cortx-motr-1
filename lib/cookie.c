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
 * Original creation date: 12/07/2012
 */

/**
  @addtogroup cookie
  @{
 * The key data-structure in Lib-Cookie is c2_cookie. It holds the address of
 * an object along with a generation-count which is used to check validity of a
 * cookie.
 *
 * The constructor of an object calls c2_cookie_new, which increments a
 * global counter cookie_generation, and embeds it in the object.
 * On arrival of a query for the object, c2_cookie_init creates
 * a cookie, and embeds the address for the object in c2_cookie along with a
 * copy of cookie_generation embedded in the object.
 *
 * For subsequent requests for the same object, client communicates a cookie
 * to a server. On server, function c2_cookie_dereference validates a cookie,
 * and retrieves an address of the object for a valid cookie.
 *
 * c2_cookie_dereference checks the validity of a cookie in two steps.
 * The first step validates an address embedded inside the cookie.
 * The second step ensures that the cookie is not stale. To identify a stale
 * cookie, it compares its generation count with the generation count in the
 * object. In order to reduce the probability of false validation,
 * the function c2_cookie_global_init initializes the cookie_generation with
 * the system-time during initialisation of Colibri.
 */

#include "lib/types.h"
#include "lib/errno.h" /* -EPROTO */
#include "lib/cookie.h"
#include "lib/arith.h" /* C2_IS_8ALIGNED */
#include "lib/time.h"  /* c2_time_now() */

static uint64_t cookie_generation;

extern bool c2_arch_addr_is_sane(const void *addr);
extern int c2_arch_cookie_global_init(void);
extern void c2_arch_cookie_global_fini(void);

int c2_cookie_global_init(void)
{
	cookie_generation = c2_time_now();
	return c2_arch_cookie_global_init();
}

void c2_cookie_new(uint64_t *gen)
{
	C2_PRE(gen != NULL);

	*gen = ++cookie_generation;
}

void c2_cookie_init(struct c2_cookie *cookie, const uint64_t *obj)
{
	C2_PRE(cookie != NULL);
	C2_PRE(obj != NULL);

	cookie->co_addr = (uint64_t)obj;
	cookie->co_generation = *obj;
}

bool c2_addr_is_sane(const uint64_t *addr)
{
	return (addr > (uint64_t *)4096) && C2_IS_8ALIGNED(addr) &&
		c2_arch_addr_is_sane(addr);
}

int c2_cookie_dereference(const struct c2_cookie *cookie, uint64_t **addr)
{
	uint64_t *obj;

	C2_PRE(cookie != NULL);
	C2_PRE(addr != NULL);

	obj = (uint64_t *)cookie->co_addr;
	if (c2_addr_is_sane(obj) && cookie->co_generation == *obj) {
		*addr = obj;
		return 0;
	} else
		return -EPROTO;
}

void c2_cookie_global_fini(void)
{
	c2_arch_cookie_global_fini();
}

/** @} end of cookie group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
