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
#include "be/op.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "ut/ut.h"
#include "ut/stob.h"

#include <sys/mman.h>
#include <unistd.h>

enum {
	BE_UT_PD_SEG_SIZE = 0x1000000,
	BE_UT_PD_PAGE_SIZE = 0x2000,
};

static bool be_ut_pd_page_is_resident(const struct m0_be_pd_page *page)
{
	unsigned char  status;
	unsigned char  status_last;
	void          *addr;
	bool           result;
	int            sys_page_size;
	int            rc;
	int            rc_last;
	int            i;

	sys_page_size = m0_pagesize_get();

	M0_UT_ASSERT(page->pp_size / sys_page_size * sys_page_size ==
		     page->pp_size);

	addr = page->pp_page;
	for (i = 0; i < page->pp_size / sys_page_size; ++i) {
		M0_UT_ASSERT(m0_be_pd_mapping__is_addr_in_page(page, addr));
		rc = mincore(addr, sys_page_size, &status);
		/* ENOMEM means that the page is not mapped. */
		M0_UT_ASSERT(rc == 0 || errno == ENOMEM);
		result = rc == 0 && (status & 1) == 1;
		addr = (char *)addr + sys_page_size;

		/* All system pages must be in the same state within BE page */
		if (i > 0) {
			M0_UT_ASSERT(rc == rc_last);
			M0_UT_ASSERT(ergo(rc == 0,
					  (status & 1) == (status_last & 1)));
		}
		rc_last = rc;
		status_last = status;
	}
	return result;
}

static void m0_be_ut_pd_mapping_resident_with_cfg(struct m0_be_pd_cfg *pd_cfg)
{
	struct m0_be_pd_mapping *mapping;
	struct m0_be_pd_page    *page;
	struct m0_be_pd          paged = {};
	m0_bcount_t              seg_size;
	void                    *seg_addr;
	bool                     resident;
	long                     i;
	int                      sys_page_size;
	int                      rc;

	sys_page_size = m0_pagesize_get();

	rc = m0_be_pd_init(&paged, pd_cfg);
	M0_UT_ASSERT(rc == 0);

	seg_size = BE_UT_PD_SEG_SIZE;
	seg_addr = m0_be_ut_seg_allocate_addr(seg_size);
	M0_UT_ASSERT((seg_size & (sys_page_size - 1)) == 0);
	M0_UT_ASSERT((seg_size & (BE_UT_PD_PAGE_SIZE - 1)) == 0);
	M0_UT_ASSERT(((unsigned long)seg_addr & (BE_UT_PD_PAGE_SIZE - 1)) == 0);
	rc = m0_be_pd_mapping_init(&paged, seg_addr, seg_size,
				   BE_UT_PD_PAGE_SIZE, -1);
	M0_UT_ASSERT(rc == 0);

	mapping = m0_be_pd__mapping_by_addr(&paged, seg_addr);
	M0_UT_ASSERT(mapping != NULL);

	M0_UT_ASSERT(mapping->pas_pcount == seg_size / BE_UT_PD_PAGE_SIZE);

	/* All system pages must be not resident right after initialisation. */
	for (i = 0; i < mapping->pas_pcount; ++i) {
		resident = be_ut_pd_page_is_resident(&mapping->pas_pages[i]);
		M0_UT_ASSERT(!resident);
	}

	/* Attach the second BE page. */
	page = m0_be_pd_mapping__addr_to_page(mapping,
					(char *)seg_addr + BE_UT_PD_PAGE_SIZE);
	M0_UT_ASSERT(page != NULL);
	m0_be_pd_page_lock(page);
	rc = m0_be_pd_mapping_page_attach(mapping, page);
	m0_be_pd_page_unlock(page);
	M0_UT_ASSERT(rc == 0);

	/* Only system pages that represent the page must be resident. */
	for (i = 0; i < mapping->pas_pcount; ++i) {
		resident = be_ut_pd_page_is_resident(&mapping->pas_pages[i]);
		M0_UT_ASSERT(equi(page == &mapping->pas_pages[i], resident));
	}

	/* Detach the page */
	m0_be_pd_page_lock(page);
	rc = m0_be_pd_mapping_page_detach(mapping, page);
	m0_be_pd_page_unlock(page);
	M0_UT_ASSERT(rc == 0);

	/* All system pages must be not resident at this point. */
	for (i = 0; i < mapping->pas_pcount; ++i) {
		resident = be_ut_pd_page_is_resident(&mapping->pas_pages[i]);
		M0_UT_ASSERT(!resident);
	}

	rc = m0_be_pd_mapping_fini(&paged, seg_addr, seg_size);
	M0_UT_ASSERT(rc == 0);
	m0_be_pd_fini(&paged);
}

