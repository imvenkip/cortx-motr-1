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
 * Original creation date: 28-Dec-2012
 */

#include "conf/ut/file_helpers.h"
#include "lib/string.h"  /* FILE, fread */
#include "lib/errno.h"   /* EFBIG, errno */

M0_INTERNAL int m0_ut_file_read(const char *path, char *dest, size_t sz)
{
	FILE  *f;
	size_t n;
	int    rc = 0;

	f = fopen(path, "r");
	if (f == NULL)
		return -errno;

	n = fread(dest, 1, sz - 1, f);
	if (ferror(f))
		rc = -errno;
	else if (!feof(f))
		rc = -EFBIG;
	else
		dest[n] = '\0';

	fclose(f);
	return rc;
}
