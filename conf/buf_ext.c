/* -*- c -*- */
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 19-Sep-2012
 */

#include "conf/buf_ext.h"
#include "lib/buf.h"
#include "lib/misc.h"   /* memcmp, memcpy, strlen, memchr */
#include "lib/memory.h" /* M0_ALLOC_ARR */

M0_INTERNAL bool m0_buf_is_aimed(const struct m0_buf *buf)
{
	return buf->b_nob > 0 && buf->b_addr != NULL;
}

M0_INTERNAL bool m0_buf_streq(const struct m0_buf *buf, const char *str)
{
	M0_PRE(m0_buf_is_aimed(buf) && str != NULL);

	return memcmp(str, buf->b_addr, buf->b_nob) == 0 &&
		strlen(str) == buf->b_nob;
}

M0_INTERNAL char *m0_buf_strdup(const struct m0_buf *buf)
{
	size_t len;
	char  *s;

	M0_PRE(m0_buf_is_aimed(buf));

	/* Measure the size of payload. */
	s = memchr(buf->b_addr, 0, buf->b_nob);
	len = s == NULL ? buf->b_nob : s - (char *)buf->b_addr;

	M0_ALLOC_ARR(s, len + 1);
	if (s != NULL) {
		memcpy(s, buf->b_addr, len);
		s[len] = 0;
	}
	return s;
}
