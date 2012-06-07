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

#include "lib/getopts.h"

#ifdef __KERNEL__
#include <linux/kernel.h>	/* simple_strtoull */
#include <linux/string.h>	/* strchr */
#else
#include <stdlib.h>		/* strtoull */
#include <string.h>		/* strchr */
#endif

#include "lib/errno.h"

#ifdef __KERNEL__
#define c2_strtoull	simple_strtoull
#else
#define c2_strtoull	strtoull
#endif

int c2_get_bcount(const char *arg, c2_bcount_t *out)
{
	char		 *end = NULL;
	char		 *pos;
	static const char suffix[] = "bkmgBKMG";
	int		  result = 0;

	static const uint64_t multiplier[] = {
		1 << 9,
		1 << 10,
		1 << 20,
		1 << 30,
		500,
		1000,
		1000 * 1000,
		1000 * 1000 * 1000
	};

	*out = c2_strtoull(arg, &end, 0);

	if (*end != 0) {
		pos = strchr(suffix, *end);
		if (pos != NULL)
			*out *= multiplier[pos - suffix];
		else
			result = -EINVAL;
	}
	return result;
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
