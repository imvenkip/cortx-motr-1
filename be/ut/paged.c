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
#include "be/tx_regmap.h"       /* m0_be_reg_area */
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/semaphore.h"      /* m0_semaphore */
#include "ut/ut.h"
#include "ut/stob.h"
#include "fop/fom_simple.h"     /* m0_fom_simple */
#include "reqh/reqh.h"          /* m0_reqh_nr_localities */
#include "reqh/reqh_service.h"  /* m0_reqh_service_init */

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

	addr = page->pp_addr;
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
	M0_UT_ASSERT((seg_size & (pd_cfg->bpc_page_size - 1)) == 0);
	M0_UT_ASSERT(((unsigned long)seg_addr & (pd_cfg->bpc_page_size - 1))
		     == 0);
	rc = m0_be_pd_mapping_init(&paged, seg_addr, seg_size, -1);
	M0_UT_ASSERT(rc == 0);

	/* Check that function handles not existent mappings */
	M0_UT_ASSERT(m0_be_pd__mapping_by_addr(&paged, NULL) == NULL);
	mapping = m0_be_pd__mapping_by_addr(&paged, seg_addr);
	M0_UT_ASSERT(mapping != NULL);

	M0_UT_ASSERT(mapping->pas_pcount == seg_size / pd_cfg->bpc_page_size);

	/* All system pages must be not resident right after initialisation. */
	for (i = 0; i < mapping->pas_pcount; ++i) {
		resident = be_ut_pd_page_is_resident(&mapping->pas_pages[i]);
		M0_UT_ASSERT(!resident);
	}

	/* Attach the second BE page. */
	page = m0_be_pd_mapping__addr_to_page(mapping,
				  (char *)seg_addr + pd_cfg->bpc_page_size);
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
	m0_be_ut_reqh_create(&pd_cfg->bpc_reqh);
	pd_cfg->bpc_page_size = BE_UT_PD_PAGE_SIZE;

	pd_cfg->bpc_mapping_type = M0_BE_PD_MAPPING_PER_PAGE;
	m0_be_ut_pd_mapping_resident_with_cfg(pd_cfg);

	pd_cfg->bpc_mapping_type = M0_BE_PD_MAPPING_SINGLE;
	m0_be_ut_pd_mapping_resident_with_cfg(pd_cfg);

	m0_be_ut_reqh_destroy();
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

enum {
	BE_UT_PDGP_SEG_NR         = 0x4,
	BE_UT_PDGP_SEG_SIZE       = 64UL << 10,
	BE_UT_PDGP_STOB_KEY_START = 0x1,
	BE_UT_PDGP_FOMS_PER_LOC   = 0x4,
	BE_UT_PDGP_SEQ_REG_SIZE   = 1UL << 10,
	BE_UT_PDGP_SEQ_REG_NR     = BE_UT_PDGP_SEG_SIZE /
				    BE_UT_PDGP_SEQ_REG_SIZE,
	BE_UT_PDGP_RND_REG_SIZE   = 4UL << 10,
	BE_UT_PDGP_RND_REG_NR     = 0x100,
};
M0_BASSERT(BE_UT_PDGP_SEG_SIZE % BE_UT_PDGP_SEQ_REG_SIZE == 0);

enum be_ut_pd_get_put_fom_work {
	BE_UT_PDGP_SEQ_CHECK,
	BE_UT_PDGP_SEQ_FILL,
	BE_UT_PDGP_RND_CHECK,
	BE_UT_PDGP_RND_FILL,
};

struct be_ut_pd_get_put_fom_ctx {
	struct m0_fom_simple          bugf_fom_s;
	struct m0_semaphore           bugf_done;
	uint64_t                      bugf_rng_seed;
	struct m0_be_reg_area         bugf_reg_area;
	struct m0_be_pd_request       bugf_request;
	struct m0_be_pd_request_pages bugf_request_pages;
};

