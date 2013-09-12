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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 01-Sep-2012
 */

#pragma once

#ifndef __MERO_LIB_STRING_H__
#define __MERO_LIB_STRING_H__

/*
 * Define standard string manipulation functions (strcat, strlen, strcmp, &c.)
 * together with sprintf(3) and snprintf(3).
 * Also pick up support for strtoul(3) and variants, and ctype macros.
 */

#ifndef __KERNEL__
# include <ctype.h>
# include <stdio.h>
# include <stdlib.h>
# include <string.h>

#define m0_strdup(s)  strdup((s))
#else
# include <linux/ctype.h>
# include <linux/kernel.h>
# include <linux/string.h>

#define m0_strdup(s)  kstrdup((s), GFP_KERNEL)

static inline char *strerror(int errnum)
{
	return "strerror() is not supported in kernel";
}
#endif /* __KERNEL__ */

#include "lib/types.h"

struct m0_fop_str {
	uint32_t s_len;
	uint8_t *s_buf;
} M0_XCA_SEQUENCE;

#endif /* __MERO_LIB_STRING_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
