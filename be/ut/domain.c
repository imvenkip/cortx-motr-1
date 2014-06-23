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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 18-Jul-2013
 */

#include "be/domain.h"

#include "ut/ut.h"
#include "be/ut/helper.h"

void m0_be_ut_mkfs(void)
{
	struct m0_be_ut_backend	 ut_be = {};
	struct m0_be_domain_cfg	 cfg = {};
	struct m0_be_domain	*dom = &ut_be.but_dom;
	struct m0_be_seg	*seg;
	void			*addr;
	void			*addr2;

	m0_be_ut_backend_cfg_default(&cfg);
	/* mkfs mode start */
	m0_be_ut_backend_init_cfg(&ut_be, &cfg, true);
	m0_be_ut_backend_seg_add2(&ut_be, 0x10000, true, &seg);
	addr = seg->bs_addr;
	m0_be_ut_backend_fini(&ut_be);

	M0_SET0(&ut_be);
	/* normal mode start */
	m0_be_ut_backend_init_cfg(&ut_be, &cfg, false);
	seg = m0_be_domain_seg(dom, addr);
	addr2 = seg->bs_addr;
	M0_ASSERT_INFO(addr == addr2, "addr = %p, addr2 = %p", addr, addr2);
	m0_be_ut_backend_seg_del(&ut_be, seg);
	m0_be_ut_backend_fini(&ut_be);

	M0_SET0(&ut_be);
	/* normal mode start */
	m0_be_ut_backend_init_cfg(&ut_be, &cfg, false);
	seg = m0_be_domain_seg(dom, addr);
	M0_ASSERT_INFO(seg == NULL, "seg = %p", seg);
	m0_be_ut_backend_fini(&ut_be);
}

void m0_be_ut_domain(void)
{
	struct m0_be_ut_backend ut_be = {};

	m0_be_ut_backend_init(&ut_be);
	m0_be_ut_backend_fini(&ut_be);
}

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
