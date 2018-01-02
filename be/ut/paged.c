/* -*- C -*- */
/*
 * COPYRIGHT 2017 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Dmytro Podgornyi <dmytro.podgornyi@seagate.com>
 * Original creation date: 31-Dec-2017
 */

/**
 * @addtogroup be
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_UT
#include "lib/trace.h"

#include "be/paged.h"
#include "be/ut/helper.h"
#include "lib/memory.h"
#include "ut/ut.h"

#include <sys/mman.h>
#include <unistd.h>

enum {
	BE_UT_PD_SEG_SIZE = 0x1000000,
	BE_UT_PD_PAGE_SIZE = 0x2000,
};

void m0_be_ut_pd_mapping_resident(void)
{
	struct m0_be_pd_mapping *mapping;
	struct m0_be_pd_page    *page;
	struct m0_be_pd_cfg      paged_cfg;
	struct m0_be_pd          paged = {};
	unsigned char           *vec;
	long                     vec_len;
	m0_bcount_t              seg_size;
	void                    *seg_addr;
	long                     sys_page_size;
	long                     i;
	int                      rc;

	sys_page_size = sysconf(_SC_PAGESIZE);
	M0_UT_ASSERT(sys_page_size > 0);

	paged_cfg = (struct m0_be_pd_cfg){
		.bpc_io_sched_cfg = {
			.bpdc_seg_io_nr          = 1,
			.bpdc_seg_io_pending_max = 1,
			.bpdc_io_credit          = M0_BE_IO_CREDIT(1, 1, 1),
		},
	};
	rc = m0_be_pd_init(&paged, &paged_cfg);
	M0_UT_ASSERT(rc == 0);

	seg_size = BE_UT_PD_SEG_SIZE;
	seg_addr = m0_be_ut_seg_allocate_addr(seg_size);
	M0_UT_ASSERT((seg_size & (sys_page_size - 1)) == 0);
	M0_UT_ASSERT((seg_size & (BE_UT_PD_PAGE_SIZE - 1)) == 0);
	rc = m0_be_pd_mapping_init(&paged, seg_addr, seg_size,
				   BE_UT_PD_PAGE_SIZE);
	M0_UT_ASSERT(rc == 0);

	vec_len = seg_size / sys_page_size;
	M0_ALLOC_ARR(vec, vec_len);
	M0_UT_ASSERT(vec != NULL);

	/* All system pages must be not resident right after initialisation. */
	rc = mincore(seg_addr, seg_size, vec);
	M0_UT_ASSERT(rc == 0);
	for (i = 0; i < vec_len; ++i)
		M0_UT_ASSERT((vec[i] & 1) == 0);

	mapping = m0_be_pd__mapping_by_addr(&paged, seg_addr);
	M0_UT_ASSERT(mapping != NULL);
	page = m0_be_pd_mapping__addr_to_page(mapping,
					(char *)seg_addr + BE_UT_PD_PAGE_SIZE);
	M0_UT_ASSERT(page != NULL);
	m0_be_pd_page_lock(page);
	rc = m0_be_pd_mapping_page_attach(mapping, page);
	m0_be_pd_page_unlock(page);
	M0_UT_ASSERT(rc == 0);

	/* Only system pages that represent the page must be resident. */
	rc = mincore(seg_addr, seg_size, vec);
	M0_UT_ASSERT(rc == 0);
	for (i = 0; i < vec_len; ++i)
		M0_UT_ASSERT(m0_be_pd_mapping__is_addr_in_page(page,
				(char *)seg_addr + i * sys_page_size) ?
							(vec[i] & 1) == 1 :
							(vec[i] & 1) == 0);

	m0_be_pd_page_lock(page);
	rc = m0_be_pd_mapping_page_detach(mapping, page);
	m0_be_pd_page_unlock(page);
	M0_UT_ASSERT(rc == 0);

	/* All system pages must be not resident at this point. */
	rc = mincore(seg_addr, seg_size, vec);
	M0_UT_ASSERT(rc == 0);
	for (i = 0; i < vec_len; ++i)
		M0_UT_ASSERT((vec[i] & 1) == 0);

	m0_free(vec);

	rc = m0_be_pd_mapping_fini(&paged, seg_addr, seg_size);
	M0_UT_ASSERT(rc == 0);
	m0_be_pd_fini(&paged);
}

#undef M0_TRACE_SUBSYSTEM

/** @} end of be group */

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
