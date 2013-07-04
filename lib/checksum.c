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
#include "lib/misc.h"                 /* IS_IN_ARRAY */
#include "lib/checksum.h"
#include "lib/crc.c"

/**
   @addtogroup data_integrity

   @{
 */

static struct m0_di_checksum_type *ctypes[M0_DI_CHECKSUM_NR];

static m0_bcount_t checksum_none_data_len(void)
{
	return 32;
}

static int checksum_none_compute(const struct m0_bufvec *data, uint64_t blk_size,
				 const uint64_t mask, uint64_t element_size,
				 const struct m0_bufvec *di_data)
{
	return 0;
}

static bool checksum_none_verify(const struct m0_bufvec *data, uint64_t blk_size,
				 const uint64_t mask, uint64_t element_size,
				 const struct m0_bufvec *di_data)
{
	/**
	 * @todo Compute the checksum using checksum_none_compute() and compare
	 * it with di_data for this checksum type.
	 **/
	return true;
}

const struct m0_di_checksum_type_ops cksum_none_ops = {
	.checksum_data_len = checksum_none_data_len,
	.checksum_compute  = checksum_none_compute,
	.checksum_verify = checksum_none_verify,
};

static struct m0_di_checksum_type cksum_none = {
	.cht_name = "none",
	.cht_id   = M0_DI_CHECKSUM_NONE,
	.cht_ops  = &cksum_none_ops,
};

M0_INTERNAL void m0_di_checksum_type_register(struct m0_di_checksum_type *ct)
{
	M0_PRE(ct != NULL);
	M0_PRE(ct->cht_id < M0_DI_CHECKSUM_NR);
	M0_PRE(ctypes[ct->cht_id] == NULL);

	ctypes[ct->cht_id] = ct;
}

M0_INTERNAL void m0_di_checksum_type_deregister(struct m0_di_checksum_type *ct)
{
	M0_PRE(ct != NULL);
	M0_PRE(ct->cht_id < M0_DI_CHECKSUM_NR);
	M0_PRE(ctypes[ct->cht_id] == ct);

	ctypes[ct->cht_id] = NULL;
}

M0_INTERNAL struct m0_di_checksum_type *m0_di_checksum_type_lookup(
						uint64_t cid)
{
	M0_PRE(IS_IN_ARRAY(cid, ctypes));

	return ctypes[cid];
}

M0_INTERNAL int m0_di_checksum_init(void)
{
	int rc = 0;

	m0_di_checksum_type_register(&cksum_none);
	m0_di_checksum_type_register(&cksum_crc32);
	/** @todo Also register other checksum algorithm types. */
	return rc;
}

M0_INTERNAL void m0_di_checksum_fini(void)
{
	m0_di_checksum_type_deregister(&cksum_none);
	m0_di_checksum_type_deregister(&cksum_crc32);
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
