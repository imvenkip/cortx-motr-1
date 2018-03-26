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
 * Original author: Anatoliy Bilenko <Anatoliy.Bilenko@seagate.com>
 * Original creation date: 13-Dec-2017
 */

/**
 * @addtogroup PageD
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"        /* M0_AMB */
#include "lib/assert.h"
#include "mero/magic.h"
#include "be/domain.h"       /* m0_be_0type_seg_cfg */
#include "be/paged.h"
#include "be/pd.h"           /* m0_be_pd_io_sched_init */
#include "be/op.h"           /* m0_be_op_wait */
#include "be/pd_service.h"   /* m0_be_pds_stype */
#include "rpc/rpc_opcodes.h" /* M0_BE_PD_FOM_OPCODE */
#include "stob/domain.h"     /* m0_stob_domain_create_or_init */
#include "stob/stob.h"       /* m0_stob */
#include "module/instance.h" /* m0_get */

#include <sys/mman.h>        /* mmap, madvise */

/* -------------------------------------------------------------------------- */
/* XXX									      */
/* -------------------------------------------------------------------------- */

enum m0_be_pd_module_level {
	M0_BE_PD_LEVEL_INIT,
	M0_BE_PD_LEVEL_SDOM,
	M0_BE_PD_LEVEL_IO_SCHED,
	M0_BE_PD_LEVEL_REQQ,
	M0_BE_PD_LEVEL_FOM_SERVICE,
	M0_BE_PD_LEVEL_FOM,
	M0_BE_PD_LEVEL_READY,
};

/* -------------------------------------------------------------------------- */
/* Prototypes								      */
/* -------------------------------------------------------------------------- */

M0_TL_DESCR_DEFINE(reqq,
		   "List of m0_be_pd_request-s inside m0_be_pd_request_queue",
		   static, struct m0_be_pd_request, prt_rq_link, prt_magic,
		   M0_BE_PD_REQUEST_MAGIC, M0_BE_PD_REQUEST_HEAD_MAGIC);
M0_TL_DEFINE(reqq, static, struct m0_be_pd_request);


M0_TL_DESCR_DEFINE(mappings,
		   "List of BE mappings",
		   static, struct m0_be_pd_mapping, pas_tlink, pas_magic,
		   M0_BE_PD_MAPPING_MAGIC, M0_BE_PD_MAPPING_HEAD_MAGIC);
M0_TL_DEFINE(mappings, static, struct m0_be_pd_mapping);


M0_TL_DESCR_DEFINE(pages,
		   "List of BE pages",
		   static, struct m0_be_pd_page, pp_pio_tlink, pp_magic,
		   M0_BE_PD_PAGE_MAGIC, M0_BE_PD_PAGE_HEAD_MAGIC);
M0_TL_DEFINE(pages, static, struct m0_be_pd_page);

/* XXX M0_INTERNAL, because it is used by sss/process_foms.c */
M0_TL_DESCR_DEFINE(seg, "m0_be_pd::bp_segs",
		   M0_INTERNAL, struct m0_be_seg, bs_linkage, bs_magic,
		   M0_BE_PD_SEGS_MAGIC, M0_BE_PD_SEGS_HEAD_MAGIC);
M0_TL_DEFINE(seg, static, struct m0_be_seg);

/* -------------------------------------------------------------------------- */
/* m0_be_pd							              */
/* -------------------------------------------------------------------------- */

static int be_pd_level_enter(struct m0_module *module)
{
	struct m0_be_pd            *pd = M0_AMB(pd, module, bp_module);
	enum m0_be_pd_module_level  level = module->m_cur + 1;

	switch (level) {
	case M0_BE_PD_LEVEL_INIT:
		m0_mutex_init(&pd->bp_lock);
		seg_tlist_init(&pd->bp_segs);
		mappings_tlist_init(&pd->bp_mappings);
		pd->bp_memory_size = 0;
		return M0_RC(0);
	case M0_BE_PD_LEVEL_SDOM:
		return m0_stob_domain_create_or_init(
					pd->bp_cfg.bpc_stob_domain_location,
					pd->bp_cfg.bpc_stob_domain_cfg_init,
					pd->bp_cfg.bpc_stob_domain_key,
					pd->bp_cfg.bpc_stob_domain_cfg_create,
					&pd->bp_segs_sdom);
	case M0_BE_PD_LEVEL_IO_SCHED:
		pd->bp_cfg.bpc_io_sched_cfg.bpdc_io_credit =
			M0_BE_IO_CREDIT(pd->bp_cfg.bpc_pages_per_io,
					pd->bp_cfg.bpc_page_size *
					pd->bp_cfg.bpc_pages_per_io,
					pd->bp_cfg.bpc_seg_nr_max);
		return M0_RC(m0_be_pd_io_sched_init(&pd->bp_io_sched,
					    &pd->bp_cfg.bpc_io_sched_cfg));
	case M0_BE_PD_LEVEL_REQQ:
		return m0_be_pd_request_queue_init(&pd->bp_reqq,
			pd->bp_cfg.bpc_io_sched_cfg.bpdc_sched.bisc_pos_start);
	case M0_BE_PD_LEVEL_FOM_SERVICE:
		return m0_be_pd_service_init(&pd->bp_fom_service,
					     pd->bp_cfg.bpc_reqh);
	case M0_BE_PD_LEVEL_FOM:
		m0_be_pd_fom_init(&pd->bp_fom, pd, pd->bp_cfg.bpc_reqh);
		return m0_be_pd_fom_start(&pd->bp_fom);
	case M0_BE_PD_LEVEL_READY:
		return M0_RC(0);
	}
	return M0_ERR(-ENOSYS);
}

static void be_pd_level_leave(struct m0_module *module)
{
	struct m0_be_pd            *pd = M0_AMB(pd, module, bp_module);
	enum m0_be_pd_module_level  level = module->m_cur;

	switch (level) {
	case M0_BE_PD_LEVEL_INIT:
		M0_ASSERT_INFO(pd->bp_memory_size == 0,
		               "bp_memory_size=%"PRIu64, pd->bp_memory_size);
		mappings_tlist_fini(&pd->bp_mappings);
		seg_tlist_fini(&pd->bp_segs);
		m0_mutex_fini(&pd->bp_lock);
		return;
	case M0_BE_PD_LEVEL_SDOM:
		m0_stob_domain_fini(pd->bp_segs_sdom);
		return;
	case M0_BE_PD_LEVEL_IO_SCHED:
		m0_be_pd_io_sched_fini(&pd->bp_io_sched);
		return;
	case M0_BE_PD_LEVEL_REQQ:
		m0_be_pd_request_queue_fini(&pd->bp_reqq);
		return;
	case M0_BE_PD_LEVEL_FOM_SERVICE:
		m0_be_pd_service_fini(pd->bp_fom_service);
		return;
	case M0_BE_PD_LEVEL_FOM:
		m0_be_pd_fom_stop(&pd->bp_fom);
		m0_be_pd_fom_fini(&pd->bp_fom);
		return;
	case M0_BE_PD_LEVEL_READY:
		return;
	}
}

#define BE_PD_LEVEL(level) [level] = {          \
		.ml_name  = #level,             \
		.ml_enter = be_pd_level_enter,  \
		.ml_leave = be_pd_level_leave,  \
	}
static const struct m0_modlev be_pd_levels[] = {
	BE_PD_LEVEL(M0_BE_PD_LEVEL_INIT),
	BE_PD_LEVEL(M0_BE_PD_LEVEL_SDOM),
	BE_PD_LEVEL(M0_BE_PD_LEVEL_IO_SCHED),
	BE_PD_LEVEL(M0_BE_PD_LEVEL_REQQ),
	BE_PD_LEVEL(M0_BE_PD_LEVEL_FOM_SERVICE),
	BE_PD_LEVEL(M0_BE_PD_LEVEL_FOM),
	BE_PD_LEVEL(M0_BE_PD_LEVEL_READY),
};
#undef BE_PD_LEVEL

M0_INTERNAL int m0_be_pd_init(struct m0_be_pd           *pd,
			      const struct m0_be_pd_cfg *pd_cfg)
{
	int rc;

	M0_PRE(pd_cfg->bpc_reqh != NULL); /* XXX catch current users */

	M0_PRE(M0_IS0(pd));
	pd->bp_cfg = *pd_cfg;
	m0_module_setup(&pd->bp_module, "m0_be_pd",
			be_pd_levels, ARRAY_SIZE(be_pd_levels), m0_get());
	rc = m0_module_init(&pd->bp_module, M0_BE_PD_LEVEL_READY);
	if (rc != 0)
		m0_module_fini(&pd->bp_module, M0_MODLEV_NONE);
	return rc;
}

