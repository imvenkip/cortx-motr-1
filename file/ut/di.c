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

#include "ut/ut.h"
#include "file/di.h"
#include "file/file.h"
#include "file/di.c"
#include "lib/trace.h"

extern const struct m0_rm_resource_type_ops file_lock_type_ops;

enum {
	BUFFER_SIZE = 4096,
	SEGS_NR	    = 16,
};

struct m0_file		   file;
struct m0_rm_domain	   res_dom;
struct m0_rm_resource_type res_type;
m0_bcount_t   size = BUFFER_SIZE;
struct m0_bufvec	   data;
void			  *di_data;
struct m0_io_indexvec_seq  io_info;
struct m0_io_indexvec	   io_vec;
struct m0_ioseg		   io_seg[SEGS_NR];
uint32_t		   blk_size;
uint32_t		   bit_set_nr;
uint32_t		   pos = 0;

void file_di_init(void)
{
	struct m0_fid fid;
	int	      rc;
	int	      i;

	rc = m0_bufvec_alloc(&data, SEGS_NR, BUFFER_SIZE);
	M0_ASSERT(rc == 0);
	for (i = 0; i < data.ov_vec.v_nr; ++i)
		memset(data.ov_buf[i], ('a' + i), BUFFER_SIZE);

	io_vec.ci_nr = data.ov_vec.v_nr;
	io_vec.ci_iosegs = io_seg;
	for (i = 0; i < data.ov_vec.v_nr; ++i) {
		io_seg[i].ci_index = i * size;
		io_seg[i].ci_count = size;
	}

	io_info.cis_nr = 1;
	io_info.cis_ivecs = &io_vec;

	res_type.rt_id  = M0_RM_FLOCK_RT;
	res_type.rt_ops = &file_lock_type_ops;
	m0_rm_domain_init(&res_dom);
	rc = m0_rm_type_register(&res_dom, &res_type);
	M0_UT_ASSERT(rc == 0);
	m0_fid_set(&fid, 1, 0);
	m0_file_init(&file, &fid, &res_dom, M0_DI_CRC32_4K);

	blk_size   = M0_BITS(file_di_sw_in_shift(&file));
	bit_set_nr = m0_no_of_bits_set(file_di_sw_mask(&file));

	size = file_di_sw_out_shift(&file) * io_info.cis_nr *
		io_info.cis_ivecs->ci_nr * M0_DI_ELEMENT_SIZE;
	di_data = m0_alloc(size);
	M0_UT_ASSERT(di_data != NULL);
}

void file_checksum_test(void)
{
	struct m0_bufvec cksum_data = M0_BUFVEC_INIT_BUF(&di_data, &size);

	file_checksum(&m0_crc32, &data, blk_size, M0_DI_CRC32_LEN, bit_set_nr,
		      pos, &cksum_data);
	M0_UT_ASSERT(file_checksum_check(&m0_crc32, &data, blk_size,
			M0_DI_CRC32_LEN, bit_set_nr, pos, &cksum_data));
}

void file_ref_tag_test(void)
{
	struct m0_bufvec cksum_data = M0_BUFVEC_INIT_BUF(&di_data, &size);

	t10_ref_tag_compute(&io_info, blk_size, bit_set_nr,
			    pos += M0_DI_CRC32_LEN, &cksum_data);
	pos = 0;
	M0_UT_ASSERT(t10_ref_tag_check(&io_info, blk_size, bit_set_nr,
				 pos += M0_DI_CRC32_LEN, &cksum_data));
}

void file_di_test(void)
{
	struct m0_bufvec cksum_data = M0_BUFVEC_INIT_BUF(&di_data, &size);

	file.fi_di_ops->do_sum(&file, &io_info, &data, &cksum_data);

	M0_UT_ASSERT(file.fi_di_ops->do_check(&file, &io_info, &data,
		     &cksum_data));
}

void file_di_fini(void)
{
	m0_file_fini(&file);
	m0_rm_type_deregister(&res_type);
	m0_rm_domain_fini(&res_dom);
	m0_bufvec_free(&data);
	m0_free(di_data);
}

const struct m0_test_suite di_ut = {
	.ts_name = "di-ut",
	.ts_tests = {
		{ "di-init", file_di_init},
		{ "di-cksum-test", file_checksum_test},
		{ "di-ref-tag-test", file_ref_tag_test},
		{ "di-test", file_di_test},
		{ "di-fini", file_di_fini},
		{ NULL, NULL },
	},
};
