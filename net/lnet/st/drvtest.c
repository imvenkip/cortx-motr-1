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
#include "lib/misc.h" /* C2_SET0 */

#define C2_LNET_DRV_TEST
#include "net/lnet/lnet_core.h"
#include "net/lnet/lnet_ioctl.h"

const char lnet_xprt_dev[] = "/dev/" C2_LNET_DEV;

/** @todo hack */
void *nlx_core_mem_alloc(size_t size, unsigned shift)
{
	return c2_alloc_aligned(size, shift);
}

/** @todo hack */
void nlx_core_mem_free(void *data, unsigned shift)
{
	c2_free_aligned(data, 1<<shift, shift);
}

/** @todo hack */
void nlx_core_dom_set_debug(struct nlx_core_domain *lcdom, unsigned dbg)
{
	lcdom->_debug_ = dbg;
}

/** @todo hack */
void nlx_core_tm_set_debug(struct nlx_core_transfer_mc *lctm, unsigned dbg)
{
	lctm->_debug_ = dbg;
}

int main(int argc, char *argv[])
{
	int f;
	int rc;
	struct nlx_core_domain *dom;
	struct nlx_core_transfer_mc *tm;
	struct c2_lnet_dev_dom_init_params p;

	rc = c2_init();
	C2_ASSERT(rc == 0);

	C2_SET0(&p);
	NLX_ALLOC_PTR(dom);
	C2_ASSERT(dom != NULL);
	NLX_ALLOC_PTR(tm);
	C2_ASSERT(tm != NULL);
	tm->ctm_magic = C2_NET_LNET_CORE_TM_MAGIC;
	tm->ctm_user_space_xo = true;
	tm->_debug_ = 15;
	nlx_core_tm_set_debug(tm, 0);
	p.ddi_cd = dom;
	nlx_core_dom_set_debug(dom, 0);

	f = open(lnet_xprt_dev, O_RDWR|O_CLOEXEC);
	if (f < 0) {
		perror(lnet_xprt_dev);
		return 1;
	}

	rc = ioctl(f, C2_LNET_DOM_INIT, &p);
	C2_ASSERT(rc == 0);
	printf("max values are: bufsize=%ld segsize=%ld segs=%d\n",
	       p.ddi_max_buffer_size,
	       p.ddi_max_buffer_segment_size, p.ddi_max_buffer_segments);

	close(f);
	NLX_FREE_PTR(tm);
	NLX_FREE_PTR(dom);

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