M0_INTERNAL void m0_be_pd_fini(struct m0_be_pd *pd)
{
	m0_module_fini(&pd->bp_module, M0_MODLEV_NONE);
}

M0_INTERNAL void m0_be_pd_reg_get(struct m0_be_pd      *paged,
				  const struct m0_be_reg     *reg,
				  struct m0_be_op *op)
	/*
				  struct m0_co_context *context,
				  struct m0_fom        *fom)
				  */
{
	struct m0_be_pd_request_pages  rpages;
	struct m0_be_pd_request       *request;

	m0_be_pd_request_pages_init(&rpages, M0_PRT_READ, NULL,
				    &M0_EXT(0, 0), reg);
	/* XXX: remove M0_ALLOC_PTR() with pool-like allocator */
	M0_ALLOC_PTR(request);
	M0_ASSERT(request != NULL);
	m0_be_pd_request_init(request, &rpages);

	/* lock is needed for m0_be_pd_pages_are_in() */
	if (false && m0_be_pd_pages_are_in(paged, request)) {

		return;
	}
	/* XXX: potential race near this point: current fom may go
	        to sleep here */
	m0_be_pd_request_push(paged, request, op);
	m0_be_op_wait(op);

	m0_be_pd_request_fini(request);
	m0_free(request);
}

/* XXX copy-paste from m0_be_pd_reg_get() */
M0_INTERNAL void m0_be_pd_reg_put(struct m0_be_pd        *paged,
				  const struct m0_be_reg *reg)
{
	struct m0_be_pd_request_pages  rpages;
	struct m0_be_pd_request       *request;

	m0_be_pd_request_pages_init(&rpages, M0_PRT_READ, NULL,
				    &M0_EXT(0, 0), reg);
	M0_ALLOC_PTR(request);
	M0_ASSERT(request != NULL);
	/* XXX init request just to iterate over all pages */
	m0_be_pd_request_init(request, &rpages);

	m0_be_pd_request_pages_forall(paged, request,
	      LAMBDA(bool, (struct m0_be_pd_page *page,
	                    struct m0_be_reg_d *rd) {
	             m0_be_pd_page_lock(page);
	             M0_CNT_DEC(page->pp_ref);
	             m0_be_pd_page_unlock(page);
	             return true;
	}));

	m0_be_pd_request_fini(request);
	m0_free(request);
}

static void be_pd_lock(struct m0_be_pd *paged)
{
	m0_mutex_lock(&paged->bp_lock);
}

static void be_pd_unlock(struct m0_be_pd *paged)
{
	m0_mutex_unlock(&paged->bp_lock);
}

static bool be_pd_is_locked(struct m0_be_pd *paged)
{
	return m0_mutex_is_locked(&paged->bp_lock);
}

static void be_pd_mem_usage_increase(struct m0_be_pd *pd,
                                     m0_bcount_t      size)
{
	be_pd_lock(pd);
	pd->bp_memory_size += size;
	if (pd->bp_memory_size > pd->bp_cfg.bpc_memory_size_max) {
		M0_LOG(M0_WARN, "memory limit exceeded: "
		       "bpc_memory_size_max=%"PRIu64" bp_memory_size=%"PRIu64,
		       pd->bp_cfg.bpc_memory_size_max,
		       pd->bp_memory_size);
	}
	be_pd_unlock(pd);
}

static void be_pd_mem_usage_decrease(struct m0_be_pd *pd,
                                     m0_bcount_t      size)
{
	m0_bcount_t size_before;

	be_pd_lock(pd);
	size_before = pd->bp_memory_size;
	pd->bp_memory_size -= size;
	M0_ASSERT(pd->bp_memory_size < size_before);
	be_pd_unlock(pd);
}

/* -------------------------------------------------------------------------- */
/* Segments							              */
/* -------------------------------------------------------------------------- */

static void be_pd_seg_stob_id(struct m0_be_pd   *pd,
			      uint64_t           stob_key,
			      struct m0_stob_id *out)
{
	m0_stob_id_make(0, stob_key,
			m0_stob_domain_id_get(pd->bp_segs_sdom), out);
}

static int be_pd_seg_stob_open(struct m0_be_pd  *pd,
			       uint64_t          stob_key,
			       const char       *stob_create_cfg,
			       struct m0_stob  **out,
			       bool              create)
{
	int               rc;
	struct m0_stob_id stob_id;

	M0_ENTRY("stob_key=%"PRIu64" create=%d", stob_key, (int)create);

	be_pd_seg_stob_id(pd, stob_key, &stob_id);
	rc = m0_stob_find(&stob_id, out);
	if (rc == 0) {
		rc = m0_stob_state_get(*out) == CSS_UNKNOWN ?
		     m0_stob_locate(*out) : 0;
		/*
		 * XXX Comment below lines, because be-ut don't clean segments
		 * and following tests fail with EEXIST.
		 * TODO Make destroy mechanism for be_domain and uncomment these
		 * lines.
		 */
		/*
		rc = rc ?: create && m0_stob_state_get(*out) == CSS_EXISTS ?
		     M0_ERR(-EEXIST) : 0;
		*/
		rc = rc ?: create && m0_stob_state_get(*out) == CSS_NOENT ?
		     m0_stob_create(*out, NULL, stob_create_cfg) : 0;
		rc = rc ?: m0_stob_state_get(*out) == CSS_EXISTS ? 0 : -ENOENT;
		if (rc != 0)
			m0_stob_put(*out);
	}
	M0_POST(ergo(rc == 0, m0_stob_state_get(*out) == CSS_EXISTS));
	return M0_RC(rc);
}

M0_INTERNAL int m0_be_pd_seg_create(struct m0_be_pd                  *pd,
				    /* m0_be_seg_init() requires be domain */
				    struct m0_be_domain              *dom,
				    const struct m0_be_0type_seg_cfg *seg_cfg)
{
	struct m0_be_seg *seg;
	struct m0_stob   *stob;
	int               rc;
	int               rc1;

	/* seg_cfg->bsc_preallocate is ignored here. See be/domain.c. */

	M0_ALLOC_PTR(seg);
	if (seg == NULL)
		return M0_ERR(-ENOMEM);

	rc = be_pd_seg_stob_open(pd, seg_cfg->bsc_stob_key,
				 seg_cfg->bsc_stob_create_cfg, &stob, true);
	if (rc == 0) {
		m0_be_seg_init(seg, stob, dom, pd, M0_BE_SEG_FAKE_ID);
		rc = m0_be_seg_create(seg, seg_cfg->bsc_size,
				      seg_cfg->bsc_addr);
		m0_be_seg_fini(seg);
		if (rc == 0)
			m0_stob_put(stob);
		else {
			rc1 = m0_stob_destroy(stob, NULL);
			if (rc1 != 0)
				M0_LOG(M0_ERROR, "Cannot destroy stob after "
						 "creation, rc1=%d", rc1);
		}
	}
	m0_free(seg);

	return M0_RC(rc);
}

/* Opens segment without adding to bp_segs list. */
static int be_pd_seg_open(struct m0_be_pd     *pd,
			  struct m0_be_seg    *seg,
			  struct m0_be_domain *dom,
			  uint64_t             stob_key)
{
	struct m0_stob *stob;
	int             rc;

	rc = be_pd_seg_stob_open(pd, stob_key, NULL, &stob, false);
	if (rc == 0) {
		m0_be_seg_init(seg, stob, dom, pd, M0_BE_SEG_FAKE_ID);
		m0_stob_put(stob);
		rc = m0_be_seg_open(seg);
		if (rc != 0)
			m0_be_seg_fini(seg);
	}
	return M0_RC(rc);
}

M0_INTERNAL int m0_be_pd_seg_open(struct m0_be_pd     *pd,
				  struct m0_be_seg    *seg,
				  /* m0_be_seg_init() requires be domain */
				  struct m0_be_domain *dom,
				  uint64_t             stob_key)
{
	int rc;

	rc = be_pd_seg_open(pd, seg, dom, stob_key);
	if (rc == 0) {
		be_pd_lock(pd);
		seg_tlink_init_at_tail(seg, &pd->bp_segs);
		be_pd_unlock(pd);
	}
	return M0_RC(rc);
}

/* Closes (or destroys) segment without removing from bp_segs list. */
static int be_pd_seg_close(struct m0_be_pd  *pd,
			   struct m0_be_seg *seg,
			   bool              destroy)
{
	int rc;

	m0_be_seg_close(seg);
	rc = destroy ? m0_be_seg_destroy(seg) : 0;
	m0_be_seg_fini(seg);

	return M0_RC(rc);
}