struct be_ut_pd_get_put_ctx {
	struct m0_reqh                  *bugp_reqh;
	struct m0_be_pd                 *bugp_pd;
	struct m0_be_pd_cfg             *bugp_pd_cfg;
	struct m0_be_seg                *bugp_seg[BE_UT_PDGP_SEG_NR];
	struct be_ut_pd_get_put_fom_ctx *bugp_fctx;
	uint64_t                         bugp_foms_nr;
	uint64_t                         bugp_loc_nr;
	enum be_ut_pd_get_put_fom_work   bugp_work;
	void                            *bugp_seg_data[BE_UT_PDGP_SEG_NR];
	struct m0_atomic64               bugp_ext_counter;
};

/* XXX copy-paste from lib/ut/locality.c */
static void fom_simple_svc_start(struct m0_reqh *reqh)
{
	struct m0_reqh_service_type *stype;
	struct m0_reqh_service      *service;
	int                          rc;

	stype = m0_reqh_service_type_find("simple-fom-service");
	M0_ASSERT(stype != NULL);
	rc = m0_reqh_service_allocate(&service, stype, NULL);
	M0_ASSERT(rc == 0);
	m0_reqh_service_init(service, reqh,
			     &M0_FID_INIT(0xdeadbeef, 0xbeefdead));
	rc = m0_reqh_service_start(service);
	M0_ASSERT(rc == 0);
	M0_POST(m0_reqh_service_invariant(service));
}

static void be_ut_pd_get_put_reg_check(struct m0_be_seg *seg,
                                       struct m0_be_reg *reg,
                                       void             *seg_data)
{
	m0_bindex_t  reg_offset   = m0_be_seg_offset(seg, reg->br_addr);
	char        *seg_actual   = seg->bs_addr;
	char        *seg_expected = seg_data;
	int          i;

	for (i = 0; i < reg->br_size; ++i)
		M0_ASSERT_INFO(seg_expected[reg_offset + i] ==
		               seg_actual[reg_offset + i],
		               "seg_actual=%p seg_expected=%p "
		               "reg_offset=%td i=%d",
		               seg_actual, seg_expected, reg_offset, i);
}

static void be_ut_pd_get_put_reg_random_fill(struct m0_be_seg *seg,
                                             struct m0_be_reg *reg,
                                             uint64_t         *seed)
{
	unsigned char *reg_addr = reg->br_addr;
	int            i;

	for (i = 0; i < reg->br_size; ++i)
		reg_addr[i] = m0_rnd(UCHAR_MAX, seed);
}

static void be_ut_pd_get_put_reg_copy_to_data(struct m0_be_seg *seg,
                                              struct m0_be_reg *reg,
                                              char             *seg_data)
{
	char        *reg_data   = reg->br_addr;
	m0_bindex_t  reg_offset = m0_be_seg_offset(seg, reg->br_addr);
	int          i;

	for (i = 0; i < reg->br_size; ++i)
		seg_data[reg_offset + i] = reg_data[i];
}

static void be_ut_pd_get_put_work(struct be_ut_pd_get_put_ctx     *ctx,
                                  struct be_ut_pd_get_put_fom_ctx *fctx,
                                  struct m0_be_reg                *reg,
                                  int                              seg_index)
{
	struct m0_be_seg *seg      = ctx->bugp_seg[seg_index];
	void             *seg_data = ctx->bugp_seg_data[seg_index];

	switch (ctx->bugp_work) {
	case BE_UT_PDGP_SEQ_CHECK:
	case BE_UT_PDGP_RND_CHECK:
		be_ut_pd_get_put_reg_check(seg, reg, seg_data);
		break;
	case BE_UT_PDGP_SEQ_FILL:
	case BE_UT_PDGP_RND_FILL:
		be_ut_pd_get_put_reg_random_fill(seg, reg,
						 &fctx->bugf_rng_seed);
		be_ut_pd_get_put_reg_copy_to_data(seg, reg, seg_data);
		m0_be_reg_area_capture(&fctx->bugf_reg_area,
				       &M0_BE_REG_D(*reg, NULL));
		break;
	default:
		M0_IMPOSSIBLE("unknown work to do");
	}
}

