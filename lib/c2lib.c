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
 * Original author: Yuriy Umanets <Yuriy_Umanets@xyratex.com>
 * Original creation date: 01/23/2010
 */

#include "lib/cdefs.h"
#include "lib/buf_xc.h"
#include "lib/vec_xc.h"

static void xc_types_init(void)
{
	c2_xc_buf_init();
	c2_xc_vec_init();
}

static void xc_types_fini(void)
{
	c2_xc_buf_fini();
	c2_xc_vec_fini();
}

int libc2_init(void)
{
	xc_types_init();

	return 0;
}
C2_EXPORTED(libc2_init);

void libc2_fini(void)
{
	xc_types_fini();
}
C2_EXPORTED(libc2_fini);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
