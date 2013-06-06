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
 * Original creation date: 14/06/2013
 */

#include "lib/assert.h"
#include "lib/checksum.h"

/**
   @addtogroup checksum

   @{
 */

enum {
	DATA_BLK_SIZE_512 = 512,
	DATA_BLK_SIZE_4K  = 4096,
	CHECKSUM_SIZE_32  = 32,
	CHECKSUM_SIZE_64  = 64,
	CHECKSUM_SIZE_128 = 128,
	CHECKSUM_SIZE_256 = 256,
};

M0_INTERNAL m0_bcount_t m0_di_data_blocksize(enum m0_di_type di_type)
{
	if (di_type == M0_DI_T10)
		return DATA_BLK_SIZE_512;
	else
		return DATA_BLK_SIZE_4K;
}

static const struct m0_di_checksum_type *ctypes[M0_DI_CHECKSUM_NR];

struct m0_di_checksum_type cksum_none;

static m0_bcount_t checksum_none_data_len(void)
{
	return CHECKSUM_SIZE_32;
}

static int checksum_none_compute(const struct m0_buf *data, uint32_t blk_size,
				 struct m0_di_checksum *csum)
{
	return 0;
}

const struct m0_di_checksum_type_ops cksum_none_ops = {
	.checksum_data_len = checksum_none_data_len,
	.checksum_compute  = checksum_none_compute,
};

M0_DI_CSUM_TYPE_DECLARE(cksum_none, "none", M0_DI_CHECKSUM_NONE,
			&cksum_none_ops);

M0_INTERNAL int m0_di_checksum_type_register(struct m0_di_checksum_type *ct)
{
	M0_PRE(ct != NULL);
	M0_PRE(ct->cht_id < M0_DI_CHECKSUM_NR);

	ctypes[ct->cht_id] = ct;
	return 0;
}

M0_INTERNAL void m0_di_checksum_type_deregister(struct m0_di_checksum_type *ct)
{
	M0_PRE(ct != NULL);
	M0_PRE(ct->cht_id < M0_DI_CHECKSUM_NR);

	ctypes[ct->cht_id] = NULL;
}

M0_INTERNAL const struct m0_di_checksum_type *m0_di_checksum_type_lookup(
						uint64_t cid)
{
	M0_PRE(IS_IN_ARRAY(cid, ctypes));
	return ctypes[cid];
}

M0_INTERNAL uint64_t m0_di_tag_compute(const struct m0_fid *gfid,
				       const struct m0_fid *fid,
				       uint64_t offset)
{
	return NULL;
}

M0_INTERNAL bool m0_di_tag_verify(const uint64_t tag, const struct m0_fid *gfid,
				  const struct m0_fid *fid, uint64_t offset)
{

	return true;
}

M0_INTERNAL int m0_data_integrity_init(void)
{
	int rc;

	rc = m0_di_checksum_type_register(&cksum_none);
	/** @todo Also register other checksum algorithm types. */
	return rc;
}

M0_INTERNAL void m0_data_integrity_fini(void)
{
	m0_di_checksum_type_deregister(&cksum_none);
}

/** @} end of checksum */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