static int be_ut_pd_get_put_index_of(struct be_ut_pd_get_put_ctx *ctx,
                                     struct m0_fom_simple        *fom_s)
{
	int i;

	for (i = 0; i < ctx->bugp_foms_nr; ++i) {
		if (&ctx->bugp_fctx[i].bugf_fom_s == fom_s)
			break;
	}
	M0_POST(i < ctx->bugp_foms_nr);
	return i;
}

static void be_ut_pd_get_put_segs_close(struct be_ut_pd_get_put_ctx *ctx)
{
	int i;

	for (i = 0; i < BE_UT_PDGP_SEG_NR; ++i)
		m0_be_pd_seg_close(ctx->bugp_pd, ctx->bugp_seg[i]);
}

static void be_ut_pd_get_put_segs_open(struct be_ut_pd_get_put_ctx *ctx)
{
	int rc;
	int i;

	for (i = 0; i < BE_UT_PDGP_SEG_NR; ++i) {
		/*
		 * XXX FIXME stob_key is used as seg_id by default, see
		 * `if' statement in m0_be_seg_init() with M0_BE_SEG_FAKE_ID.
		 */
		rc = m0_be_pd_seg_open(ctx->bugp_pd, ctx->bugp_seg[i], NULL,
		                       BE_UT_PDGP_STOB_KEY_START + i);
		M0_UT_ASSERT(rc == 0);
		M0_UT_ASSERT(ctx->bugp_seg[i] != NULL);
	}
}

static void be_ut_pd_get_put_segs_reopen(struct be_ut_pd_get_put_ctx *ctx,
                                         bool                         reinit_pd)
{
	int rc;

	be_ut_pd_get_put_segs_close(ctx);
	if (reinit_pd) {
		m0_be_pd_fini(ctx->bugp_pd);
		m0_free(ctx->bugp_pd);
		M0_ALLOC_PTR(ctx->bugp_pd);
		rc = m0_be_pd_init(ctx->bugp_pd, ctx->bugp_pd_cfg);
		M0_UT_ASSERT(rc == 0);
	}
	be_ut_pd_get_put_segs_open(ctx);
}

static void be_ut_pd_get_put_reg_random(struct m0_be_seg *seg,
                                        struct m0_be_reg *reg,
                                        m0_bcount_t       reg_size_max,
                                        uint64_t         *seed)
{
	m0_bcount_t reg_size = m0_rnd(reg_size_max - 1, seed) + 1;
	*reg = M0_BE_REG(seg, reg_size,
	                 seg->bs_addr + m0_rnd(seg->bs_size - reg_size, seed));
	M0_UT_ASSERT(m0_be_reg__invariant(reg));
}

static void
be_ut_pd_get_put_reg_area_write(struct be_ut_pd_get_put_ctx     *ctx,
                                struct be_ut_pd_get_put_fom_ctx *fctx)
{
	struct m0_ext ext;
	m0_bindex_t   ext_end;

	ext_end = m0_atomic64_add_return(&ctx->bugp_ext_counter, 1);
	ext = M0_EXT(ext_end - 1, ext_end);
	m0_be_pd_request_pages_init(&fctx->bugf_request_pages,
	                            M0_PRT_WRITE, &fctx->bugf_reg_area,
	                            &ext, NULL);
	m0_be_pd_request_init(&fctx->bugf_request,
	                      &fctx->bugf_request_pages);
	M0_BE_OP_SYNC(op, m0_be_pd_request_push(ctx->bugp_pd,
	                                        &fctx->bugf_request,
	                                        &op));
	m0_be_pd_request_fini(&fctx->bugf_request);
}

