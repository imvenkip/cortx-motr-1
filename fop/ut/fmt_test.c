/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 05/19/2010
 */

#include <stdio.h>

#include "lib/errno.h"
#include "lib/ut.h"

#include "fop/fop.h"
#include "test_format_u.h"

#include "fop/fop_format_def.h"
#include "test_format.ff"

extern void test(void);

#define O(v, f) offsetof(typeof(v), f)
#define L(t, n) t ## _memlayout.fm_child[n].ch_offset

void test_fop(void)
{
	struct fid       f;
	struct optfid    of;
	struct fid_array fa;

	struct c2_stob_io_seg seg;
	struct c2_stob_io_buf buf;
	struct c2_stob_io_vec iov;

	struct c2_stob_io_write_fop fop;

	C2_UT_ASSERT(O(f, f_seq) == L(fid, 0));
	C2_UT_ASSERT(O(f, f_oid) == L(fid, 1));

	C2_UT_ASSERT(O(of, b_present) == L(optfid, 0));
	C2_UT_ASSERT(O(of, u.b_fid) == L(optfid, 1));
	C2_UT_ASSERT(O(of, u.b_none) == L(optfid, 2));

	C2_UT_ASSERT(O(fa, fa_nr) == L(fid_array, 0));
	C2_UT_ASSERT(O(fa, fa_fid) == L(fid_array, 1));

	C2_UT_ASSERT(O(seg, f_offset) == L(c2_stob_io_seg, 0));
	C2_UT_ASSERT(O(seg, f_count) == L(c2_stob_io_seg, 1));

	C2_UT_ASSERT(O(buf, csib_count) == L(c2_stob_io_buf, 0));
	C2_UT_ASSERT(O(buf, csib_value) == L(c2_stob_io_buf, 1));

	C2_UT_ASSERT(O(iov, csiv_count) == L(c2_stob_io_vec, 0));
	C2_UT_ASSERT(O(iov, csiv_seg) == L(c2_stob_io_vec, 1));

	C2_UT_ASSERT(O(fop, siw_object) == L(c2_stob_io_write_fop, 0));
	C2_UT_ASSERT(O(fop, siw_vec) == L(c2_stob_io_write_fop, 1));
	C2_UT_ASSERT(O(fop, siw_buf) == L(c2_stob_io_write_fop, 2));
}

const struct c2_test_suite fop_ut = {
	.ts_name = "fop-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "fop", test_fop },
		{ NULL, NULL }
	}
};

/** @} end of fop group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
