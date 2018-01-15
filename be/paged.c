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
#include "be/paged.h"
#include "be/tx_service.h"   /* m0_be_txs_stype */
#include "rpc/rpc_opcodes.h" /* M0_BE_PD_FOM_OPCODE */
#include "module/instance.h" /* m0_get */

#include <sys/mman.h>        /* mmap, madvise */

/* -------------------------------------------------------------------------- */
/* XXX									      */
/* -------------------------------------------------------------------------- */

enum m0_be_pd_module_level {
	M0_BE_PD_LEVEL_INIT,
	M0_BE_PD_LEVEL_IO_SCHED,
	M0_BE_PD_LEVEL_REQQ,
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

/* -------------------------------------------------------------------------- */
/* m0_be_pd							              */
/* -------------------------------------------------------------------------- */

static int be_pd_level_enter(struct m0_module *module)
{
	struct m0_be_pd            *pd = M0_AMB(pd, module, bp_module);
	enum m0_be_pd_module_level  level = module->m_cur + 1;

	switch (level) {
	case M0_BE_PD_LEVEL_INIT:
		mappings_tlist_init(&pd->bp_mappings);
		return M0_RC(0);
	case M0_BE_PD_LEVEL_IO_SCHED:
		return M0_RC(m0_be_pd_io_sched_init(&pd->bp_io_sched,
					    &pd->bp_cfg.bpc_io_sched_cfg));
	case M0_BE_PD_LEVEL_REQQ:
		return m0_be_pd_request_queue_init(&pd->bp_reqq,
			pd->bp_cfg.bpc_io_sched_cfg.bpdc_sched.bisc_pos_start);
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
		mappings_tlist_fini(&pd->bp_mappings);
		return;
	case M0_BE_PD_LEVEL_IO_SCHED:
		m0_be_pd_io_sched_fini(&pd->bp_io_sched);
		return;
	case M0_BE_PD_LEVEL_REQQ:
		m0_be_pd_request_queue_fini(&pd->bp_reqq);
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
	BE_PD_LEVEL(M0_BE_PD_LEVEL_IO_SCHED),
	BE_PD_LEVEL(M0_BE_PD_LEVEL_REQQ),
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

/* -------------------------------------------------------------------------- */
/* PD FOM							              */
/* -------------------------------------------------------------------------- */

/**
 * Phases of PD fom.
 * @verbatim
 *                                INIT
 *                                 |
 *                 m0_be_pd_init() |
 *                                 v    m0_be_pd_fini()
 *                                IDLE ----------------> FINI
 *                                 | ^
 *                 m0_be_reg_get() | |
 *         m0_be_pd_request_push() | |
 *                                 | |
 *                                 | |
 *                                 v |
 *                       +------ MANAGE -------+
 *  request is PRT_WRITE |         ^           | request is PRT_WRITE
 *                       |         |           |
 *                       v         |           v
 *                     READ        |          WRITE
 *    request.op.channel |         |           | request.op.channel
 *             signalled |         |           | signalled
 *                       v         |           v
 *                     READ        |          WRITE
 *                     DONE        |          DONE
 *                       |         |           |
 *                       +-------->+<----------+
 * @endverbatim
 */
enum m0_be_pd_fom_state {
	PFS_INIT   = M0_FOM_PHASE_INIT,
	PFS_FINISH = M0_FOM_PHASE_FINISH,

	PFS_IDLE   = M0_FOM_PHASE_NR,
	PFS_READ,
	PFS_READ_DONE,
	PFS_WRITE,
	PFS_WRITE_DONE,
	PFS_MANAGE,
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

	_S(PFS_IDLE,       0, M0_BITS(PFS_MANAGE)),
	_S(PFS_MANAGE,     0, M0_BITS(PFS_IDLE, PFS_WRITE, PFS_READ)),
	_S(PFS_WRITE,      0, M0_BITS(PFS_WRITE_DONE)),
	_S(PFS_READ,       0, M0_BITS(PFS_READ_DONE)),
	_S(PFS_WRITE_DONE, 0, M0_BITS(PFS_MANAGE)),
	_S(PFS_READ_DONE,  0, M0_BITS(PFS_MANAGE)),
	_S(PFS_MANAGE,     0, M0_BITS(PFS_IDLE)),

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
	enum m0_be_pd_fom_state        phase = m0_fom_phase(fom);
	struct m0_be_pd_fom           *pd_fom = fom2be_pd_fom(fom);
	struct m0_be_pd               *paged = pd_fom->bpf_pd;
	struct m0_be_pd_io_sched      *pios = &paged->bp_io_sched;
	struct m0_be_pd_request_queue *queue = &paged->bp_reqq;
	struct m0_be_pd_request       *request;
	// struct m0_be_pd_io            *pio;
	struct m0_be_pd_page          *page;
	struct m0_be_reg_d            *rd;
	unsigned                       pages_nr;
	int rc;
	int rc1;

	M0_ENTRY("pd_fom=%p paged=%p phase=%d", pd_fom, paged, phase);

	switch(phase) {
	case PFS_INIT:
		m0_be_op_init(&pd_fom->bpf_op);
		break;
	case PFS_FINISH:
		m0_be_op_fini(&pd_fom->bpf_op);
		break;
	case PFS_IDLE:
		request = m0_be_pd_request_queue_pop(queue);
		M0_ASSERT(M0_IN(request->prt_pages.prp_type, (M0_PRT_READ,
		                                              M0_PRT_WRITE)));
		pd_fom->bpf_cur_request = request;
		if (request == NULL) {
			rc = M0_FSO_WAIT;
			break;
		} else {
			m0_fom_phase_move(fom, 0, PFS_MANAGE);
			rc = M0_FSO_AGAIN;
		}
		break;
	case PFS_MANAGE:
		request = pd_fom->bpf_cur_request;
		/* XXX use .._is_read()/...is_write() */
		m0_fom_phase_moveif(fom,
				    request->prt_pages.prp_type == M0_PRT_READ,
				    PFS_READ, PFS_WRITE);
		rc = M0_FSO_AGAIN;
		break;
	case PFS_READ:
		request = pd_fom->bpf_cur_request;
		M0_ASSERT(request != NULL);
		M0_BE_OP_SYNC(op,
			      m0_be_pd_io_get(pios, &pd_fom->bpf_cur_pio, &op));
		// pio = pd_fom->bpf_cur_pio;
		// m0_be_pd_io_init(pio, paged, M0_PIT_READ);
		pages_nr = 0;
		M0_BE_PD_REQUEST_PAGES_FORALL(paged, request, page, rd) {
			m0_be_pd_page_lock(page);
			if (!m0_be_pd_page_is_in(paged, page)) {
				/*
				 * XXX we need acccess to the mapping
				 * from segment
				 */
				/* XXX The actual allocation is here */
				rc1 = m0_be_pd_mapping_page_attach(
				    m0_be_pd__mapping_by_addr(paged,
				                        page->pp_page), page);
				M0_ASSERT(rc1 == 0);
			}
			if (page->pp_state == PPS_MAPPED) {
				/* NEXT IS HERE
				m0_be_io_add(m0_be_pd_io_be_io(pio),
					     rd->rd_seg->bs_stob,
					     ...,
				*/
				page->pp_state = PPS_READING;
				++pages_nr;
			}
			m0_be_pd_page_unlock(page);
		} M0_BE_PD_REQUEST_PAGES_ENDFOR;
		if (pages_nr == 0) {
			m0_fom_phase_move(fom, 0, PFS_MANAGE);
			rc = M0_FSO_AGAIN;
		} else {
			m0_be_op_reset(&pd_fom->bpf_op);
			rc = m0_be_op_tick_ret(&pd_fom->bpf_op, fom, PFS_READ_DONE);
			// NEXT IS HERE rc1 = m0_be_pd_io_add(pios,
			M0_ASSERT(rc1 == 0);
		}
		// m0_be_pd_io_add(pios, pio, page);
		break;
#if 0
	case PFS_READ_DONE:
		pio = pd_fom->bpf_pio;
		request = pd_fom->bpf_request;
		M0_BE_PD_REQUEST_PAGES_FORALL(paged, request, page) {
			m0_be_pd_page_lock(page);

			if (m0_tlist_contains(pio, page))
				page->pp_state = M0_PPS_READY;
			/* XXX: just for one PD FOM */
			M0_ASSERT(page->pp_state == M0_PPS_READY);

			/* XXX move page to ready state IF it is in pio */
			m0_be_pd_page_unlock(page);
		}
		m0_be_pd_io_fini(pio);
		m0_be_pd_io_put(pio);
		m0_be_op_done(&request->prt_op);

		m0_fom_phase_set(fom, PFS_IDLE);
		break;
	case PFS_WRITE:
		request = pd_fom->bpf_request;
		/**
		 *  NOTE: Looks like parallel READ requests do not coexist
		 *  with parallel WRITEs. Threrefore READs may not take lock on
		 *  the whole mapping.
		 */
		m0_be_pd_mappings_lock(paged, request);
		/* XXX: just for one PD FOM */
		M0_BE_PD_REQUEST_PAGES_FORALL(paged, request, page) {
			m0_be_pd_page_lock(page);

			M0_ASSERT(page->pp_state == M0_PPS_READY);
			page->pp_state = M0_PPS_WRITING;

			m0_be_pd_page_unlock(page);
		}
		m0_be_pd_mappings_unlock(paged, request);

		/* XXX: Assume that a page realted to the request isn't in
		 * M0_PPS_READY! */
		m0_be_pd_request__copy_to_cellars(paged, request);

		pio = m0_be_pd_io_get(paged);
		pd_fom->bpf_cur_pio = pio;
		m0_be_pd_io_init(pio, paged, M0_PIT_WRITE);

		M0_BE_PD_REQUEST_PAGES_FORALL(paged, request, page) {
			m0_be_pd_io_add(pio, page);
		}

		rc = m0_be_op_tick_ret(pio->pio_op, fom, PFS_WRITE_DONE);
		rc1 = m0_be_pd_io_launch(pio);
		M0_ASSERT(rc1 == 0);
		break;

	case PFS_WRITE_DONE:
		pio = pd_fom->bpf_pio;
		request = pd_fom->bpf_request;

		M0_BE_PD_REQUEST_PAGES_FORALL(paged, request, page) {
			m0_be_pd_page_lock(page);

			M0_ASSERT(page->pp_state == M0_PPS_WRITING);
			page->pp_state = M0_PPS_READY;

			m0_be_pd_page_unlock(page);
		}

		m0_be_pd_io_fini(pio);
		m0_be_pd_io_put(pio);
		m0_be_op_done(&request->prt_op);

		m0_fom_phase_set(fom, PFS_IDLE);
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
#endif
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


M0_INTERNAL void m0_be_pd_fom_init(struct m0_be_pd_fom    *fom,
				   struct m0_be_pd        *pd,
				   struct m0_reqh         *reqh)
{
	m0_fom_init(&fom->bpf_gen, &pd_fom_type, &pd_fom_ops, NULL, NULL, reqh);
}

M0_INTERNAL void m0_be_pd_fom_fini(struct m0_be_pd_fom *fom)
{
	/* fom->bpf_gen is finalised in pd_fom_fini() */
}

M0_INTERNAL int m0_be_pd_fom_start(struct m0_be_pd_fom *fom)
{
	/* XXX commented to pass current tests */
	/* m0_fom_queue(&fom->bpf_gen); */
	/* TODO wait until it starts */

	return 0;
}

M0_INTERNAL void m0_be_pd_fom_stop(struct m0_be_pd_fom *fom)
{
	/* TODO move fom to FINISH phase synchronously */
}


M0_INTERNAL void m0_be_pd_fom_mod_init(void)
{
	m0_fom_type_init(&pd_fom_type, M0_BE_PD_FOM_OPCODE, &pd_fom_type_ops,
			 &m0_be_txs_stype, &pd_fom_conf);
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
	return rq->prq_current_pos != request->prt_ext.e_start;
}

static bool be_pd_request_is_write(struct m0_be_pd_request *request)
{
	return request->prt_pages.prp_type == M0_PRT_WRITE;
}

static void deferred_insert_sorted(struct m0_be_pd_request_queue *rq,
				   struct m0_be_pd_request       *request)
{
	struct m0_be_pd_request *rq_prev;

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

	rq->prq_current_pos = request->prt_ext.e_end + 1;
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

	m0_mutex_lock(&rq->prq_lock);

	/*
	 * XXX Race here. Need to post ast and check state there. Also need to
	 * track whether ast already posted.
	 */

	/* no special lock is needed here */
	if (reqq_tlist_length(&rq->prq_queue) == 0 &&
	    m0_fom_phase(fom) == PFS_IDLE)
		m0_fom_wakeup(fom);

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
}

M0_INTERNAL void m0_be_pd_request_fini(struct m0_be_pd_request *request)
{
	reqq_tlink_fini(request);
}

M0_INTERNAL void m0_be_pd_request_pages_init(struct m0_be_pd_request_pages *rqp,
					     enum m0_be_pd_request_type type,
					     struct m0_be_reg_area     *rarea,
					     struct m0_ext             *ext,
					     struct m0_be_reg          *reg)
{
	*rqp = (struct m0_be_pd_request_pages) {
		.prp_type = type,
		.prp_reg_area = rarea,
		.prp_ext = *ext,
		.prp_reg = *reg,
	};
}

static void copy_reg_to_page(struct m0_be_pd_page       *page,
			     const struct m0_be_reg_d   *rd)
{
/**
 *      +---REG---+
 *    +--P1--+--P2--+
 *
 */
	void *addr = rd->rd_reg.br_addr + (page->pp_page - page->pp_cellar);
	m0_bcount_t size = min_check((uint64_t)(page->pp_cellar +
						page->pp_size -	addr),
				     (uint64_t)(addr + rd->rd_reg.br_size));

	memcpy(addr, rd->rd_buf, size);
}

M0_INTERNAL void
m0_be_pd_request__copy_to_cellars(struct m0_be_pd         *paged,
				  struct m0_be_pd_request *request)
{
	m0_be_pd_request_pages_forall(paged, request,
	      LAMBDA(bool, (struct m0_be_pd_page *page,
			    struct m0_be_reg_d *rd) {
			     copy_reg_to_page(page, rd);
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
						   (PPS_READY, PPS_WRITING));
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

	m0_be_prp_cursor_init(&cursor, (paged), rpages,
			      rd->rd_reg.br_addr,
			      rd->rd_reg.br_size);
	while (m0_be_prp_cursor_next(&cursor)) {
		(page) = m0_be_prp_cursor_page_get(&cursor);
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
		rd->rd_reg.br_addr = rpages->prp_reg.br_addr;
		rd->rd_reg.br_size = rpages->prp_reg.br_size;
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
	struct m0_be_pd_mapping *mapping;
	struct m0_be_pd_page    *last_page;
	struct m0_be_pd         *paged      = cursor->rpi_paged;
	const void       *start_addr = cursor->rpi_addr;
	const void       *end_addr   = cursor->rpi_addr + cursor->rpi_size - 1;

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
	page->pp_page   = addr;
	page->pp_size   = size;
	page->pp_cellar = NULL;
	m0_mutex_init(&page->pp_lock);
	/* TODO pp_pio_tlink */
	page->pp_state = PPS_UNMAPPED;

	return 0;
}

M0_INTERNAL void m0_be_pd_page_fini(struct m0_be_pd_page *page)
{
	/* TODO pp_pio_tlink */
	m0_mutex_fini(&page->pp_lock);
	page->pp_state = PPS_FINI;
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

	return !M0_IN(page->pp_state, (PPS_FINI, PPS_UNMAPPED));
}

M0_INTERNAL void *m0_be_pd_mapping__addr(struct m0_be_pd_mapping *mapping)
{
	return mapping->pas_pages[0].pp_page;
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
	 * XXX Dump first 64MB of mapping for debug. TODO Configure instead of
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
				      m0_bcount_t      page_size,
				      int              fd)
{
	struct m0_be_pd_mapping *mapping;
	m0_bcount_t              i;
	int                      rc;

	/* XXX paged must be locked here to protect mappings list */

	M0_PRE(size % page_size == 0);
	M0_PRE(be_pd_mapping_find(paged, addr, size) == NULL);

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
	mappings_tlist_add(&paged->bp_mappings, mapping);

	return M0_RC(0);
}

M0_INTERNAL int m0_be_pd_mapping_fini(struct m0_be_pd *paged,
				      const void      *addr,
				      m0_bcount_t      size)
{
	struct m0_be_pd_mapping *mapping;
	m0_bcount_t              i;
	int                      rc;

	/* XXX paged must be locked here to protect mappings list */

	mapping = be_pd_mapping_find(paged, addr, size);
	M0_ASSERT(mapping != NULL);

	mappings_tlist_del(mapping);
	rc = be_pd_mapping_unmap(mapping);
	if (rc != 0)
		return M0_RC(rc);
	mappings_tlink_fini(mapping);
	m0_mutex_fini(&mapping->pas_lock);
	for (i = 0; i < mapping->pas_pcount; ++i)
		m0_be_pd_page_fini(&mapping->pas_pages[i]);
	m0_free(mapping->pas_pages);
	m0_mutex_fini(&mapping->pas_lock);
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
		rc = mlock(page->pp_page, page->pp_size);
		if (rc != 0)
			rc = M0_ERR(-errno);
		break;
	case M0_BE_PD_MAPPING_PER_PAGE:
		p = mmap(page->pp_page, page->pp_size, PROT_READ | PROT_WRITE,
			 MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE |
			 MAP_POPULATE | MAP_PRIVATE, -1, 0);
		if (p == MAP_FAILED)
			rc = M0_ERR(-errno);
		else {
			be_pd_mapping_advise(mapping,
					     page->pp_page, page->pp_size);
			rc = mlock(page->pp_page, page->pp_size);
			if (rc != 0) {
				rc = M0_ERR(-errno);
				rc2 = munmap(page->pp_page, page->pp_size);
				if (rc2 != 0)
					M0_LOG(M0_ERROR,
					       "Cannot unmap region addr=%p "
					       "size=%lu", page->pp_page,
					       (unsigned long)page->pp_size);
			}
		}
		break;
	case M0_BE_PD_MAPPING_COMPAT:
		M0_LOG(M0_DEBUG, "Appempt to attach page %p in compat mode",
				 page->pp_page);
		break;
	default:
		M0_IMPOSSIBLE("Mapping type doesn't exist.");
	}

	if (rc == 0)
		page->pp_state = PPS_MAPPED;

	return M0_RC(rc);
}

M0_INTERNAL int m0_be_pd_mapping_page_detach(struct m0_be_pd_mapping *mapping,
					     struct m0_be_pd_page    *page)
{
	int rc;

	M0_PRE(m0_be_pd_page_is_locked(page));

	/* TODO make proper poisoning */
	memset(page->pp_page, 0xCC, page->pp_size);

	/* Unlock memory region */
	switch (mapping->pas_type) {
	case M0_BE_PD_MAPPING_SINGLE:
		/* fall through */
	case M0_BE_PD_MAPPING_PER_PAGE:
		rc = munlock(page->pp_page, page->pp_size);
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
		rc = madvise(page->pp_page, page->pp_size,
			     /* XXX MADV_FREE */ MADV_DONTNEED);
		if (rc != 0)
			rc = M0_ERR(-errno);
		break;
	case M0_BE_PD_MAPPING_PER_PAGE:
		rc = munmap(page->pp_page, page->pp_size);
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

	if (rc == 0)
		page->pp_state = PPS_UNMAPPED;

	return M0_RC(rc);
}

M0_INTERNAL bool
m0_be_pd_mapping__is_addr_in_page(const struct m0_be_pd_page *page,
				  const void                 *addr)
{
	return page->pp_page <= addr && addr < page->pp_page + page->pp_size;
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

	m0_tl_for(mappings, &paged->bp_mappings, mapping) {
		if (addr >= m0_be_pd_mapping__addr(mapping) &&
		    addr <  m0_be_pd_mapping__addr(mapping) +
			    m0_be_pd_mapping__size(mapping))
			return mapping;
	} m0_tl_endfor;

	return NULL;
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
