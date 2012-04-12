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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 21-Jan-2012
 */

#include "xcode/xcode.h"
#include "lib/tlist.h"
#include "lib/assert.h"
#include "lib/bob.h"

/**
 * @addtogroup bob
 *
 * @{
 */

void c2_bob_type_tlist_init(struct c2_bob_type *bt, const struct c2_tl_descr *td)
{
	C2_PRE(td->td_link_magic != 0);

	bt->bt_name         = td->td_name;
	bt->bt_magix        = td->td_link_magic;
	bt->bt_magix_offset = td->td_link_magic_offset;
}

/**
 * Returns the value of magic field.
 *
 * Macro is used instead of inline function so that constness of the result
 * depends on the constness of "bob" argument.
 */
#define MAGIX(bt, bob) ((uint64_t *)(bob + bt->bt_magix_offset))

void c2_bob_init(const struct c2_bob_type *bt, void *bob)
{
	*MAGIX(bt, bob) = bt->bt_magix;
}

void c2_bob_fini(const struct c2_bob_type *bt, void *bob)
{
	C2_ASSERT(c2_bob_check(bt, bob));
	*MAGIX(bt, bob) = 0;
}

bool c2_bob_check(const struct c2_bob_type *bt, const void *bob)
{
	return
		bob != NULL &&
		*MAGIX(bt, bob) == bt->bt_magix &&
		ergo(bt->bt_check != NULL, bt->bt_check(bob));
}

/** @} end of cond group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
