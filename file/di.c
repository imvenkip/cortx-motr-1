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
 * Original creation date: 05/08/2013
 */

/**
 * @addtogroup data_integrity
 * @{
 */
#include "stob/battr.h"
#include "file/di.h"
#include "lib/misc.h"

static uint64_t file_di_sw_mask(const struct m0_file *file);
static uint64_t file_di_sw_in_shift(const struct m0_file *file);
static uint64_t file_di_sw_out_shift(const struct m0_file *file);
static void     file_di_sw_sum(const struct m0_file *file, uint64_t bitmask,
			       const struct m0_indexvec *io_info,
			       const struct m0_bufvec *in,
			       const struct m0_bufvec *out);
static bool	file_di_sw_check(const struct m0_file *file, uint64_t bitmask,
				 const struct m0_indexvec *io_info,
				 const struct m0_bufvec *in,
				 const struct m0_bufvec *out);


static struct m0_di_type file_di_type_sw = {
	.dt_name = "File data integrity SW type",
};

static const struct m0_di_ops di_ops[M0_DI_NR] = {
	[M0_DI_CRC32_4K] = {
		.do_type = &file_di_type_sw,
		.do_mask = file_di_sw_mask,
		.do_in_shift = file_di_sw_in_shift,
		.do_out_shift = file_di_sw_out_shift,
		.do_sum = file_di_sw_sum,
		.do_check = file_di_sw_check,
	},
};

static uint64_t file_di_sw_mask(const struct m0_file *file)
{
	/**
	 * This mask is fixed for each di type.
	 * Use combination of CRC32 and TAG here.
	 **/
	return M0_BITS(M0_BI_CKSUM_CRC_32, M0_BI_REF_TAG);
}

static uint64_t file_di_sw_in_shift(const struct m0_file *file)
{
	return 1 << 12;
}

static uint64_t file_di_sw_out_shift(const struct m0_file *file)
{
	uint64_t mask = file_di_sw_mask(file);

	return m0_no_of_bits_set(mask) * 64;
}

static void file_di_sw_sum(const struct m0_file *file, uint64_t bitmask,
			   const struct m0_indexvec *io_info,
			   const struct m0_bufvec *in_data,
			   const struct m0_bufvec *di_data)
{
	/**
	 * @todo Compute the di data in di_data for input block data in
	 * in_data.
	 **/
}

static bool file_di_sw_check(const struct m0_file *file, uint64_t bitmask,
			     const struct m0_indexvec *io_info,
			     const struct m0_bufvec *in_data,
			     const struct m0_bufvec *di_data)
{
	/**
	 * @todo Compute the di data for input blocks in in_data using
	 * file_di_sum() and compare it with di data in di_data.
	 **/
	 return true;
}

M0_INTERNAL const struct m0_di_ops *m0_di_ops_get(enum m0_di_types di_type)
{
	M0_PRE(IS_IN_ARRAY(di_type, di_ops));

	return &di_ops[di_type];
}

M0_INTERNAL void m0_md_di_set(void *addr, m0_bcount_t nob,
			      uint64_t *cksum_field)
{

}

M0_INTERNAL bool m0_md_di_chk(void *addr, m0_bcount_t nob,
			      uint64_t *cksum_field)
{
	return true;
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
