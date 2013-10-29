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
#undef M0_TRACE_SUBSYSTEM
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_FILE

#include "lib/trace.h"
#include "stob/battr.h"
#include "file/di.h"
#include "file/file.h"
#include "lib/misc.h"
#include "lib/vec_xc.h"
#include "file/crc.c"

static uint64_t file_di_sw_mask(const struct m0_file *file);
static uint64_t file_di_sw_in_shift(const struct m0_file *file);
static uint64_t file_di_sw_out_shift(const struct m0_file *file);
static void	file_di_sw_sum(const struct m0_file *file,
			       const struct m0_io_indexvec_seq *io_info,
			       const struct m0_bufvec *in_data,
			       struct m0_bufvec *di_data);
static bool	file_di_sw_check(const struct m0_file *file,
				 const struct m0_io_indexvec_seq *io_info,
				 const struct m0_bufvec *in_data,
				 const struct m0_bufvec *di_data);
static void	file_checksum(void (*checksum)(const void *data, uint64_t bsize,
					       uint64_t *di, uint32_t nr),
			      const struct m0_bufvec *in_data,
			      uint64_t bsize, uint32_t cksum_len,
			      uint32_t bit_set_nr, int pos,
			      struct m0_bufvec *di_data);
static bool	file_checksum_check(void (*checksum)(const void *data,
						     uint64_t bsize,
						     uint64_t *di, uint32_t nr),
				    const struct m0_bufvec *in_data,
				    uint64_t bsize, uint32_t cksum_len,
				    uint32_t bit_set_nr, int pos,
				    const struct m0_bufvec *di_data);

static void t10_ref_tag_compute(const struct m0_io_indexvec_seq *io_info,
			        uint64_t blk_size, uint32_t bit_set_nr,
				uint32_t pos, struct m0_bufvec *di_data);
static bool t10_ref_tag_check(const struct m0_io_indexvec_seq *io_info,
			      uint64_t blk_size, uint32_t bit_set_nr,
			      uint32_t pos, const struct m0_bufvec *di_data);

static struct m0_di_type file_di_type_sw = {
	.dt_name = "crc32-4k+t10-ref-tag",
};

static const struct m0_di_ops di_ops[M0_DI_NR] = {
	[M0_DI_CRC32_4K] = {
		.do_type      = &file_di_type_sw,
		.do_mask      = file_di_sw_mask,
		.do_in_shift  = file_di_sw_in_shift,
		.do_out_shift = file_di_sw_out_shift,
		.do_sum       = file_di_sw_sum,
		.do_check     = file_di_sw_check,
	},
};

static uint64_t file_di_sw_mask(const struct m0_file *file)
{
	return M0_BITS(M0_BI_CKSUM_CRC_32, M0_BI_REF_TAG);
}

static uint64_t file_di_sw_in_shift(const struct m0_file *file)
{
	return 12;
}

static uint64_t file_di_sw_out_shift(const struct m0_file *file)
{
	return m0_no_of_bits_set(file_di_sw_mask(file));
}

static void file_di_sw_sum(const struct m0_file *file,
			   const struct m0_io_indexvec_seq *io_info,
			   const struct m0_bufvec *in_data,
			   struct m0_bufvec *di_data)
{
	uint64_t bsize;
	uint32_t bit_set_nr;
	uint32_t pos = 0;

	M0_PRE(file != NULL);
	M0_PRE(in_data != NULL);
	M0_PRE(di_data != NULL);
	M0_PRE(io_info != NULL);

	bsize	   = M0_BITS(file->fi_di_ops->do_in_shift(file));
	bit_set_nr = m0_no_of_bits_set(file->fi_di_ops->do_mask(file));

	file_checksum(&m0_crc32, in_data, bsize, M0_DI_CRC32_LEN, bit_set_nr,
		      pos, di_data);
	t10_ref_tag_compute(io_info, bsize, bit_set_nr, pos += M0_DI_CRC32_LEN,
			    di_data);
}

