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

M0_INTERNAL void m0_bob_type_tlist_init(struct m0_bob_type *bt,
					const struct m0_tl_descr *td)
{
	M0_PRE(td->td_link_magic != 0);

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

M0_INTERNAL void m0_bob_init(const struct m0_bob_type *bt, void *bob)
{
	*MAGIX(bt, bob) = bt->bt_magix;
}

M0_INTERNAL void m0_bob_fini(const struct m0_bob_type *bt, void *bob)
{
	M0_ASSERT(m0_bob_check(bt, bob));
	*MAGIX(bt, bob) = 0;
}

M0_INTERNAL bool m0_bob_check(const struct m0_bob_type *bt, const void *bob)
{
	return
		(unsigned long)bob + 4096 > 8192 &&
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
