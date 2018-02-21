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
	BE_UT_PD_FOM_STOB_KEY = 0xf0f11e,
};

void m0_be_ut_pd_fom(void)
{
	struct m0_be_domain_cfg  cfg = {};
	struct m0_be_pd_cfg     *pd_cfg;
	struct m0_be_pd          paged = {};
	struct m0_be_seg         seg = {};
	struct m0_be_reg_area    reg_area;
	int			 reg_area_nr = 2;		 /* XXX */
	struct m0_ext            write_ext = { .e_end = 12288 }; /* XXX */
	struct m0_be_reg         reg;
	void                    *addr;
	int                      rc;
	int			 dummy = 0x12345678;

	struct m0_be_0type_seg_cfg seg_cfg;

	m0_be_ut_backend_cfg_default(&cfg);

	/* >>>>> XXX: weird cfgs stated !!!! <<<<< */
	rc = m0_be_reg_area_init(&reg_area,
				 &M0_BE_TX_CREDIT(reg_area_nr * 2,
						  reg_area_nr * 0x10),
				 M0_BE_REG_AREA_DATA_NOCOPY);
	M0_UT_ASSERT(rc == 0);
	/* >>>>> XXX: weird cfgs ended !!!! <<<<<  */

	pd_cfg = &cfg.bc_pd_cfg;
	m0_be_ut_reqh_create(&pd_cfg->bpc_reqh);

	rc = m0_be_pd_init(&paged, pd_cfg);
	M0_UT_ASSERT(rc == 0);

	addr    = m0_be_ut_seg_allocate_addr(BE_UT_PD_FOM_SEG_SIZE);
	seg_cfg = (struct m0_be_0type_seg_cfg){
		.bsc_stob_key        = BE_UT_PD_FOM_STOB_KEY,
		.bsc_size            = BE_UT_PD_FOM_SEG_SIZE,
		.bsc_addr            = addr,
		.bsc_stob_create_cfg = NULL,
	};
	rc = m0_be_pd_seg_create(&paged, NULL, &seg_cfg);
	M0_UT_ASSERT(rc == 0);
	rc = m0_be_pd_seg_open(&paged, &seg, NULL, seg_cfg.bsc_stob_key);
	M0_UT_ASSERT(rc == 0);

	reg = M0_BE_REG(&seg, 1, addr);
	M0_BE_OP_SYNC(op, m0_be_pd_reg_get(&paged, &reg, &op));
	m0_be_pd_reg_put(&paged, &reg);



	{
		struct m0_be_pd_request       request;
		struct m0_be_pd_request_pages rpages;

		m0_be_reg_area_capture(&reg_area, &M0_BE_REG_D(reg, &dummy));
		reg = M0_BE_REG(&seg, 1, addr+0x10);
		m0_be_reg_area_capture(&reg_area, &M0_BE_REG_D(reg, &dummy));


		m0_be_pd_request_pages_init(&rpages, M0_PRT_WRITE, &reg_area,
					    &write_ext, NULL);
		m0_be_pd_request_init(&request, &rpages);

		M0_BE_OP_SYNC(op, m0_be_pd_request_push(&paged, &request, &op));
	}



	m0_be_pd_seg_close(&paged, &seg);
	rc = m0_be_pd_seg_destroy(&paged, NULL, seg_cfg.bsc_stob_key);
	M0_UT_ASSERT(rc == 0);

	m0_be_pd_fini(&paged);
	m0_be_ut_reqh_destroy();

	m0_be_reg_area_fini(&reg_area);
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