/*
 * For each "bsize" block in "in_data" call checksum function and store
 * result as the "pos"-th block attribute in "di_data".
 *
 *  @param bit_set_nr Number of elements(64-bit) of di data for each
 *		      block of data.
 *  @param bsize size of the block of data.
 *  @param cksum_len length of the checksum for each block of data.
 *  @param pos starting position of checksum in 'N' element di data.
 */
static void file_checksum(void (*checksum)(const void *data,
					   uint64_t bsize,
					   uint64_t *di, uint32_t nr),
			  const struct m0_bufvec *in_data,
			  uint64_t bsize, uint32_t cksum_len,
			  uint32_t bit_set_nr, int pos,
			  struct m0_bufvec *di_data)
{
        struct m0_bufvec_cursor  data_cur;
        struct m0_bufvec_cursor  cksum_cur;
	uint64_t		*blk_data = NULL;
	uint64_t		*cksum = NULL;
	uint64_t		*di;

	M0_PRE(in_data != NULL);
	M0_PRE(di_data != NULL);
	M0_PRE(cksum_len != 0);
	M0_PRE(pos < bit_set_nr);

	di = m0_alloc(cksum_len);
	if (di == NULL)
		return;

	m0_bufvec_cursor_init(&data_cur, in_data);
        m0_bufvec_cursor_init(&cksum_cur, di_data);

	m0_bufvec_cursor_move(&cksum_cur, pos * M0_DI_ELEMENT_SIZE);
	do {
		int i;

		blk_data = m0_bufvec_cursor_addr(&data_cur);
		cksum = m0_bufvec_cursor_addr(&cksum_cur);

		checksum(blk_data, bsize, di, cksum_len);
		for (i = 0; i < cksum_len; ++i) {
			cksum[i] = di[i];
			M0_LOG(M0_DEBUG,"crc is:%lu \n",
				(unsigned long int)cksum[i]);
		}
		m0_bufvec_cursor_move(&cksum_cur,
				      bit_set_nr * M0_DI_ELEMENT_SIZE);
	} while (!m0_bufvec_cursor_move(&data_cur, bsize));
}

static bool file_di_sw_check(const struct m0_file *file,
			     const struct m0_io_indexvec_seq *io_info,
			     const struct m0_bufvec *in_data,
			     const struct m0_bufvec *di_data)
{
	uint64_t bsize;
	uint32_t bit_set_nr;
	uint32_t pos = 0;

	M0_PRE(file != NULL);
	M0_PRE(in_data != NULL);
	M0_PRE(di_data != NULL);
	M0_PRE(io_info != NULL);

	bsize	   = M0_BITS(file->fi_di_ops->do_in_shift(file));
	bit_set_nr = m0_no_of_bits_set(file->fi_di_ops->do_mask(file));

	return file_checksum_check(&m0_crc32, in_data, bsize, M0_DI_CRC32_LEN,
				   bit_set_nr, pos, di_data) ?:
	       t10_ref_tag_check(io_info, bsize, bit_set_nr,
				 pos += M0_DI_CRC32_LEN, di_data);
}

/*
 * For each "bsize" block in "in_data" call checksum function and compare the
 * result with the "pos"-th block attribute in "di_data".
 *
 *  @param bit_set_nr Number of elements(64-bit) of di data for each
 *		      block of data.
 *  @param bsize size of the block of data.
 *  @param cksum_len length of the checksum for each block of data.
 *  @param pos starting position of checksum in 'N' element di data.
 */
