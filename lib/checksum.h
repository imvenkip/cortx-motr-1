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
 * Original creation date: 23/05/2013
 */

#pragma once

#ifndef __MERO_LIB_CHECKSUM_H__
#define __MERO_LIB_CHECKSUM_H__

#include "lib/types.h"
#include "lib/vec.h"
#include "stob/battr.h"

/**
 @defgroup data_integrity
	Data integrity using checksum

	Checksum for data blocks is computed based on checksum algorithm
	selected from configuration.
	Also checksum length is chosen based on this algorithm.

	In m0_di_checksum_init(void)
		- Get checksum algorithm type from configuration.
		- Checksum algorithm type is set in file attributes during
		  file creation.

	 A di data bufvec, consisting of a single buffer is added as m0_buf in iofop
	 and passed across the network.
@{
*/

/* Forward declarations. */
struct m0_di_checksum_type_ops;

/** Checksum algorithm to be used. */
enum m0_di_checksum_alg_type {
	/** No checksum algorithm is used. */
	M0_DI_CHECKSUM_NONE,
	/** CRC algorithm is used to compute 32-bit checksum. */
	M0_DI_CHECKSUM_CRC32 = M0_BI_CKSUM_CRC_32,
	/** SHA-2 checksum algorithm is used to compute 256-bit checksum. */
	M0_DI_CHECKSUM_SHA = M0_BI_CKSUM_SHA256_0,
	/** Fletcher checksum algorithm is used to compute 64-bit checksum. */
	M0_DI_CHECKSUM_FF64 = M0_BI_CKSUM_FLETCHER_64,
	M0_DI_CHECKSUM_NR,
};

struct m0_di_checksum_type {
	const char                           *cht_name;
	/** checksum identifier passed as m0_di_checksum_data::chd_id. */
	uint64_t			      cht_id;
	/** Checksum type operations. */
	const struct m0_di_checksum_type_ops *cht_ops;
	uint64_t                              cht_magic;
};

M0_INTERNAL void m0_di_checksum_type_register(struct m0_di_checksum_type *ct);
M0_INTERNAL void m0_di_checksum_type_deregister(struct m0_di_checksum_type *ct);
M0_INTERNAL struct m0_di_checksum_type *m0_di_checksum_type_lookup(
						uint64_t cid);

struct m0_di_checksum_type_ops {
	/** Returns the length of the checksum. */
	m0_bcount_t (*checksum_data_len)(void);
	/** Computes the checksum on data and stores it in the di data.
	 *  @param mask contains positons of this checksum in N 64-bit element
	 *  @param element_size is size of each element in the di data.
	 */
	int (*checksum_compute)(const struct m0_bufvec *data, uint64_t blk_size,
				const uint64_t mask, uint64_t element_size,
				const struct m0_bufvec *di_data);
	/** Computes the checksum on data and compares it with the di data
	 *  for this checksum type.
	 *  @param mask contains positons of this checksum in N 64-bit element
	 *  @param element_size is size of each element in the di data.
	 */
	bool (*checksum_verify)(const struct m0_bufvec *data, uint64_t blk_size,
				const uint64_t mask, uint64_t element_size,
				const struct m0_bufvec *di_data);
};

M0_INTERNAL int m0_di_checksum_init(void);
M0_INTERNAL void m0_di_checksum_fini(void);

#endif /*  __MERO_LIB_CHECKSUM_H__ */
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
