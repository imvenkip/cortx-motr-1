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
 * Original author: Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>
 * Original creation date: 4-Jul-2012
 */

#include "fop/fom_long_lock_bob.h"
#include "lib/bob.h"

enum {
        /** Value of c2_long_lock_link::lll_magix */
        C2_LONG_LOCK_LINK_MAGIX = 0xB055B1E55EDF01D5,
        /** Value of c2_longlock::l_magix */
        C2_LONG_LOCK_MAGIX = 0x0B055B1E55EDBA55
};

/**
 * Descriptor of typed list used in c2_long_lock with
 * c2_long_lock_link::lll_lock_linkage.
 */
C2_TL_DESCR_DEFINE(c2_lll, "list of lock-links in longlock", ,
                   struct c2_long_lock_link, lll_lock_linkage, lll_magix,
                   C2_LONG_LOCK_LINK_MAGIX, C2_LONG_LOCK_LINK_MAGIX);

C2_TL_DEFINE(c2_lll, , struct c2_long_lock_link);

static struct c2_bob_type long_lock_bob;
C2_BOB_DEFINE(, &long_lock_bob, c2_long_lock);

static struct c2_bob_type long_lock_link_bob;
C2_BOB_DEFINE(, &long_lock_link_bob, c2_long_lock_link);

void c2_fom_ll_global_init(void)
{
	long_lock_bob.bt_name         = "LONG_LOCK_BOB";
	long_lock_bob.bt_magix        = C2_LONG_LOCK_MAGIX;
	long_lock_bob.bt_magix_offset = offsetof(struct c2_long_lock, l_magix);

	c2_bob_type_tlist_init(&long_lock_link_bob, &c2_lll_tl);
}
C2_EXPORTED(c2_fom_ll_global_init);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