static int be_ut_pd_get_put_fom_tick(struct m0_fom *fom, void *data, int *phase)
{
	struct be_ut_pd_get_put_fom_ctx *fctx;
	struct be_ut_pd_get_put_ctx     *ctx = data;
	struct m0_fom_simple            *simpleton;
	struct m0_be_pd                 *pd = ctx->bugp_pd;
	struct m0_be_reg                 reg;
	struct m0_be_seg                *seg;
	void                            *seg_addr;
	m0_bcount_t                      reg_size;
	int                              reg_nr;
	int                              index;
	int                              seg_index;
	int                              i;
	int                              iter_nr;

	simpleton = container_of(fom, struct m0_fom_simple, si_fom);
	index = be_ut_pd_get_put_index_of(ctx, simpleton);
	fctx = &ctx->bugp_fctx[index];
	seg_index = index % BE_UT_PDGP_SEG_NR;
	seg = ctx->bugp_seg[seg_index];
	seg_addr = seg->bs_addr;

	switch (ctx->bugp_work) {
	case BE_UT_PDGP_SEQ_CHECK:
	case BE_UT_PDGP_SEQ_FILL:
		reg_size = BE_UT_PDGP_SEQ_REG_SIZE;
		reg_nr   = BE_UT_PDGP_SEQ_REG_NR;
		iter_nr  = 0;
		for (i = 0; i < reg_nr; ++i) {
			reg = M0_BE_REG(seg, reg_size, seg_addr + i * reg_size);
			if (m0_be_seg_offset(seg, reg.br_addr) <
			    m0_be_seg_reserved(seg))
				continue;
			M0_BE_OP_SYNC(op, m0_be_pd_reg_get(pd, &reg, &op));
			be_ut_pd_get_put_work(ctx, fctx, &reg, seg_index);
			m0_be_pd_reg_put(pd, &reg);
			++iter_nr;
		}
		M0_UT_ASSERT(iter_nr > 0);
		break;
	case BE_UT_PDGP_RND_CHECK:
	case BE_UT_PDGP_RND_FILL:
		/* XXX almost copy-paste */
		reg_size = BE_UT_PDGP_RND_REG_SIZE;
		reg_nr   = BE_UT_PDGP_RND_REG_NR;
		iter_nr  = 0;
		for (i = 0; i < reg_nr; ++i) {
			be_ut_pd_get_put_reg_random(seg, &reg, reg_size,
			                            &fctx->bugf_rng_seed);
			if (m0_be_seg_offset(seg, reg.br_addr) <
			    m0_be_seg_reserved(seg))
				continue;
			M0_BE_OP_SYNC(op, m0_be_pd_reg_get(pd, &reg, &op));
			be_ut_pd_get_put_work(ctx, fctx, &reg, seg_index);
			m0_be_pd_reg_put(pd, &reg);
			++iter_nr;
		}
		M0_UT_ASSERT(iter_nr > 0);
		break;
	default:
		M0_IMPOSSIBLE("unknown work to do");
	};
	if (M0_IN(ctx->bugp_work, (BE_UT_PDGP_SEQ_FILL, BE_UT_PDGP_RND_FILL)))
		be_ut_pd_get_put_reg_area_write(ctx, fctx);
	return -1;
}

static void be_ut_pd_get_put_fom_free(struct m0_fom_simple *fom_s)
{
	struct be_ut_pd_get_put_fom_ctx *fctx;

	fctx = container_of(fom_s, struct be_ut_pd_get_put_fom_ctx, bugf_fom_s);
	m0_semaphore_up(&fctx->bugf_done);
}

static void be_ut_pd_get_put_foms_run(struct be_ut_pd_get_put_ctx    *ctx,
                                      enum be_ut_pd_get_put_fom_work  work)
{
	struct be_ut_pd_get_put_fom_ctx *fctx;
	struct m0_fom_simple            *fom_s;
	int                              foms_to_run;
	int                              i;