M0_INTERNAL void m0_be_pd_seg_close(struct m0_be_pd  *pd,
				    struct m0_be_seg *seg)
{
	int rc;

	be_pd_lock(pd);
	seg_tlink_del_fini(seg);
	be_pd_unlock(pd);
	rc = be_pd_seg_close(pd, seg, false);
	M0_ASSERT(rc == 0);
}

M0_INTERNAL int m0_be_pd_seg_destroy(struct m0_be_pd     *pd,
				     struct m0_be_domain *dom,
				     uint64_t             seg_id)
{
	struct m0_be_seg *seg;
	int               rc;

	M0_ALLOC_PTR(seg);
	if (seg == NULL)
		return M0_ERR(-ENOMEM);

	/* Open and destroy segment bypassing the bp_segs list. */
	rc = be_pd_seg_open(pd, seg, dom, seg_id);
	rc = rc ?: be_pd_seg_close(pd, seg, true);
	m0_free(seg);

	return M0_RC(rc);
}

M0_INTERNAL struct m0_be_seg *m0_be_pd_seg_by_addr(struct m0_be_pd *pd,
						   const void      *addr)
{
	struct m0_be_seg *seg;

	be_pd_lock(pd);
	seg = m0_tl_find(seg, seg, &pd->bp_segs, m0_be_seg_contains(seg, addr));
	be_pd_unlock(pd);

	return seg;
}

M0_INTERNAL struct m0_be_seg *m0_be_pd_seg_by_id(struct m0_be_pd *pd,
						 uint64_t         id)
{
	struct m0_be_seg *seg;

	be_pd_lock(pd);
	seg = m0_tl_find(seg, seg, &pd->bp_segs, seg->bs_id == id);
	be_pd_unlock(pd);

	return seg;
}

M0_INTERNAL struct m0_be_seg *m0_be_pd_seg_first(struct m0_be_pd *pd)
{
	struct m0_be_seg *seg;

	be_pd_lock(pd);
	seg = seg_tlist_head(&pd->bp_segs);
	be_pd_unlock(pd);

	return seg;
}

M0_INTERNAL struct m0_be_seg *m0_be_pd_seg_next(struct m0_be_pd        *pd,
						const struct m0_be_seg *seg)
{
	struct m0_be_seg *next;

	be_pd_lock(pd);
	next = seg_tlist_next(&pd->bp_segs, seg);
	be_pd_unlock(pd);

	return next;
}

M0_INTERNAL bool m0_be_pd_is_stob_seg(struct m0_be_pd         *pd,
                                      const struct m0_stob_id *stob_id)
{
	bool result;

	be_pd_lock(pd);
	result = m0_tl_exists(seg, seg, &pd->bp_segs,
			      m0_be_seg_contains_stob(seg, stob_id));
	be_pd_unlock(pd);

	return result;
}

/* -------------------------------------------------------------------------- */
/* PD FOM							              */
/* -------------------------------------------------------------------------- */

/**
 * Phases of PD fom.
 * @verbatim
 *                                INIT
 *                                 |
 *                 m0_be_pd_init() |
 *				   |
 *                 m0_be_pd_fini() v
 *         FINI <---------------- IDLE <---------------------------------+
 *                                 |                                     |
 *                 m0_be_reg_get() |					 |
 *         m0_be_pd_request_push() |					 |
 *                                 v					 |
 *                       +----MANAGE_PRE -------+			 |
 *  request is PRT_READ  |                      | request is PRT_WRITE	 |
 *                       |                      |			 |
 *                       v                      v			 |
 *                     READ                  WRITE_PRE			 |
 *                       |                      |			 |
 *                       |                      v			 |
 *			 |		     WRITE_POST			 |
 *    request.op.channel |           have more ^ | request.op.channel	 |
 *             signalled |         armed pages | | signalled		 |
 *                       v                     | v			 |
 *                     READ                   WRITE			 |
 *                     DONE                    DONE			 |
 *                       |                      |			 |
 *                       +-------->+<-----------+			 |
 *                                 |					 |
 *                                 v					 |
 *			       MANAGE_POST ------------------------------+
 * @endverbatim
 */
enum m0_be_pd_fom_state {
	PFS_INIT   = M0_FOM_PHASE_INIT,
	PFS_FINISH = M0_FOM_PHASE_FINISH,

	PFS_IDLE   = M0_FOM_PHASE_NR,
	PFS_READ,
	PFS_READ_DONE,
	PFS_WRITE_PRE,
	PFS_WRITE_POST,
	PFS_WRITE_DONE,
	PFS_MANAGE_PRE,
	PFS_MANAGE_POST,
	PFS_FAILED,
	PFS_NR,
};

static struct m0_sm_state_descr pd_fom_states[PFS_NR] = {
#define _S(name, flags, allowed)      \
	[name] = {                    \
		.sd_flags   = flags,  \
		.sd_name    = #name,  \
		.sd_allowed = allowed \
	}

	_S(PFS_INIT,   M0_SDF_INITIAL, M0_BITS(PFS_IDLE, PFS_FAILED)),
	_S(PFS_FINISH, M0_SDF_TERMINAL, 0),
	_S(PFS_FAILED, M0_SDF_FAILURE, M0_BITS(PFS_FINISH)),

	_S(PFS_IDLE,        0, M0_BITS(PFS_MANAGE_PRE, PFS_FINISH)),
	_S(PFS_MANAGE_PRE,  0, M0_BITS(PFS_WRITE_PRE, PFS_READ)),
	_S(PFS_WRITE_PRE,   0, M0_BITS(PFS_WRITE_POST)),
	_S(PFS_WRITE_POST,  0, M0_BITS(PFS_WRITE_DONE, PFS_MANAGE_POST)),
	_S(PFS_READ,        0, M0_BITS(PFS_READ_DONE, PFS_MANAGE_POST)),
	_S(PFS_WRITE_DONE,  0, M0_BITS(PFS_MANAGE_POST, PFS_WRITE_POST)),
	_S(PFS_READ_DONE,   0, M0_BITS(PFS_MANAGE_POST)),
	_S(PFS_MANAGE_POST, 0, M0_BITS(PFS_IDLE)),

#undef _S
};

const static struct m0_sm_conf pd_fom_conf = {
	.scf_name      = "pd_fom",
	.scf_nr_states = ARRAY_SIZE(pd_fom_states),
	.scf_state     = pd_fom_states,
};

static struct m0_be_pd_fom *fom2be_pd_fom(struct m0_fom *fom)
{
	/* XXX bob_of */
	return container_of(fom, struct m0_be_pd_fom, bpf_gen);
}

