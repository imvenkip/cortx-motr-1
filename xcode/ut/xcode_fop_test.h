/* -*- C -*- */
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
 * Original author: Manish Honap <manish_honap@xyratex.com>
 * Original creation date: 19-Dec-2012
 */

#pragma once

#ifndef __MERO_XCODE_FOP_TEST_H__
#define __MERO_XCODE_FOP_TEST_H__

#include "lib/types.h"
#include "xcode/xcode_attr.h"

struct m0_test_buf {
	uint32_t tb_cnt;
	uint8_t *tb_buf;
} M0_XCA_SEQUENCE;

struct m0_test_key {
	uint32_t tk_index;
	uint64_t tk_val;
	uint8_t  tk_flag;
} M0_XCA_RECORD;

struct m0_pair {
	uint64_t           p_offset;
	uint32_t           p_cnt;
	struct m0_test_key p_key;
	struct m0_test_buf p_buf;
} M0_XCA_RECORD;

struct m0_desc_arr {
	uint32_t        da_cnt;
	struct m0_pair *da_pair;
} M0_XCA_SEQUENCE;

struct m0_fop_test_arr {
	uint32_t            fta_cnt;
	struct m0_desc_arr *fta_data;
} M0_XCA_SEQUENCE;

struct m0_fop_test {
	uint32_t               ft_cnt;
	uint64_t               ft_offset;
	struct m0_fop_test_arr ft_arr;
} M0_XCA_RECORD;

#endif /* __MERO_XCODE_FOP_TEST_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
