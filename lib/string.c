/* -*- C -*- */
/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Dima Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 21-Aug-2014
 */

#include "lib/types.h"   /* m0_bcount_t */
#include "lib/misc.h"    /* ARRAY_SIZE */
#include "lib/string.h"


const char *m0_bcount_with_suffix(char *buf, size_t size, m0_bcount_t c)
{
	static const char *suffix[] = {
		"", "Ki", "Mi", "Gi", "Ti", "Pi", "Ei", "Zi", "Yi"
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(suffix) - 1 && c >= 1024; ++i, c /= 1024)
		;
	snprintf(buf, size, "%3" PRId64 " %s", c, suffix[i]);
	return buf;
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
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