	ctx->bugp_work = work;
	switch (ctx->bugp_work) {
	case BE_UT_PDGP_SEQ_CHECK:
	case BE_UT_PDGP_RND_CHECK:
		foms_to_run = ctx->bugp_foms_nr;
		break;
	case BE_UT_PDGP_SEQ_FILL:
	case BE_UT_PDGP_RND_FILL:
		foms_to_run = ARRAY_SIZE(ctx->bugp_seg);
		break;
	default:
		M0_IMPOSSIBLE("unknown work to do");
	}
	M0_ASSERT(foms_to_run <= ctx->bugp_foms_nr);
	for (i = 0; i < foms_to_run; ++i) {
		fctx = &ctx->bugp_fctx[i];
		m0_be_reg_area_reset(&fctx->bugf_reg_area);
		M0_SET0(&fctx->bugf_request);
		M0_SET0(&fctx->bugf_request_pages);
		fom_s = &fctx->bugf_fom_s;
		M0_SET0(fom_s);
		m0_fom_simple_post(fom_s, ctx->bugp_reqh, NULL,
		                   &be_ut_pd_get_put_fom_tick,
		                   &be_ut_pd_get_put_fom_free, ctx, i);
	}
	for (i = 0; i < foms_to_run; ++i)
		m0_semaphore_down(&ctx->bugp_fctx[i].bugf_done);
}

