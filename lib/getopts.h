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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 05/19/2012
 */

#ifndef __COLIBRI_LIB_GETOPTS_H__
#define __COLIBRI_LIB_GETOPTS_H__

#include "lib/types.h"

#ifndef __KERNEL__
#include "lib/user_space/getopts.h"
#endif

/**
   @addtogroup getopts
   @{
 */

/**
   Convert numerical argument, followed by a multiplier suffix, to an
   uint64_t value.  The numerical argument is expected in the format that
   strtoull(..., 0) can parse. The multiplier suffix should be a char
   from "bkmgBKMG" string. The char matches factor which will be
   multiplied by numerical part of argument.

   Suffix char matches:
   - @b b = 512
   - @b k = 1024
   - @b m = 1024 * 1024
   - @b g = 1024 * 1024 * 1024
   - @b B = 500
   - @b K = 1000
   - @b M = 1000 * 1000
   - @b G = 1000 * 1000 * 1000
 */
int c2_get_bcount(const char *arg, c2_bcount_t *out);

/** @} end of getopts group */

/* __COLIBRI_LIB_GETOPTS_H__ */
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
