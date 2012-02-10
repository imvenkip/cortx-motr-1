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
#include "net/lnet/lnet_ioctl.h"

const char lnet_xprt_dev[] = "/dev/c2_lnet";

int main(int argc, char *argv[])
{
	int f;
	int rc;
	unsigned int val;

	rc = c2_init();
	C2_ASSERT(rc == 0);

	f = open(lnet_xprt_dev, O_RDWR);
	if (f < 0) {
		perror(lnet_xprt_dev);
		return 1;
	}

	val = 0;
	rc = ioctl(f, C2_LNET_PROTOREAD, &val);
	C2_ASSERT(rc == 0);
	printf("initial value is %d\n", val);
	val++;
	rc = ioctl(f, C2_LNET_PROTOWRITE, &val);
	C2_ASSERT(rc == 0);
	val = 0;
	rc = ioctl(f, C2_LNET_PROTOREAD, &val);
	C2_ASSERT(rc == 0);
	printf("final value is %d\n", val);

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