void m0_be_ut_pd_get_put(void)
{
	struct be_ut_pd_get_put_fom_ctx *fctx;
	struct be_ut_pd_get_put_ctx     *ctx;
	struct m0_be_0type_seg_cfg       seg_cfg;
	struct m0_be_domain_cfg         *bd_cfg;
	struct m0_be_tx_credit           tx_credit;
	struct m0_reqh                  *reqh = NULL;
	uint64_t                         loc_nr;
	uint64_t                         foms_nr;
	int                              rc;
	int                              i;

	m0_be_ut_reqh_create(&reqh);
	fom_simple_svc_start(reqh);
	loc_nr = m0_reqh_nr_localities(reqh);
	foms_nr = loc_nr * BE_UT_PDGP_FOMS_PER_LOC;

	M0_ALLOC_PTR(ctx);
	M0_ALLOC_ARR(ctx->bugp_fctx, foms_nr);
	M0_ALLOC_PTR(ctx->bugp_pd);
	M0_ALLOC_PTR(ctx->bugp_pd_cfg);
	ctx->bugp_foms_nr = foms_nr;
	ctx->bugp_loc_nr  = loc_nr;
	ctx->bugp_reqh    = reqh;
	m0_atomic64_set(&ctx->bugp_ext_counter, 0x3377);

	tx_credit = M0_BE_TX_CREDIT(max_check(BE_UT_PDGP_SEQ_REG_NR,
					      BE_UT_PDGP_RND_REG_NR),
	                            max_check(BE_UT_PDGP_SEQ_REG_NR *
					      BE_UT_PDGP_SEQ_REG_SIZE,
	                                      BE_UT_PDGP_RND_REG_NR *
					      BE_UT_PDGP_RND_REG_SIZE));
	for (i = 0; i < foms_nr; ++i) {
		fctx = &ctx->bugp_fctx[i];
		m0_semaphore_init(&fctx->bugf_done, 0);
		/* don't forget to add a seed */
		fctx->bugf_rng_seed = i + 0x5eed;
		rc = m0_be_reg_area_init(&fctx->bugf_reg_area, &tx_credit,
		                         M0_BE_REG_AREA_DATA_COPY);
		M0_UT_ASSERT(rc == 0);
	}
	for (i = 0; i < ARRAY_SIZE(ctx->bugp_seg); ++i)
		M0_ALLOC_PTR(ctx->bugp_seg[i]);
	for (i = 0; i < ARRAY_SIZE(ctx->bugp_seg_data); ++i)
		ctx->bugp_seg_data[i] = m0_alloc(BE_UT_PDGP_SEG_SIZE);

	M0_ALLOC_PTR(bd_cfg);
	m0_be_ut_backend_cfg_default(bd_cfg);
	*ctx->bugp_pd_cfg = bd_cfg->bc_pd_cfg;
	m0_free(bd_cfg);

	ctx->bugp_pd_cfg->bpc_reqh = reqh;
	ctx->bugp_pd_cfg->bpc_io_sched_cfg.bpdc_sched.bisc_pos_start =
		m0_atomic64_get(&ctx->bugp_ext_counter);
	rc = m0_be_pd_init(ctx->bugp_pd, ctx->bugp_pd_cfg);
	M0_UT_ASSERT(rc == 0);

	for (i = 0; i < BE_UT_PDGP_SEG_NR; ++i) {
		seg_cfg = (struct m0_be_0type_seg_cfg){
			.bsc_stob_key = BE_UT_PDGP_STOB_KEY_START + i,
			.bsc_preallocate = false,
			.bsc_size = BE_UT_PDGP_SEG_SIZE,
			.bsc_addr = m0_be_ut_seg_allocate_addr(
			                        BE_UT_PDGP_SEG_SIZE),
			.bsc_stob_create_cfg = NULL,
		};
		rc = m0_be_pd_seg_create(ctx->bugp_pd, NULL, &seg_cfg);
		M0_UT_ASSERT(rc == 0);
	}
	be_ut_pd_get_put_segs_open(ctx);
	be_ut_pd_get_put_foms_run(ctx, BE_UT_PDGP_SEQ_CHECK);
	be_ut_pd_get_put_foms_run(ctx, BE_UT_PDGP_SEQ_FILL);
	be_ut_pd_get_put_foms_run(ctx, BE_UT_PDGP_SEQ_CHECK);
	be_ut_pd_get_put_foms_run(ctx, BE_UT_PDGP_RND_CHECK);
	be_ut_pd_get_put_segs_reopen(ctx, false);
	be_ut_pd_get_put_foms_run(ctx, BE_UT_PDGP_SEQ_CHECK);
	be_ut_pd_get_put_foms_run(ctx, BE_UT_PDGP_RND_FILL);
	be_ut_pd_get_put_foms_run(ctx, BE_UT_PDGP_SEQ_CHECK);
	be_ut_pd_get_put_foms_run(ctx, BE_UT_PDGP_RND_CHECK);
	be_ut_pd_get_put_segs_reopen(ctx, true);
	be_ut_pd_get_put_foms_run(ctx, BE_UT_PDGP_SEQ_CHECK);
	be_ut_pd_get_put_foms_run(ctx, BE_UT_PDGP_RND_CHECK);
	be_ut_pd_get_put_segs_close(ctx);
	for (i = 0; i < BE_UT_PDGP_SEG_NR; ++i) {
		rc = m0_be_pd_seg_destroy(ctx->bugp_pd, NULL,
					  BE_UT_PDGP_STOB_KEY_START + i);
		M0_UT_ASSERT(rc == 0);
	}
	m0_be_pd_fini(ctx->bugp_pd);

	for (i = 0; i < ARRAY_SIZE(ctx->bugp_seg_data); ++i)
		m0_free(ctx->bugp_seg_data[i]);
	for (i = 0; i < ARRAY_SIZE(ctx->bugp_seg); ++i)
		m0_free(ctx->bugp_seg[i]);
	for (i = 0; i < foms_nr; ++i) {
		fctx = &ctx->bugp_fctx[i];
		m0_be_reg_area_fini(&fctx->bugf_reg_area);
		m0_semaphore_fini(&fctx->bugf_done);
	}
	m0_free(ctx->bugp_pd_cfg);
	m0_free(ctx->bugp_pd);
	m0_free(ctx->bugp_fctx);
	m0_free(ctx);
	m0_be_ut_reqh_destroy();
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