static int pd_fom_tick(struct m0_fom *fom)
{
	enum m0_be_pd_fom_state        phase    = m0_fom_phase(fom);
	struct m0_be_pd_fom           *pd_fom    = fom2be_pd_fom(fom);
	struct m0_be_pd               *paged     = pd_fom->bpf_pd;
	struct m0_be_pd_cfg           *cfg       = &paged->bp_cfg;
	struct m0_be_pd_io_sched      *pios      = &paged->bp_io_sched;
	struct m0_be_pd_request_queue *queue     = &paged->bp_reqq;
	struct m0_be_pd_request       *request   = pd_fom->bpf_cur_request;
	struct m0_be_pd_io            *pio       = pd_fom->bpf_cur_pio;
	struct m0_tl                  *pio_armed = &pd_fom->bpf_cur_pio_armed;
	struct m0_tl                  *pio_done  = &pd_fom->bpf_cur_pio_done;
	struct m0_be_pd_page          *pio_page;
	unsigned                       pio_nr;
	unsigned                       pio_nr_max = cfg->bpc_pages_per_io;
	struct m0_be_pd_page          *current_iterated_in_write_case; /* XXX */
	struct m0_be_seg              *seg;
	unsigned                       pages_nr;
	int rc;
	int rc1;

	M0_ENTRY("pd_fom=%p paged=%p phase=%d", pd_fom, paged, phase);

	switch(phase) {
	case PFS_INIT:
		m0_be_op_init(&pd_fom->bpf_op);
		pages_tlist_init(pio_armed);
		pages_tlist_init(pio_done);
		/* pd_fom->bpf_pio_ext = 0ULL; */
		/* XXX: MAX, look here */
		pd_fom->bpf_pio_ext = pios->bpd_sched.bis_pos;
		rc = M0_FSO_AGAIN;
		m0_fom_phase_move(fom, 0, PFS_IDLE);
		break;
	case PFS_FINISH:
		pages_tlist_fini(pio_done);
		pages_tlist_fini(pio_armed);
		m0_be_op_fini(&pd_fom->bpf_op);
		rc = M0_FSO_WAIT;
		break;
	case PFS_IDLE:
		request = m0_be_pd_request_queue_pop(queue);
		if (request == NULL) {
			rc = M0_FSO_WAIT;
			break;
		} else {
			if (request->prt_pages.prp_type == M0_PRT_STOP) {
				m0_fom_phase_move(fom, 0, PFS_FINISH);
				m0_be_op_done(request->prt_op);
				rc = M0_FSO_WAIT;
			} else {
				M0_ASSERT(M0_IN(request->prt_pages.prp_type,
						(M0_PRT_READ, M0_PRT_WRITE)));
				pd_fom->bpf_cur_request = request;
				m0_fom_phase_move(fom, 0, PFS_MANAGE_PRE);
				rc = M0_FSO_AGAIN;
			}
		}
		break;
	case PFS_MANAGE_PRE:
		/* XXX use .._is_read()/...is_write() */
		m0_fom_phase_move(fom, 0,
		                  request->prt_pages.prp_type == M0_PRT_READ ?
		                  PFS_READ : PFS_WRITE_PRE);
		rc = M0_FSO_AGAIN;
		break;
	case PFS_MANAGE_POST:
		m0_fom_phase_move(fom, 0, PFS_IDLE);
		rc = M0_FSO_AGAIN;
		break;
	case PFS_READ:
		M0_ASSERT(request != NULL);
		M0_BE_OP_SYNC(op,
			      m0_be_pd_io_get(pios, &pd_fom->bpf_cur_pio, &op));
		pio = pd_fom->bpf_cur_pio;
		pages_nr = 0;

		m0_be_pd_request_pages_forall(paged, request,
		      LAMBDA(bool, (struct m0_be_pd_page *page,
				    struct m0_be_reg_d *rd) {
			m0_be_pd_page_lock(page);
			M0_CNT_INC(page->pp_ref);
			if (!m0_be_pd_page_is_in(paged, page)) {
				/*
				 * XXX we need access to the mapping
				 * from segment
				 */
				/* XXX The actual allocation is here */
				rc1 = m0_be_pd_mapping_page_attach(
				    m0_be_pd__mapping_by_addr(paged,
				                        page->pp_addr), page);
				M0_ASSERT(rc1 == 0);
			}
			if (page->pp_state == M0_PPS_MAPPED) {
				seg = rd->rd_reg.br_seg;
				/* recovery case */
				if (seg == NULL) {
					seg = m0_be_pd_seg_by_addr(paged,
						   rd->rd_reg.br_addr);
				}
				m0_be_io_add(m0_be_pd_io_be_io(pio),
					     seg->bs_stob,
			     /* XXX: here should be cellar page, still for the
			      * sake of debugging I've chaneged this place */
					     /* page->pp_cellar, */
					     page->pp_addr,
					     m0_be_seg_offset(seg,
							      page->pp_addr),
					     page->pp_size);
				page->pp_state = M0_PPS_READING;
				++pages_nr;
			}
			m0_be_pd_page_unlock(page);

			return true;
			     }));

		if (pages_nr == 0) {
			m0_fom_phase_move(fom, 0, PFS_READ_DONE);
			rc = M0_FSO_AGAIN;
		} else {
			m0_be_op_reset(&pd_fom->bpf_op);

			/* XXX: check out the following:
			 *  - request->prt_pages.prp_ext or request.ext
			 *  - active &pd_fom->bpf_op or not?
			 */
			m0_be_io_configure(m0_be_pd_io_be_io(pio), SIO_READ);
			m0_be_pd_io_add(pios, pio,
			/* XXX: ext is expected to be passed.  for now it's OK
			   to skip.  But you know, this is just for NOW! */
					/* &request->prt_pages.prp_ext, */
					NULL,
					&pd_fom->bpf_op);

			rc = m0_be_op_tick_ret(&pd_fom->bpf_op, fom,
					       PFS_READ_DONE);
		}
		break;

	case PFS_READ_DONE:
		m0_be_pd_request_pages_forall(paged, request,
		      LAMBDA(bool, (struct m0_be_pd_page *page,
				    struct m0_be_reg_d *rd) {
			m0_be_pd_page_lock(page);

			M0_ASSERT(M0_IN(page->pp_state, (M0_PPS_READING,
							 M0_PPS_READY)));
			page->pp_state = M0_PPS_READY;
			/* XXX move page to ready state IF it is in pio */
			m0_be_pd_page_unlock(page);

			return true;
		     }));

		m0_be_pd_io_put(pios, pio);
		m0_be_op_done(request->prt_op);

		m0_fom_phase_set(fom, PFS_MANAGE_POST);
		rc = M0_FSO_AGAIN;
		break;

	case PFS_WRITE_PRE:
		M0_PRE(pages_tlist_length(pio_armed) == 0);

		/**
		 *  NOTE: Looks like parallel READ requests do not coexist
		 *  with parallel WRITEs. Threrefore READs may not take lock on
		 *  the whole mapping.
		 */
		//NOTE: m0_be_pd_mappings_lock(paged, request);

		current_iterated_in_write_case = NULL;
		m0_be_pd_request_pages_forall(paged, request,
		      LAMBDA(bool, (struct m0_be_pd_page *page,
				    struct m0_be_reg_d *rd) {

		        if (current_iterated_in_write_case == page)
			        return true;

			current_iterated_in_write_case = page;

			m0_be_pd_page_lock(page);
			M0_ASSERT(page->pp_state == M0_PPS_READY);
			page->pp_state = M0_PPS_WRITING;
			m0_be_pd_page_unlock(page);

			pages_tlist_add_tail(pio_armed, page);

			return true;
		     }));

		//NOTE: m0_be_pd_mappings_unlock(paged, request);

		m0_be_pd_request__copy_to_cellars(paged, request);

		m0_fom_phase_set(fom, PFS_WRITE_POST);
		rc = M0_FSO_AGAIN;

	case PFS_WRITE_POST:
		M0_PRE(pages_tlist_length(pio_done) == 0);

		M0_BE_OP_SYNC(op,
			      m0_be_pd_io_get(pios, &pd_fom->bpf_cur_pio, &op));
		pio = pd_fom->bpf_cur_pio;

		for (pio_nr = 0; pio_nr < pio_nr_max; ++pio_nr) {
			pio_page = pages_tlist_head(pio_armed);
			if (pio_page == NULL)
				break;
			pages_tlist_move_tail(pio_done, pio_page);

			m0_be_pd_page_lock(pio_page);
			M0_ASSERT(pio_page->pp_state == M0_PPS_WRITING);
			m0_be_pd_page_unlock(pio_page);

			seg = m0_be_pd__page_to_seg(paged, pio_page);
			m0_be_io_add(m0_be_pd_io_be_io(pio),
				     seg->bs_stob, pio_page->pp_cellar,
				     m0_be_seg_offset(seg, pio_page->pp_addr),
				     pio_page->pp_size);
		}

		m0_be_op_reset(&pd_fom->bpf_op);
		m0_be_io_configure(m0_be_pd_io_be_io(pio), SIO_WRITE);
		m0_be_pd_io_add(pios, pio, /* &request->prt_pages.prp_ext, */
				&M0_EXT(pd_fom->bpf_pio_ext,
					pd_fom->bpf_pio_ext + 1),
				&pd_fom->bpf_op);
		pd_fom->bpf_pio_ext++;

		rc = m0_be_op_tick_ret(&pd_fom->bpf_op, fom, PFS_WRITE_DONE);
		break;

	case PFS_WRITE_DONE:
		while ((pio_page = pages_tlist_pop(pio_done)) != NULL) {
			m0_be_pd_page_lock(pio_page);
			M0_ASSERT(pio_page->pp_state == M0_PPS_WRITING);
			pio_page->pp_state = M0_PPS_READY;
			m0_be_pd_page_unlock(pio_page);
		}

		m0_be_pd_io_put(pios, pio);

		if (pages_tlist_length(pio_armed) == 0) {
			m0_be_op_done(request->prt_op);
			m0_fom_phase_set(fom, PFS_MANAGE_POST);
		} else {
			m0_fom_phase_set(fom, PFS_WRITE_POST);
		}
		rc = M0_FSO_AGAIN;
		break;
		/* Possible state for page management
		  case PFS_MANAGE:
		  for (mapping in mappings) {
		    for (page in mapping->pages) {
		      if (page->cnt == 0)
		      m0_be_pd_mapping_page_detach(mapping, page);
		    }
		  }
		*/

	default:
		M0_IMPOSSIBLE("XXX");
	}
	return M0_RC(rc);
}