void m0_be_ut_pd_mapping_resident(void)
{
	struct m0_be_domain_cfg  cfg = {};
	struct m0_be_pd_cfg     *pd_cfg;

	m0_be_ut_backend_cfg_default(&cfg);
	pd_cfg = &cfg.bc_pd_cfg;

	pd_cfg->bpc_mapping_type = M0_BE_PD_MAPPING_PER_PAGE;
	m0_be_ut_pd_mapping_resident_with_cfg(pd_cfg);

	pd_cfg->bpc_mapping_type = M0_BE_PD_MAPPING_SINGLE;
	m0_be_ut_pd_mapping_resident_with_cfg(pd_cfg);
}

/* ----------------------------------------------------------------------------
 * The real PAGED test.
 * -------------------------------------------------------------------------- */

enum {
	BE_UT_PD_FOM_SEG_SIZE = 0x100000,
};

void m0_be_ut_pd_fom(void)
{
	struct m0_be_domain_cfg  cfg = {};
	struct m0_be_pd_cfg     *pd_cfg;
	struct m0_be_pd          paged = {};
	struct m0_be_seg         seg = {};
	struct m0_stob          *stob;
	void                    *addr;
	int                      rc;

	m0_be_pd_fom_mod_init();

	m0_be_ut_backend_cfg_default(&cfg);

	/* XXX: weird cfgs */
	m0_be_tx_group_seg_io_credit(&cfg.bc_engine.bec_group_cfg,
		     &cfg.bc_pd_cfg.bpc_io_sched_cfg.bpdc_io_credit);


	pd_cfg = &cfg.bc_pd_cfg;
	m0_be_ut_reqh_create(&pd_cfg->bpc_reqh);

	rc = m0_be_pd_init(&paged, pd_cfg);
	M0_UT_ASSERT(rc == 0);

	stob = m0_ut_stob_linux_get();
	m0_be_seg_init(&seg, stob, NULL, &paged, 42);
	addr = m0_be_ut_seg_allocate_addr(BE_UT_PD_FOM_SEG_SIZE);
	rc = m0_be_seg_create(&seg, BE_UT_PD_FOM_SEG_SIZE, addr);
	M0_UT_ASSERT(rc == 0);
	//m0_be_seg_close(&seg); // XXX: why this thing is here????
	rc = m0_be_seg_open(&seg);
	M0_UT_ASSERT(rc == 0);

	M0_BE_OP_SYNC(op, m0_be_pd_reg_get(&paged,
					   &M0_BE_REG(&seg, 1, addr), &op));
	m0_be_pd_reg_put(&paged, &M0_BE_REG(&seg, 1, addr));

	m0_be_seg_close(&seg);
	rc = m0_be_seg_destroy(&seg);
	M0_UT_ASSERT(rc == 0);

	m0_be_seg_fini(&seg);

	m0_ut_stob_put(stob, true);

	m0_be_pd_fini(&paged);
	m0_be_ut_reqh_destroy();

	m0_be_pd_fom_mod_fini();
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
