/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 01/20/2012
 */

#ifndef __COLIBRI_BALLOC_BALLOC_UT_H__
#define __COLIBRI_BALLOC_BALLOC_UT_H__

#include <stdio.h>        /* fprintf */
#include <stdlib.h>       /* srand, rand */
#include <errno.h>
#include <sys/time.h>
#include <err.h>

#include "dtm/dtm.h"      /* c2_dtx */
#include "lib/arith.h"    /* C2_3WAY, c2_uint128 */
#include "lib/misc.h"     /* C2_SET0 */
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/thread.h"
#include "lib/getopts.h"
#include "db/db.h"
#include "lib/ut.h"
#include "balloc/balloc.h"

/* Interfaces for UT */
extern void c2_balloc_debug_dump_sb(const char *tag,
				    struct c2_balloc_super_block *sb);
extern void c2_balloc_debug_dump_group_extent(const char *tag,
					      struct c2_balloc_group_info *grp);

extern int c2_balloc_release_extents(struct c2_balloc_group_info *grp);
extern int c2_balloc_load_extents(struct c2_balloc *cb,
			   struct c2_balloc_group_info *grp,
			   struct c2_db_tx *tx);
extern struct c2_balloc_group_info * c2_balloc_gn2info(struct c2_balloc *cb,
						c2_bindex_t groupno);
extern void c2_balloc_debug_dump_group(const char *tag,
				       struct c2_balloc_group_info *grp);
extern void c2_balloc_lock_group(struct c2_balloc_group_info *grp);
extern int c2_balloc_trylock_group(struct c2_balloc_group_info *grp);
extern void c2_balloc_unlock_group(struct c2_balloc_group_info *grp);

#endif /*__COLIBRI_BALLOC_BALLOC_UT_H__*/

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