static bool file_checksum_check(void (*checksum)(const void *data,
						 uint64_t bsize,
						 uint64_t *di, uint32_t nr),
				const struct m0_bufvec *in_data,
				uint64_t bsize, uint32_t cksum_len,
				uint32_t bit_set_nr, int pos,
				const struct m0_bufvec *di_data)
{
        struct m0_bufvec_cursor  data_cur;
        struct m0_bufvec_cursor  cksum_cur;
	uint64_t		*blk_data = NULL;
	uint64_t		*cksum = NULL;
	uint64_t		*di;

	M0_PRE(cksum_len != 0);
	M0_PRE(in_data != NULL);
	M0_PRE(di_data != NULL);
	M0_PRE(pos < bit_set_nr);

	di = m0_alloc(cksum_len);
	M0_ASSERT(di != NULL);
	if (di == NULL)
		return false;

	m0_bufvec_cursor_init(&data_cur, in_data);
        m0_bufvec_cursor_init(&cksum_cur, di_data);

	m0_bufvec_cursor_move(&cksum_cur, pos * M0_DI_ELEMENT_SIZE);
	do {
		int i;

		blk_data = m0_bufvec_cursor_addr(&data_cur);
		cksum = m0_bufvec_cursor_addr(&cksum_cur);

		checksum(blk_data, bsize, di, cksum_len);
		for (i = 0; i < cksum_len; ++i) {
			M0_LOG(M0_DEBUG,"crc is:%lu old crc: %lu\n",
			(unsigned long int)cksum[i], (unsigned long int)di[i]);
			if (cksum[i] != di[i])
				return false;
		}
		m0_bufvec_cursor_move(&cksum_cur,
				      bit_set_nr * M0_DI_ELEMENT_SIZE);
	} while (!m0_bufvec_cursor_move(&data_cur, bsize));
	return true;
}

static void t10_ref_tag_compute(const struct m0_io_indexvec_seq *io_info,
			        uint64_t blk_size,
			        uint32_t bit_set_nr, uint32_t pos,
			        struct m0_bufvec *di_data)
{
        struct m0_bufvec_cursor    cksum_cur;
	uint64_t		  *cksum = NULL;
	int			   i = 0;
	int			   j = 0;

        m0_bufvec_cursor_init(&cksum_cur, di_data);

	m0_bufvec_cursor_move(&cksum_cur, pos * M0_DI_ELEMENT_SIZE);
	do {
		struct m0_io_indexvec *io_vec = io_info->cis_ivecs;

		cksum = m0_bufvec_cursor_addr(&cksum_cur);

		*cksum = io_vec[i].ci_iosegs[j].ci_index +
			 io_vec[i].ci_iosegs[j].ci_count;
		M0_LOG(M0_DEBUG,"i:%d j:%d Number of segemnts:%d \n", i, j,
				io_vec[i].ci_nr);
		if (j == io_vec[i].ci_nr - 1) {
			i++;
			j = 0;
		}
		j++;
		m0_bufvec_cursor_move(&cksum_cur,
				      bit_set_nr * M0_DI_ELEMENT_SIZE);
	} while (i < io_info->cis_nr);
}

static bool t10_ref_tag_check(const struct m0_io_indexvec_seq *io_info,
			      uint64_t blk_size, uint32_t bit_set_nr,
			      uint32_t pos, const struct m0_bufvec *di_data)
{
        struct m0_bufvec_cursor  cksum_cur;
	uint64_t		*cksum = NULL;
	int			 i = 0;
	int			 j = 0;

        m0_bufvec_cursor_init(&cksum_cur, di_data);

	m0_bufvec_cursor_move(&cksum_cur, pos * M0_DI_ELEMENT_SIZE);
	do {
		struct m0_io_indexvec *io_vec = io_info->cis_ivecs;

		cksum = m0_bufvec_cursor_addr(&cksum_cur);
		if (*cksum != io_vec[i].ci_iosegs[j].ci_index +
			      io_vec[i].ci_iosegs[j].ci_count)
			return false;
		M0_LOG(M0_DEBUG,"i:%d j:%d Number of segemnts:%d \n", i, j,
				io_vec[i].ci_nr);
		if (j == io_vec[i].ci_nr - 1) {
			i++;
			j = 0;
		}
		j++;

		m0_bufvec_cursor_move(&cksum_cur,
				      bit_set_nr * M0_DI_ELEMENT_SIZE);
	} while (i < io_info->cis_nr);
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
	md_crc32_cksum_set(addr, nob, cksum_field);
}

M0_INTERNAL bool m0_md_di_chk(void *addr, m0_bcount_t nob,
			      uint64_t *cksum_field)
{
	return md_crc32_cksum_check(addr, nob, cksum_field);
}

#undef M0_TRACE_SUBSYSTEM
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
