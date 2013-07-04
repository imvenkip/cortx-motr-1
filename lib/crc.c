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

static m0_bcount_t checksum_crc32_data_len(void);
static int checksum_crc32_compute(const struct m0_bufvec *data, uint64_t blk_size,
				  const uint64_t mask, uint64_t element_size,
				  const struct m0_bufvec *di_data);
static bool checksum_crc32_verify(const struct m0_bufvec *data, uint64_t blk_size,
				  const uint64_t mask, uint64_t element_size,
				  const struct m0_bufvec *di_data);

const struct m0_di_checksum_type_ops cksum_crc32_ops = {
	.checksum_data_len = checksum_crc32_data_len,
	.checksum_compute  = checksum_crc32_compute,
	.checksum_verify   = checksum_crc32_verify,
};

static struct m0_di_checksum_type cksum_crc32 = {
	.cht_name = "crc32",
	.cht_id = M0_DI_CHECKSUM_CRC32,
	.cht_ops = &cksum_crc32_ops,
};

static uint32_t crc32_cksum(uint32_t crc, const void *data, uint64_t len)
{
	return crc32(crc, data, len);
}

static m0_bcount_t checksum_crc32_data_len(void)
{
	return 32;
}

static int checksum_crc32_compute(const struct m0_bufvec *data, uint64_t blk_size,
				  const uint64_t mask, uint64_t element_size,
				  const struct m0_bufvec *di_data)
{
        struct m0_bufvec_cursor  data_cur;
        struct m0_bufvec_cursor  cksum_cur;
	uint64_t		*blk_data = NULL;
	uint64_t		*cksum = NULL;
	uint32_t		 crc = ~0;
	uint32_t		 count;
	uint32_t		 pos;

	count = m0_no_of_bits_set(mask);
	pos   = m0_start_of_bit_set(mask);

	m0_bufvec_cursor_init(&data_cur, data);
        m0_bufvec_cursor_init(&cksum_cur, di_data);
	m0_bufvec_cursor_move(&cksum_cur, pos * element_size);

	while (!(m0_bufvec_cursor_move(&data_cur, blk_size))) {
		blk_data = m0_bufvec_cursor_addr(&data_cur);
		cksum = m0_bufvec_cursor_addr(&cksum_cur);
		crc = crc32_cksum(crc, blk_data, blk_size);
		cksum[0] = crc;
		m0_bufvec_cursor_move(&cksum_cur, count * element_size);
	}
	return 0;
}

static bool checksum_crc32_verify(const struct m0_bufvec *data, uint64_t blk_size,
				  uint64_t mask, uint64_t element_size,
				  const struct m0_bufvec *di_data)
{
	return true;
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

static inline void md_crc32_cksum_set(void *data, uint64_t len, uint64_t *cksum)
{
	*cksum = md_crc32_cksum(data, len, cksum);
}

static inline bool md_crc32_cksum_verify(void *data, uint64_t len,
					 uint64_t *cksum)
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