M0_UNUSED static struct m0_be_pd_fom *fom_to_pd_fom(const struct m0_fom *fom)
{
	return container_of(fom, struct m0_be_pd_fom, bpf_gen);
}

static void pd_fom_fini(struct m0_fom *fom)
{
	m0_fom_fini(fom);
}

static size_t pd_fom_locality(const struct m0_fom *fom)
{
	return 0;
}

static const struct m0_fom_ops pd_fom_ops = {
	.fo_fini          = pd_fom_fini,
	.fo_tick          = pd_fom_tick,
	.fo_home_locality = pd_fom_locality
};

static struct m0_fom_type pd_fom_type;

static const struct m0_fom_type_ops pd_fom_type_ops = {
	.fto_create = NULL
};


/* TODO fix the function name (namespaces are missing) */
static void pd_reqq_push(struct m0_sm_group *_, struct m0_sm_ast *ast)
{
	/* TODO fix m0_be_pd_fom name here */
	struct m0_be_pd_fom *m   = M0_AMB(m, ast, bpf_ast_reqq_push);
	struct m0_fom       *fom = &m->bpf_gen;

	M0_ENTRY();
	m0_mutex_lock(&m->bpf_ast_post_lock);
	if (m0_fom_is_waiting(fom) && m0_fom_phase(fom) == PFS_IDLE) {
		M0_LOG(M0_DEBUG, "waking up");
		m0_fom_ready(fom);
	}
	m->bpf_ast_posted = false;
	m0_mutex_unlock(&m->bpf_ast_post_lock);
	M0_LEAVE();
}

static void pd_fom_on_reqq_push_wakeup(struct m0_be_pd_fom *pd_fom)
{
	m0_mutex_lock(&pd_fom->bpf_ast_post_lock);
	if (!pd_fom->bpf_ast_posted) {
		m0_sm_ast_post(&pd_fom->bpf_gen.fo_loc->fl_group,
		               &pd_fom->bpf_ast_reqq_push);
		pd_fom->bpf_ast_posted = true;
	}
	m0_mutex_unlock(&pd_fom->bpf_ast_post_lock);
}

/* TODO make a good name for struct m0_be_pd_fom *, not just fom */
M0_INTERNAL void m0_be_pd_fom_init(struct m0_be_pd_fom    *fom,
				   struct m0_be_pd        *pd,
				   struct m0_reqh         *reqh)
{
	m0_fom_init(&fom->bpf_gen, &pd_fom_type, &pd_fom_ops, NULL, NULL, reqh);
	fom->bpf_pd = pd;
	m0_mutex_init(&fom->bpf_ast_post_lock);
	fom->bpf_ast_posted = false;
	fom->bpf_ast_reqq_push = (struct m0_sm_ast){ .sa_cb = pd_reqq_push };
}

M0_INTERNAL void m0_be_pd_fom_fini(struct m0_be_pd_fom *fom)
{
	m0_mutex_fini(&fom->bpf_ast_post_lock);
	/* fom->bpf_gen is finalised in pd_fom_fini() */
}

M0_INTERNAL int m0_be_pd_fom_start(struct m0_be_pd_fom *fom)
{
	m0_fom_queue(&fom->bpf_gen);

	return 0;
}

M0_INTERNAL void m0_be_pd_fom_stop(struct m0_be_pd_fom *fom)
{
	struct m0_be_pd_request_pages  rpages;
	struct m0_be_pd_request        request;
	struct m0_be_pd               *paged = fom->bpf_pd;

	m0_be_pd_request_pages_init(&rpages, M0_PRT_STOP, NULL,
				    &M0_EXT(0, 0), &M0_BE_REG(NULL, 0, NULL));
	m0_be_pd_request_init(&request, &rpages);
	M0_BE_OP_SYNC(op, m0_be_pd_request_push(paged, &request, &op));
	m0_be_pd_request_fini(&request);
}

M0_INTERNAL void m0_be_pd_fom_mod_init(void)
{
	m0_fom_type_init(&pd_fom_type, M0_BE_PD_FOM_OPCODE, &pd_fom_type_ops,
			 &m0_be_pds_stype, &pd_fom_conf);
}

M0_INTERNAL void m0_be_pd_fom_mod_fini(void)
{
}

/* -------------------------------------------------------------------------- */
/* Request queue							      */
/* -------------------------------------------------------------------------- */

M0_INTERNAL int m0_be_pd_request_queue_init(struct m0_be_pd_request_queue *rq,
					    m0_bcount_t pos_start)
{
	rq->prq_current_pos = pos_start;
	reqq_tlist_init(&rq->prq_deferred);

	reqq_tlist_init(&rq->prq_queue);
	m0_mutex_init(&rq->prq_lock);

	return 0;
}

M0_INTERNAL void m0_be_pd_request_queue_fini(struct m0_be_pd_request_queue *rq)
{
	m0_mutex_fini(&rq->prq_lock);
	reqq_tlist_fini(&rq->prq_queue);
	reqq_tlist_fini(&rq->prq_deferred);
}

M0_INTERNAL struct m0_be_pd_request *
m0_be_pd_request_queue_pop(struct m0_be_pd_request_queue *rq)
{
	struct m0_be_pd_request *request;

	M0_ENTRY();

	m0_mutex_lock(&rq->prq_lock);
	request = reqq_tlist_pop(&rq->prq_queue);
	m0_mutex_unlock(&rq->prq_lock);

	M0_LEAVE("request=%p", request);

	return request;
}

static bool be_pd_request_is_deferred(struct m0_be_pd_request_queue *rq,
				      struct m0_be_pd_request       *request)
{
	M0_ENTRY("is_deferred=%d cur=%" PRIu64 " start=%" PRIu64,
		 !!rq->prq_current_pos != request->prt_ext.e_start,
		 rq->prq_current_pos, request->prt_ext.e_start);

	return rq->prq_current_pos != request->prt_ext.e_start;
}

static bool be_pd_request_is_write(struct m0_be_pd_request *request)
{
	M0_ENTRY("is_write=%d", !!request->prt_pages.prp_type == M0_PRT_WRITE);

	return request->prt_pages.prp_type == M0_PRT_WRITE;
}

static void deferred_insert_sorted(struct m0_be_pd_request_queue *rq,
				   struct m0_be_pd_request       *request)
{
	struct m0_be_pd_request *rq_prev;

	M0_ENTRY("request=%p", request);

	M0_PRE(m0_mutex_is_locked(&rq->prq_lock));

	for (rq_prev = reqq_tlist_tail(&rq->prq_deferred);
	     rq_prev != NULL;
	     rq_prev = reqq_tlist_prev(&rq->prq_deferred, rq_prev)) {
		if (rq_prev->prt_ext.e_end <= request->prt_ext.e_start)
			break;
	}

	if (rq_prev == NULL)
		reqq_tlist_add(&rq->prq_deferred, request);
	else
		reqq_tlist_add_after(rq_prev, request);
}

static void request_queue_update_pos(struct m0_be_pd_request_queue *rq,
				     struct m0_be_pd_request       *request)
{
	M0_PRE(rq->prq_current_pos == request->prt_ext.e_start);

	M0_ENTRY("request=%p", request);

	/* XXX: should we increment count compared in another place with e_start ??? */
	/* rq->prq_current_pos = request->prt_ext.e_end + 1; */

	rq->prq_current_pos = request->prt_ext.e_end;

	M0_LEAVE("request=%p current_pos=%" PRIu64, request,
		 rq->prq_current_pos);
}

M0_INTERNAL void
m0_be_pd_request_queue_push(struct m0_be_pd_request_queue      *rq,
			    struct m0_be_pd_request            *request,
			    struct m0_fom                      *fom)
{
	M0_ENTRY("request=%p", request);

	/* XXX catch users that don't init request->prt_ext */
	M0_PRE(ergo(be_pd_request_is_write(request),
		    request->prt_ext.e_end != 0));

	M0_LOG(M0_DEBUG, "request=%p start=%" PRIu64 " end=%" PRIu64, request,
	       request->prt_ext.e_start, request->prt_ext.e_end);

