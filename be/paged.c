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
#include "mero/magic.h"
#include "be/paged.h"
#include "be/tx_service.h"   /* m0_be_txs_stype */
#include "rpc/rpc_opcodes.h" /* M0_BE_PD_FOM_OPCODE */

/* -------------------------------------------------------------------------- */
/* Prototypes								      */
/* -------------------------------------------------------------------------- */

M0_TL_DESCR_DEFINE(reqq,
		   "list of m0_be_pd_request-s inside m0_be_pd_request_queue",
		   static, struct m0_be_pd_request, ptr_rq_link, ptr_magic,
		   M0_BE_PD_REQQ_LINK_MAGIC, M0_BE_PD_REQQ_MAGIC);
M0_TL_DEFINE(reqq, static, struct m0_be_pd_request);


M0_TL_DESCR_DEFINE(mappings,
		   "list of MAPPINGSZZZZZ XXXXXXXXXXXXx ",
		   static, struct m0_be_pd_mapping, pas_tlink, pas_magic,
		   M0_BE_PD_REQQ_LINK_MAGIC, M0_BE_PD_REQQ_MAGIC); /*XXX*/
M0_TL_DEFINE(mappings, static, struct m0_be_pd_mapping);

static bool mapping_is_addr_in_page(struct m0_be_pd_page *page,
				    const void *addr);

static struct m0_be_pd_page *
mapping_addr_to_page(struct m0_be_pd_mapping *mapping, const void *addr);

/* -------------------------------------------------------------------------- */
/* PD FOM							              */
/* -------------------------------------------------------------------------- */

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

static int pd_fom_tick(struct m0_fom *fom)
{
	return M0_FSO_AGAIN;
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
				   struct m0_be_pD        *pd,
				   struct m0_reqh         *reqh)
{
	m0_fom_init(&fom->bpf_gen, &pd_fom_type, &pd_fom_ops, NULL, NULL, reqh);
}


M0_INTERNAL void m0_be_pd_fom_fini(struct m0_be_pd_fom *fom)
{
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

M0_INTERNAL int m0_be_pd_request_queue_init(struct m0_be_pd_request_queue *rq)
{
	reqq_tlist_init(&rq->prq_queue);
	m0_mutex_init(&rq->prq_lock);

	return 0;
}

M0_INTERNAL void m0_be_pd_request_queue_fini(struct m0_be_pd_request_queue *rq)
{
	m0_mutex_fini(&rq->prq_lock);
	reqq_tlist_fini(&rq->prq_queue);
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

M0_INTERNAL void
m0_be_pd_request_queue_push(struct m0_be_pd_request_queue      *rq,
			    struct m0_be_pd_request            *request,
			    struct m0_fom                      *fom)
{
	M0_ENTRY("request=%p", request);

	m0_mutex_lock(&rq->prq_lock);

	/* no special lock is needed here */
	if (reqq_tlist_length(&rq->prq_queue) == 0 &&
	    m0_fom_phase(fom) == PFS_IDLE)
		m0_fom_wakeup(fom);

	reqq_tlist_add(&rq->prq_queue, request);
	m0_mutex_unlock(&rq->prq_lock);

	M0_LEAVE();
}

M0_INTERNAL void m0_be_pd_request_push(struct m0_be_pD         *paged,
				       struct m0_be_pd_request *request)
{
	m0_be_op_active(&request->prt_op);
	m0_be_pd_request_queue_push(paged->bp_reqq, request,
				    &paged->bp_fom->bpf_gen);
}

/* -------------------------------------------------------------------------- */
/* Requests							              */
/* -------------------------------------------------------------------------- */

bool request_invariant(struct m0_be_pd_request *request)
{
	return true;
}

M0_INTERNAL void m0_be_pd_request_init(struct m0_be_pd_request       *request,
				       struct m0_be_pd_request_pages *pages)
{
	m0_be_op_init(&request->prt_op);
	reqq_tlink_init(request);
	request->prt_pages = *pages;
}

M0_INTERNAL void m0_be_pd_request_fini(struct m0_be_pd_request *request)
{
	reqq_tlink_fini(request);
	m0_be_op_fini(&request->prt_op);
}

M0_INTERNAL void m0_be_pd_request_pages_init(struct m0_be_pd_request_pages *rqp,
					     enum m0_be_pd_request_type type,
					     struct m0_be_reg_area     *rarea,
					     struct m0_be_reg          *reg)
{
	*rqp = (struct m0_be_pd_request_pages) {
		.prp_type = type,
		.prp_reg_area = rarea,
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
m0_be_pd_request__copy_to_cellars(struct m0_be_pD         *paged,
				  struct m0_be_pd_request *request)
{
	m0_be_pd_request_pages_forall(paged, request,
	      LAMBDA(bool, (struct m0_be_pd_page *page,
			    struct m0_be_reg_d *rd) {
			     copy_reg_to_page(page, rd);
			     return true;
		     }));
}

M0_INTERNAL bool m0_be_pd_pages_are_in(struct m0_be_pD         *paged,
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
request_pages_forall_helper(struct m0_be_pD         *paged,
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
m0_be_pd_request_pages_forall(struct m0_be_pD         *paged,
			      struct m0_be_pd_request *request,
			      bool (*iterate)(struct m0_be_pd_page *page,
					      struct m0_be_reg_d   *rd))
{
	struct m0_be_pd_request_pages *rpages = &request->prt_pages;
	struct m0_be_reg_d            *rd;
	struct m0_be_reg_d             rd_read;

	if (rpages->prp_type == PRT_READ) {
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
				       struct m0_be_pD               *paged,
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
	struct m0_be_pD         *paged      = cursor->rpi_paged;
	const void       *start_addr = cursor->rpi_addr;
	const void       *end_addr   = cursor->rpi_addr + cursor->rpi_size - 1;

	if (cursor->rpi_mapping == NULL) {
		m0_tl_for(mappings, &paged->bp_mappings, mapping) {
			cursor->rpi_page = mapping_addr_to_page(mapping,
								start_addr);
			if (cursor->rpi_page == NULL)
				continue;
			cursor->rpi_mapping = mapping;
			return true;
		} m0_tl_endfor;
	} else {
		if (mapping_is_addr_in_page(cursor->rpi_page, end_addr))
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

static bool mapping_is_addr_in_page(struct m0_be_pd_page *page,
					 const void *addr)
{
	return page->pp_page <= addr && addr < page->pp_page + page->pp_size;
}

/**
 *  @return NULL if @addr is out of the @mapping
 *  @return @page containing given @addr from the mapping
 */
static struct m0_be_pd_page *
mapping_addr_to_page(struct m0_be_pd_mapping *mapping, const void *addr)
{
	/* WARNING: Very dumb implementation.
	 *          Just to see the picture of underlying process.
	 */
	m0_bcount_t nr;

	for (nr = 0; nr < mapping->pas_pcount; ++nr) {
		if (mapping_is_addr_in_page(&mapping->pas_pages[nr], addr))
			return &mapping->pas_pages[nr];
	}
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
