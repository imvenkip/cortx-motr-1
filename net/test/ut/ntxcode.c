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
 * Original author: Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 06/28/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/** @todo remove */
#ifndef __KERNEL__
#include <stdio.h>		/* printf */
#endif

#include "lib/ut.h"		/* C2_UT_ASSERT */
#include "lib/semaphore.h"	/* c2_semaphore */
#include "lib/memory.h"		/* c2_alloc */
#include "net/lnet/lnet.h"	/* c2_net_lnet_ifaces_get */

#include "net/test/commands.h"

#ifndef __KERNEL__
#define LOGD(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOGD(format, ...) do {} while (0)
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
