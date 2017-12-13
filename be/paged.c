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

/* -------------------------------------------------------------------------- */
/* Prototypes								      */
/* -------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------- */
/* Request queue							      */
/* -------------------------------------------------------------------------- */

M0_TL_DESCR_DEFINE(reqq,
		   "list of m0_be_pd_request-s inside m0_be_pd_request_queue",
		   static, struct m0_be_pd_request, ptr_rq_link, ptr_magic,
		   M0_BE_PD_REQQ_LINK_MAGIC, M0_BE_PD_REQQ_MAGIC);
M0_TL_DEFINE(reqq, static, struct m0_be_pd_request);


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
#if 0
M0_INTERNAL void
m0_be_pd_request_pages_forall(struct m0_be_pD                   *paged,
			      struct m0_be_pd_request           *request,
			      bool (*iter)(struct m0_be_pd_page *page,
					   const void *param))
{
	M0_PRE(request_invariant(request));

	if (request->prt_pages.prp_type == PRT_READ)
		mapping_iterate_reg_by_pages(paged,
				     &request->prt_pages.prp_reg,
				     iter);
	else
		mapping_iterate_reg_area_by_pages(paged,
					  request->prt_pages.prp_reg_area,
					  iter);
}

static bool copy_one_reg_to_cellar(struct m0_be_pd_page *page,
				   const void *param)
{

	return false;
}
#endif

M0_INTERNAL void
m0_be_pd_request__copy_to_cellars(struct m0_be_pD         *paged,
				  struct m0_be_pd_request *request)
{
#if 0
	struct m0_be_reg_d  *rd;

	M0_BE_REG_AREA_FORALL(request->prt_pages.prp_reg_area, rd) {
		mapping_iterate_reg_by_pages(paged, &rd->rd_reg,
					     copy_one_reg_to_cellar);
	}
#endif
}

M0_INTERNAL bool m0_be_pd_pages_are_in(struct m0_be_pD               *paged,
				       struct m0_be_pd_request_pages *pages)
{
	return false;
}

/* -------------------------------------------------------------------------- */
/* Mappings							              */
/* -------------------------------------------------------------------------- */

M0_TL_DESCR_DEFINE(mappings,
		   "list of m0_be_pd_request-s inside m0_be_pd_request_queue",
		   static, struct m0_be_pd_mapping, pas_tlink, pas_magic,
		   M0_BE_PD_REQQ_LINK_MAGIC, M0_BE_PD_REQQ_MAGIC); /*XXX*/
M0_TL_DEFINE(mappings, static, struct m0_be_pd_mapping);

M0_UNUSED static bool mapping_is_addr_in_page(struct m0_be_pd_page *page,
				    const void *addr)
{
	return page->pp_page <= addr && addr < page->pp_page + page->pp_size;
}

/**
 *  @return NULL if @addr is out of the @mapping
 *  @return @page containing given @addr from the mapping
 */
M0_UNUSED static struct m0_be_pd_page *
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

#if 0 // remove it
static void mapping_iterate_by_pages(struct m0_be_pD *paged,
				     const void *addr, m0_bcount_t size,
				     bool (*iter)(struct m0_be_pd_page *page,
						  const void *param))
{
	struct m0_be_pd_mapping *mapping;
	struct m0_be_pd_page    *start = NULL;
	struct m0_be_pd_page    *end;
	struct m0_be_pd_page    *page;
	const void              *start_addr = addr;
	const void              *end_addr   = addr + size - 1;
	bool                     look_at_next_mapping = false;

	m0_tl_for(mappings, &paged->bp_mappings, mapping) {
		if (look_at_next_mapping)
			start = &mapping->pas_pages[0];
		else
			start = mapping_addr_to_page(mapping, start_addr);


		if (start != NULL) {
			end = mapping_addr_to_page(mapping, end_addr);
			if (end != NULL) {
				for (page = start; page <= end; ++page) {
					if (!iter(page))
						return;
				}
				return;
			} else {
				end = &mapping->pas_pages[mapping->pas_pcount - 1];
				for (page = start; page <= end; ++page) {
					if (!iter(page))
						return;
				}
				look_at_next_mapping = true;
			}
		}

	} m0_tl_endfor;

	M0_POST(start != NULL);
	M0_POST(mapping_addr_to_page(mapping, end_addr));
}

static void mapping_iterate_reg_by_pages(struct m0_be_pD  *paged,
				 struct m0_be_reg         *reg,
				 bool (*iter)(struct m0_be_pd_page *page,
					      const void *param))
{
	mapping_iterate_by_pages(paged, reg->br_addr, reg->br_size, iter);
}

static void mapping_iterate_reg_area_by_pages(struct m0_be_pD *paged,
				      struct m0_be_reg_area   *rarea,
				      bool (*iter)(struct m0_be_pd_page *page,
						   const void *param))
{
	struct m0_be_reg_d  *rd;
	M0_BE_REG_AREA_FORALL(rarea, rd) {
		mapping_iterate_reg_by_pages(paged, &rd->rd_reg, iter);
	}
}
#endif

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
