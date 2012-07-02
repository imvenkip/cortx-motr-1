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

enum {
        /** Value of c2_longlock::l_magix */
        C2_LONG_LOCK_MAGIX = 0x0B055B1E55EDBA55
};

static struct c2_bob_type longlock_bob;
C2_BOB_DEFINE(, &longlock_bob, c2_long_lock);

void c2_fom_ll_global_init(void)
{
	longlock_bob.bt_name         = "LONG_LOCK_BOB";
	longlock_bob.bt_magix        = C2_LONG_LOCK_MAGIX;
	longlock_bob.bt_magix_offset = offsetof(struct c2_long_lock, l_magix);
}