	m0_mutex_lock(&rq->prq_lock);
	/* Use deferred list to push WRITE requests in required order. */
	if (be_pd_request_is_write(request)) {
		if (be_pd_request_is_deferred(rq, request))
			deferred_insert_sorted(rq, request);
		else {
			while (request != NULL &&
			       !be_pd_request_is_deferred(rq, request)) {
				reqq_tlist_add_tail(&rq->prq_queue, request);
				request_queue_update_pos(rq, request);
				request = reqq_tlist_pop(&rq->prq_deferred);
			}
			if (request != NULL)
				reqq_tlist_add(&rq->prq_deferred, request);
		}
	} else
		reqq_tlist_add_tail(&rq->prq_queue, request);
	m0_mutex_unlock(&rq->prq_lock);

	M0_LEAVE();
}

M0_INTERNAL void m0_be_pd_request_push(struct m0_be_pd         *paged,
				       struct m0_be_pd_request *request,
				       struct m0_be_op         *op)
{
	request->prt_op = op;
	m0_be_op_active(request->prt_op);
	m0_be_pd_request_queue_push(&paged->bp_reqq, request,
				    &paged->bp_fom.bpf_gen);
	pd_fom_on_reqq_push_wakeup(&paged->bp_fom);
}

/* -------------------------------------------------------------------------- */
/* Requests							              */
/* -------------------------------------------------------------------------- */

M0_INTERNAL bool request_invariant(struct m0_be_pd_request *request)
{
	return true;
}

M0_INTERNAL void m0_be_pd_request_init(struct m0_be_pd_request       *request,
				       struct m0_be_pd_request_pages *pages)
{
	reqq_tlink_init(request);
	request->prt_pages = *pages;
	request->prt_ext = pages->prp_ext; /* XXX: remove prt_ext use prp_ext? */
}

M0_INTERNAL void m0_be_pd_request_fini(struct m0_be_pd_request *request)
{
	reqq_tlink_fini(request);
}

M0_INTERNAL void m0_be_pd_request_pages_init(struct m0_be_pd_request_pages *rqp,
					     enum m0_be_pd_request_type type,
					     struct m0_be_reg_area     *rarea,
					     struct m0_ext             *ext,
					     const struct m0_be_reg    *reg)
{

	*rqp = (struct m0_be_pd_request_pages) {
		.prp_type = type,
		.prp_reg_area = rarea,
		.prp_ext = *ext,
		.prp_reg = reg == NULL ? M0_BE_REG(NULL, 0, NULL) : *reg,
	};
}

static void copy_reg_to_cellar(struct m0_be_pd_page       *page,
			       const struct m0_be_reg_d   *rd)
{
/**
 * This function translates regions pointing into segment's address space
 * into cellar pages addresses. XXX: CAN BE TRICKY OR/AND BUGGY. ENJOY.
 *
 *      +---------------REG_v3----------------+
 *      +---------REG_v2-----------+          |
 *      +---REG_v1---+             |          |
 *      |            |             |          |
 *   |  V            V |           V     |    V            |
 *   o--.----P1------.-o-------P2--.-----o----.--P3--------o
 *   |                 |                 |                 |
 *                   |------------------------|
 *   1)
 *   |---------------|     - 0off
 *                   |-|   - rest
 *
 *   2)
 *                   |-|   - 0off (negative)
 *                     |-----------------|    - size (page-size)
 *
 *   3)
 *                   |-------------------|    - 0off (negative)
 *
 */
	void        *addr;
	void        *start;
	m0_bcount_t  size;
	m0_bcount_t  rest;
	ptrdiff_t    addr_0off;

	M0_PRE(page->pp_cellar != NULL &&
	       page->pp_state == M0_PPS_WRITING &&
	       m0_be_pd_page_is_locked(page));

	addr_0off = rd->rd_reg.br_addr - page->pp_addr;
	if (addr_0off < 0) {
		addr = page->pp_cellar;
		M0_ASSERT(rd->rd_reg.br_size + addr_0off > 0);
		size = min_check(page->pp_size,
				 rd->rd_reg.br_size + addr_0off);
		start = rd->rd_buf - addr_0off;
	} else {
		addr = page->pp_cellar + addr_0off;
		M0_ASSERT(page->pp_size > addr_0off);
		rest = page->pp_size - addr_0off;
		size = min_check(rest, rd->rd_reg.br_size);
		start = rd->rd_buf;
	}

	memcpy(addr, start, size);
}

M0_INTERNAL void
m0_be_pd_request__copy_to_cellars(struct m0_be_pd         *paged,
				  struct m0_be_pd_request *request)
{
	m0_be_pd_request_pages_forall(paged, request,
	      LAMBDA(bool, (struct m0_be_pd_page *page,
			    struct m0_be_reg_d *rd) {
			     m0_be_pd_page_lock(page);
			     copy_reg_to_cellar(page, rd);
			     m0_be_pd_page_unlock(page);
			     return true;
		     }));
}

M0_INTERNAL bool m0_be_pd_pages_are_in(struct m0_be_pd         *paged,
				       struct m0_be_pd_request *request)
{
	bool pages_are_in = true;

	m0_be_pd_request_pages_forall(paged, request,
	      LAMBDA(bool, (struct m0_be_pd_page *page,
			    struct m0_be_reg_d *rd) {
			     pages_are_in &= M0_IN(page->pp_state,
						   (M0_PPS_READY, M0_PPS_WRITING));
			     return true;
		     }));

	return pages_are_in;
}

static bool
request_pages_forall_helper(struct m0_be_pd         *paged,
			    struct m0_be_pd_request *request,
			    struct m0_be_reg_d      *rd,
			    bool (*iterate)(struct m0_be_pd_page *page,
					    struct m0_be_reg_d   *rd))
{
	struct m0_be_pd_request_pages *rpages = &request->prt_pages;
	struct m0_be_prp_cursor        cursor;
	struct m0_be_pd_page          *page;

	m0_be_prp_cursor_init(&cursor, paged, rpages,
			      rd->rd_reg.br_addr,
			      rd->rd_reg.br_size);
	while (m0_be_prp_cursor_next(&cursor)) {
		page = m0_be_prp_cursor_page_get(&cursor);
		if (!iterate(page, rd))
			return false;
	}
	m0_be_prp_cursor_fini(&cursor);

	return true;
}

M0_INTERNAL void
m0_be_pd_request_pages_forall(struct m0_be_pd         *paged,
			      struct m0_be_pd_request *request,
			      bool (*iterate)(struct m0_be_pd_page *page,
					      struct m0_be_reg_d   *rd))
{
	struct m0_be_pd_request_pages *rpages = &request->prt_pages;
	struct m0_be_reg_d            *rd;
	struct m0_be_reg_d             rd_read;

	if (rpages->prp_type == M0_PRT_READ) {
		rd = &rd_read;
		rd->rd_reg = rpages->prp_reg;
		request_pages_forall_helper(paged, request, rd, iterate);
		return;
	}

	M0_BE_REG_AREA_FORALL(rpages->prp_reg_area, rd) {
		if (!request_pages_forall_helper(paged, request, rd, iterate))
			return;
	}
}

/* -------------------------------------------------------------------------- */
/* Request cursors						              */
/* -------------------------------------------------------------------------- */

M0_INTERNAL void m0_be_prp_cursor_init(struct m0_be_prp_cursor       *cursor,
				       struct m0_be_pd               *paged,
				       struct m0_be_pd_request_pages *pages,
				       const void                    *addr,
				       m0_bcount_t                    size)
{
	*cursor = (struct m0_be_prp_cursor) {
		.rpi_pages = pages,
		.rpi_paged = paged,
		.rpi_addr  = addr,
		.rpi_size  = size,
	};
}

M0_INTERNAL void m0_be_prp_cursor_fini(struct m0_be_prp_cursor *cursor)
{
}

M0_INTERNAL bool m0_be_prp_cursor_next(struct m0_be_prp_cursor *cursor)
{
	struct m0_be_pd_mapping *mapping = NULL;
	struct m0_be_pd_page    *last_page;
	struct m0_be_pd         *paged      = cursor->rpi_paged;
	const void       *start_addr = cursor->rpi_addr;
	const void       *end_addr   = cursor->rpi_addr + cursor->rpi_size - 1;

	/* XXX TODO protect bp_mappings with bp_lock */
	if (cursor->rpi_mapping == NULL) {
		m0_tl_for(mappings, &paged->bp_mappings, mapping) {
			cursor->rpi_page =
				m0_be_pd_mapping__addr_to_page(mapping,
							       start_addr);
			if (cursor->rpi_page == NULL)
				continue;
			cursor->rpi_mapping = mapping;
			return true;
		} m0_tl_endfor;
	} else {
		if (m0_be_pd_mapping__is_addr_in_page(cursor->rpi_page,
						      end_addr))
			return false;

		mapping = cursor->rpi_mapping;
		last_page = &mapping->pas_pages[mapping->pas_pcount - 1];
		if (cursor->rpi_page != last_page) {
			cursor->rpi_page++;
			return true;
		}

		mapping = mappings_tlist_next(&paged->bp_mappings, mapping);
		if (mapping == NULL)
			return false;

		cursor->rpi_mapping = mapping;
		cursor->rpi_page = &mapping->pas_pages[0];
		return true;
	}

	return false;
}

