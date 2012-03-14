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
 * Original author: Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 02/10/2012
 */

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>

#include "colibri/init.h"
#include "lib/assert.h"
#include "lib/memory.h"

#define C2_LNET_DRV_TEST
#include "net/lnet/lnet_core.h"
#include "net/lnet/lnet_ioctl.h"

const char lnet_xprt_dev[] = "/dev/" C2_LNET_DEV;

int main(int argc, char *argv[])
{
	int f;
	int rc;
	unsigned int val;
	struct nlx_core_transfer_mc *tm;
	struct prototype_mem_area ma = {
		.nm_size = sizeof *tm,
	};

	rc = c2_init();
	C2_ASSERT(rc == 0);

	C2_ALLOC_PTR(tm);
	C2_ASSERT(tm != NULL);
	tm->ctm_magic = C2_NET_LNET_CORE_TM_MAGIC;
	tm->ctm_user_space_xo = true;
	tm->_debug_ = 15;
	ma.nm_user_addr = (unsigned long) tm;

	f = open(lnet_xprt_dev, O_RDWR|O_CLOEXEC);
	if (f < 0) {
		perror(lnet_xprt_dev);
		return 1;
	}

	rc = ioctl(f, PROTOMAP, &ma);
	C2_ASSERT(rc == 0);
	val = 0;
	rc = ioctl(f, PROTOREAD, &val);
	C2_ASSERT(rc == 0);
	printf("initial value is %d\n", val);
	printf("initial _debug_ is %d\n", tm->_debug_);
	val++;
	rc = ioctl(f, PROTOWRITE, &val);
	C2_ASSERT(rc == 0);
	val = 0;
	printf("final _debug_ is %d\n", tm->_debug_);
	rc = ioctl(f, PROTOREAD, &val);
	C2_ASSERT(rc == 0);
	printf("final value is %d\n", val);
	rc = ioctl(f, PROTOUNMAP, &ma);
	C2_ASSERT(rc == 0);

	c2_fini();
	return 0;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
