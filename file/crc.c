/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Madhavrao Vemuri <madhav_vemuri@xyratex.com>
 * Original creation date: 01/07/2013
 */

#ifndef __KERNEL__
#include <zlib.h>
#else
#include <linux/crc32.h>
#endif
#include "lib/misc.h"

/**
   @addtogroup data_integrity

   @{
 */

static uint32_t crc32_cksum(uint32_t crc, const void *data, uint64_t len);
static uint64_t md_crc32_cksum(void *data, uint64_t len, uint64_t *cksum);

M0_INTERNAL void m0_crc32(const void *data, uint64_t len, uint64_t *di,
			  uint32_t nr)
{
	M0_PRE(di != NULL);
	M0_PRE(nr > 0);

	di[0] = crc32(~0, data, len);
}

static void md_crc32_cksum_set(void *data, uint64_t len, uint64_t *cksum)
{
	*cksum = md_crc32_cksum(data, len, cksum);
}

static uint64_t md_crc32_cksum(void *data, uint64_t len, uint64_t *cksum)
{
	uint64_t crc = ~0;
	uint64_t old_cksum = *cksum;

	*cksum = 0;
	crc = crc32_cksum(crc, data, len);
	*cksum = old_cksum;

	return crc;
}

static uint32_t crc32_cksum(uint32_t crc, const void *data, uint64_t len)
{
	return crc32(crc, data, len);
}

static bool md_crc32_cksum_check(void *data, uint64_t len, uint64_t *cksum)
{
	uint64_t new_cksum;

	new_cksum = md_crc32_cksum(data, len, cksum);

	return *cksum == new_cksum;
}

/** @} end of data_integrity */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