M0_INTERNAL struct m0_be_pd_page *
m0_be_prp_cursor_page_get(struct m0_be_prp_cursor *cursor)
{
	return cursor->rpi_page;
}

/* -------------------------------------------------------------------------- */
/* Mappings							              */
/* -------------------------------------------------------------------------- */

M0_INTERNAL void m0_be_pd_mappings_lock(struct m0_be_pd              *paged,
					struct m0_be_pd_request      *request)
{
}

M0_INTERNAL void m0_be_pd_mappings_unlock(struct m0_be_pd            *paged,
					  struct m0_be_pd_request    *request)
{
}

M0_INTERNAL int m0_be_pd_page_init(struct m0_be_pd_page *page,
				   void                 *addr,
				   m0_bcount_t           size)
{
	page->pp_addr   = addr;
	page->pp_size   = size;
	page->pp_cellar = NULL;
	page->pp_ref    = 0;
	m0_mutex_init(&page->pp_lock);
	page->pp_state = M0_PPS_UNMAPPED;
	pages_tlink_init(page);

	return 0;
}

M0_INTERNAL void m0_be_pd_page_fini(struct m0_be_pd_page *page)
{
	pages_tlink_fini(page);
	m0_mutex_fini(&page->pp_lock);
	M0_ASSERT_INFO(page->pp_ref == 0,
		       "page=%p pp_addr=%p pp_cellar=%p pp_ref=%"PRIu64,
		       page, page->pp_addr, page->pp_cellar, page->pp_ref);
	/* TODO check the current state */
	page->pp_state = M0_PPS_FINI;
}

M0_INTERNAL void m0_be_pd_page_lock(struct m0_be_pd_page *page)
{
	m0_mutex_lock(&page->pp_lock);
}

M0_INTERNAL void m0_be_pd_page_unlock(struct m0_be_pd_page *page)
{
	m0_mutex_unlock(&page->pp_lock);
}

M0_INTERNAL bool m0_be_pd_page_is_locked(struct m0_be_pd_page *page)
{
	return m0_mutex_is_locked(&page->pp_lock);
}

M0_INTERNAL bool m0_be_pd_page_is_in(struct m0_be_pd      *paged,
				     struct m0_be_pd_page *page)
{
	M0_PRE(m0_be_pd_page_is_locked(page));

	return !M0_IN(page->pp_state, (M0_PPS_FINI, M0_PPS_UNMAPPED));
}

M0_INTERNAL void *m0_be_pd_mapping__addr(struct m0_be_pd_mapping *mapping)
{
	return mapping->pas_pages[0].pp_addr;
}

M0_INTERNAL m0_bcount_t
m0_be_pd_mapping__page_size(struct m0_be_pd_mapping *mapping)
{
	return mapping->pas_pages[0].pp_size;
}

M0_INTERNAL m0_bcount_t m0_be_pd_mapping__size(struct m0_be_pd_mapping *mapping)
{
	return m0_be_pd_mapping__page_size(mapping) * mapping->pas_pcount;
}

static struct m0_be_pd_mapping *be_pd_mapping_find(struct m0_be_pd *paged,
						   const void      *addr,
						   m0_bcount_t      size)
{
	struct m0_be_pd_mapping *mapping;

	M0_PRE(be_pd_is_locked(paged));

	m0_tl_for(mappings, &paged->bp_mappings, mapping) {
		if (m0_be_pd_mapping__addr(mapping) == addr &&
		    m0_be_pd_mapping__size(mapping) == size)
			return mapping;
	} m0_tl_endfor;

	return NULL;
}

/* Uses madvise(2) on memory range which may be subrange of the mapping. */
static void be_pd_mapping_advise(struct m0_be_pd_mapping *mapping,
				 void                    *addr,
				 size_t                   len)
{
	void *reserved;
	void *addr_end;
	int   rc;

	rc = madvise(addr, len, MADV_DONTFORK);
	if (rc != 0)
		M0_LOG(M0_DEBUG, "MADV_DONTFORK: errno=%d", errno);

	/*
	 * Dump first 64MB of mapping for debug. TODO Configure instead of
	 * hardcoding.
	 */
	reserved = (char *)m0_be_pd_mapping__addr(mapping) + (64ULL << 20);
	addr_end = (char *)addr + len;
	if ((unsigned long)addr_end > (unsigned long)reserved) {
		if ((unsigned long)addr < (unsigned long)reserved) {
			addr = reserved;
			len -= reserved - addr;
		}
		/* Rely on m0_dont_dump() reporting errors. */
		(void)m0_dont_dump(addr, len);
	}
}

static int be_pd_mapping_map(struct m0_be_pd_mapping *mapping)
{
	void   *addr;
	void   *p;
	size_t  size;

	addr = m0_be_pd_mapping__addr(mapping);
	size = m0_be_pd_mapping__size(mapping);

	switch (mapping->pas_type) {
	case M0_BE_PD_MAPPING_SINGLE:
		p = mmap(addr, size, PROT_READ | PROT_WRITE,
			 MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE |
			 MAP_PRIVATE, -1, 0);
		break;
	case M0_BE_PD_MAPPING_PER_PAGE:
		M0_CASSERT(MAP_FAILED != NULL);
		p = NULL;
		break;
	case M0_BE_PD_MAPPING_COMPAT:
		p = mmap(addr, size, PROT_READ | PROT_WRITE,
			 MAP_FIXED | MAP_NORESERVE | MAP_PRIVATE,
			 mapping->pas_fd, 0);
		break;
	default:
		M0_IMPOSSIBLE("Mapping type doesn't exist.");
	}

	if (p != MAP_FAILED && p != NULL)
		be_pd_mapping_advise(mapping, addr, size);

	return p == MAP_FAILED ? M0_ERR(-errno) : M0_RC(0);
}

static int be_pd_mapping_unmap(struct m0_be_pd_mapping *mapping)
{
	int rc;

	switch (mapping->pas_type) {
	case M0_BE_PD_MAPPING_COMPAT:
		/* fall through */
	case M0_BE_PD_MAPPING_SINGLE:
		rc = munmap(m0_be_pd_mapping__addr(mapping),
			    m0_be_pd_mapping__size(mapping));
		if (rc != 0)
			rc = M0_ERR(-errno);
		break;
	case M0_BE_PD_MAPPING_PER_PAGE:
		rc = 0;
		break;
	default:
		M0_IMPOSSIBLE("Mapping type doesn't exist.");
	}
	return M0_RC(rc);
}

M0_INTERNAL int m0_be_pd_mapping_init(struct m0_be_pd *paged,
				      void            *addr,
				      m0_bcount_t      size,
				      int              fd)
{
	struct m0_be_pd_mapping *mapping;
	m0_bcount_t              page_size = paged->bp_cfg.bpc_page_size;
	m0_bcount_t              i;
	int                      rc;

	M0_PRE(size % page_size == 0);
	M0_ALLOC_PTR(mapping);
	M0_ASSERT(mapping != NULL); /* XXX */

	mapping->pas_type   = paged->bp_cfg.bpc_mapping_type;
	mapping->pas_pd     = paged;
	mapping->pas_pcount = size / page_size;
	M0_ALLOC_ARR(mapping->pas_pages, mapping->pas_pcount);
	M0_ASSERT(mapping->pas_pages != NULL); /* XXX */
	for (i = 0; i < mapping->pas_pcount; ++i)
		m0_be_pd_page_init(&mapping->pas_pages[i],
				   (char *)addr + i * page_size, page_size);
	m0_mutex_init(&mapping->pas_lock);
	mappings_tlink_init(mapping);

	/* TODO Remove when BE conversion is finished. */
	if (fd > 0) {
		mapping->pas_type = M0_BE_PD_MAPPING_COMPAT;
		mapping->pas_fd   = fd;
	}

	rc = be_pd_mapping_map(mapping);
	M0_ASSERT(rc == 0); /* XXX */
	be_pd_lock(paged);
	M0_ASSERT(be_pd_mapping_find(paged, addr, size) == NULL);
	mappings_tlist_add(&paged->bp_mappings, mapping);
	be_pd_unlock(paged);

	/* XXX: delete this after page_get/put ready in code */
	if (mapping->pas_type == M0_BE_PD_MAPPING_COMPAT) {
		for (i = 0; i < mapping->pas_pcount; ++i) {
			m0_be_pd_page_lock(&mapping->pas_pages[i]);
			rc = m0_be_pd_mapping_page_attach(mapping,
						      &mapping->pas_pages[i]);
			M0_ASSERT(rc == 0);
			m0_be_pd_page_unlock(&mapping->pas_pages[i]);
		}
	}


	return M0_RC(0);
}

M0_INTERNAL int m0_be_pd_mapping_fini(struct m0_be_pd *paged,
				      const void      *addr,
				      m0_bcount_t      size)
{
	struct m0_be_pd_mapping *mapping;
	m0_bcount_t              i;
	int                      rc;

	be_pd_lock(paged);

	mapping = be_pd_mapping_find(paged, addr, size);
	M0_ASSERT(mapping != NULL);
	mappings_tlist_del(mapping);

	be_pd_unlock(paged);

	/* XXX: delete this after page_get/put ready in code */
	if (mapping->pas_type == M0_BE_PD_MAPPING_COMPAT) {
		for (i = 0; i < mapping->pas_pcount; ++i) {
			m0_be_pd_page_lock(&mapping->pas_pages[i]);
			int rc = m0_be_pd_mapping_page_detach(mapping,
							&mapping->pas_pages[i]);
			M0_ASSERT(rc == 0);
			m0_be_pd_page_unlock(&mapping->pas_pages[i]);
		}
	}

	rc = be_pd_mapping_unmap(mapping);
	if (rc != 0)
		return M0_RC(rc);
	mappings_tlink_fini(mapping);
	m0_mutex_fini(&mapping->pas_lock);
	for (i = 0; i < mapping->pas_pcount; ++i)
		m0_be_pd_page_fini(&mapping->pas_pages[i]);
	m0_free(mapping->pas_pages);
	m0_free(mapping);

	return M0_RC(0);
}

M0_INTERNAL int m0_be_pd_mapping_page_attach(struct m0_be_pd_mapping *mapping,
					     struct m0_be_pd_page    *page)
{
	void *p;
	int   rc;
	int   rc2;

	M0_PRE(m0_be_pd_page_is_locked(page));

	switch (mapping->pas_type) {
	case M0_BE_PD_MAPPING_SINGLE:
		rc = mlock(page->pp_addr, page->pp_size);
		if (rc != 0)
			rc = M0_ERR(-errno);
		break;
	case M0_BE_PD_MAPPING_PER_PAGE:
		p = mmap(page->pp_addr, page->pp_size, PROT_READ | PROT_WRITE,
			 MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE |
			 MAP_POPULATE | MAP_PRIVATE, -1, 0);
		if (p == MAP_FAILED)
			rc = M0_ERR(-errno);
		else {
			be_pd_mapping_advise(mapping,
					     page->pp_addr, page->pp_size);
			rc = mlock(page->pp_addr, page->pp_size);
			if (rc != 0) {
				rc = M0_ERR(-errno);
				rc2 = munmap(page->pp_addr, page->pp_size);
				if (rc2 != 0)
					M0_LOG(M0_ERROR,
					       "Cannot unmap region addr=%p "
					       "size=%lu", page->pp_addr,
					       (unsigned long)page->pp_size);
			} else {
				be_pd_mem_usage_increase(mapping->pas_pd,
				                         page->pp_size);
			}
		}
		break;
	case M0_BE_PD_MAPPING_COMPAT:
		M0_LOG(M0_DEBUG, "Appempt to attach page %p in compat mode",
				 page->pp_addr);
		rc = 0;
		break;
	default:
		M0_IMPOSSIBLE("Mapping type doesn't exist.");
	}

	if (rc == 0) {
		/* XXX: 4k */
		page->pp_cellar = m0_alloc_aligned(page->pp_size, 12);
		be_pd_mem_usage_increase(mapping->pas_pd, page->pp_size);
		M0_ASSERT(page->pp_cellar != NULL);
		memcpy(page->pp_cellar, page->pp_addr, page->pp_size);
		/* XXX */
		page->pp_state = mapping->pas_type == M0_BE_PD_MAPPING_COMPAT ?
			M0_PPS_READY : M0_PPS_MAPPED;
	}

	return M0_RC(rc);
}

M0_INTERNAL int m0_be_pd_mapping_page_detach(struct m0_be_pd_mapping *mapping,
					     struct m0_be_pd_page    *page)
{
	int rc;

	M0_PRE(m0_be_pd_page_is_locked(page));

	/* TODO make proper poisoning */
	memset(page->pp_addr, 0xCC, page->pp_size);

	/* Unlock memory region */
	switch (mapping->pas_type) {
	case M0_BE_PD_MAPPING_SINGLE:
		/* fall through */
	case M0_BE_PD_MAPPING_PER_PAGE:
		rc = munlock(page->pp_addr, page->pp_size);
		if (rc != 0)
			rc = M0_ERR(-errno);
		break;
	case M0_BE_PD_MAPPING_COMPAT:
		/* do nothing */
		rc = 0;
		break;
	default:
		M0_IMPOSSIBLE("Mapping type doesn't exist.");
	}
	if (rc != 0)
		return M0_RC(rc);

	/* Release system pages */
	switch (mapping->pas_type) {
	case M0_BE_PD_MAPPING_SINGLE:
		/* Use MADV_FREE on Linux 4.5 and higher. */
		rc = madvise(page->pp_addr, page->pp_size,
			     /* XXX MADV_FREE */ MADV_DONTNEED);
		if (rc != 0)
			rc = M0_ERR(-errno);
		break;
	case M0_BE_PD_MAPPING_PER_PAGE:
		rc = munmap(page->pp_addr, page->pp_size);
		/* XXX handle detach error */
		if (rc != 0)
			rc = M0_ERR(-errno);
		be_pd_mem_usage_decrease(mapping->pas_pd, page->pp_size);
		break;
	case M0_BE_PD_MAPPING_COMPAT:
		/* do nothing */
		rc = 0;
		break;
	default:
		M0_IMPOSSIBLE("Mapping type doesn't exist.");
	}

	if (rc == 0) {
		/* XXX: 4k */
		m0_free_aligned(page->pp_cellar, page->pp_size, 12);
		be_pd_mem_usage_decrease(mapping->pas_pd, page->pp_size);
		page->pp_state = M0_PPS_UNMAPPED;
	}

	return M0_RC(rc);
}

M0_INTERNAL bool
m0_be_pd_mapping__is_addr_in_page(const struct m0_be_pd_page *page,
				  const void                 *addr)
{
	return page->pp_addr <= addr && addr < page->pp_addr + page->pp_size;
}

/**
 *  @return NULL if @addr is out of the @mapping
 *  @return @page containing given @addr from the mapping
 */
M0_INTERNAL struct m0_be_pd_page *
m0_be_pd_mapping__addr_to_page(struct m0_be_pd_mapping *mapping,
			       const void              *addr)
{
	struct m0_be_pd_page *page = NULL;
	m0_bcount_t           n;
	m0_bcount_t           page_size;
	void                 *mapping_start;
	void                 *mapping_end;

	mapping_start = m0_be_pd_mapping__addr(mapping);
	mapping_end   = m0_be_pd_mapping__size(mapping) + (char *)mapping_start;
	page_size     = m0_be_pd_mapping__page_size(mapping);
	if (addr >= mapping_start && addr < mapping_end) {
		n = (addr - mapping_start) / page_size;
		page = &mapping->pas_pages[n];
		M0_ASSERT(m0_be_pd_mapping__is_addr_in_page(page, addr));
	}
	return page;
}

M0_INTERNAL struct m0_be_pd_mapping *
m0_be_pd__mapping_by_addr(struct m0_be_pd *paged, const void *addr)
{
	struct m0_be_pd_mapping *mapping;

	be_pd_lock(paged);

	m0_tl_for(mappings, &paged->bp_mappings, mapping) {
		if (addr >= m0_be_pd_mapping__addr(mapping) &&
		    addr <  m0_be_pd_mapping__addr(mapping) +
			    m0_be_pd_mapping__size(mapping))
			break;
	} m0_tl_endfor;

	be_pd_unlock(paged);

	return mapping;
}

M0_INTERNAL struct m0_be_seg *
m0_be_pd__page_to_seg(struct m0_be_pd            *paged,
		      const struct m0_be_pd_page *page)
{
	return m0_be_pd_seg_by_addr(paged, page->pp_addr);
}

#undef M0_TRACE_SUBSYSTEM

/** @} end_addr of PageD group */

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
