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
 * Original author: Dmitriy Podgorniy <Dmitriy.Podgorniy@seagate.com>
 *
 * Original creation date: 1-Dec-2017
 */

/**
 * @addtogroup PageD
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"
#include "be/paged.h"


// usecase 1: be segments initialization
M0_INTERNAL int _m0_be_seg_open(struct m0_be_seg *seg)
{
	//...

	//--- REMOVE p = mmap(g->sg_addr, g->sg_size, PROT_READ | PROT_WRITE,
	//--- REMOVE  MAP_FIXED | MAP_PRIVATE | MAP_NORESERVE, fd, g->sg_offset);
	int rc;
	struct m0_be_pd *pd = seg->bs_domain->bd_paged;

	rc = m0_be_pd_mapping_init(pd, g->sg_addr, g->sg_size, 1 << 20);
	//...
}

M0_INTERNAL void _m0_be_seg_close(struct m0_be_seg *seg)
{
	//...
	//--- REMOVE  munmap(seg->bs_addr, seg->bs_size);
	int rc;
	struct m0_be_pd *pd = seg->bs_domain->bd_paged;

	rc = m0_be_pd_mapping_fini(pd, seg->bs_addr, seg->bs_size);
	M0_ASSERT(rc == 0);
	//...
}

//usecase 2: READ request
//TODO: wrap request_pages interfaces inside, remove knowledge regarding "pages"
// requested_regions_can_be_{used/obtained/given_to_user}()
M0_INTERNAL void m0_be_pd_reg_get(struct m0_be_pd      *paged,
				  struct m0_be_reg     *reg,
				  struct m0_co_context *context,
				  struct m0_fom        *fom)
{
	M0_CO_REENTER(
		struct m0_be_pd_request_pages  rpages;
		struct m0_be_pd_request       *request;
		);

	m0_be_pd_request_pages_init(&F(rpages), M0_PRT_READ, NULL, reg);
	/* lock is needed for m0_be_pd_pages_are_in() */
	if (m0_be_pd_pages_are_in(paged, &F(rpages))) {

		return;
	}

	/* XXX: remove M0_ALLOC_PTR() with pool-like allocator */
	M0_ALLOC_PTR(F(request));
	M0_ASSERT(request != NULL);
	m0_be_pd_request_init(F(request), &F(rpages));

	M0_ASSERT(F(request)->pr_op->bo_sm.sm_state == M0_BOS_ACTIVE);
	/* XXX: potential race near this point: current fom may go
	        to sleep here */
	m0_fom_wait_on(fom, F(request)->pr_op.bo_sm.sm_chan, &fom->fo_cb);
	/* XXX: ref counting */
	m0_be_pd_request_push(F(paged), F(request));
	M0_CO_YIELD();

	m0_be_pd_request_fini(F(request));
	m0_free(F(request));
}

M0_INTERNAL void m0_be_pd_reg_put(struct m0_be_pd      *paged,
				  struct m0_be_reg     *reg,
				  struct m0_co_context *context,
				  struct m0_fom        *fom)
{
	/* XXX: decrement ref counting */
}

// usecase 3: PageD FOM: read, write requests
static struct m0_be_pd_fom *fom2be_pd_fom(struct m0_fom *fom);
M0_INTERNAL int m0_be_pd_tick(struct m0_fom *fom)
{
	enum tx_group_fom_state phase = m0_fom_phase(fom);
	struct m0_be_pd_fom *pd_fom   = fom2be_pd_fom(fom);
	struct m0_be_pd *paged = pd_fom->bpf_pd;
	struct m0_be_pd_request_queue *queue;
	struct m0_be_pd_request *request;
	struct m0_be_pd_page *page;
	int rc;
	int rc1;

	switch(phase) {
	case PFS_INIT:
	case PFS_FINISH:
	case PFS_IDLE:
		queue = pd_fom->bpf_pd->pd_reqq;
		request = m0_be_pd_request_queue_pop(queue);
		if (request == NULL) {
			rc = M0_FSO_WAIT;
			break;
		}
		// M0_ASSERT(type in READ/WRITE);
		pd_fom->bpf_cur_request = request;
		m0_fom_phase_moveif(fom, request->prt_pages.prp_type == M0_PRT_READ,
				   PFS_READ, PFS_WRITE);
		rc = M0_FSO_AGAIN;
		break;
	case PFS_READ:
		request = pd_fom->bpf_cur_request;
		pio = m0_be_pd_io_get(paged);
		m0_be_pd_io_init(pio, paged, M0_PIT_READ);
		pd_fom->bpf_cur_pio = pio;
		M0_ASSERT(request != NULL);
		M0_BE_PD_REQUEST_PAGES_FORALL(paged, request, page) {
			m0_be_pd_page_lock(page);
			if (!m0_be_pd_page_is_in(paged, page))
				m0_be_pd_mapping_page_attach(paged, page);
			if (page->pp_state == M0_PPS_MAPPED) {
				page_pp_state = M0_PPS_READING;
				m0_be_pd_io_add(pio, page);
			}
			m0_be_pd_page_unlock(page);
		}
		/* XXX conditionally */
		rc = m0_be_op_tick_ret(pio->pio_op, fom, PFS_READ_DONE);
		rc1 = m0_be_pd_io_launch(pio);
		M0_ASSERT(rc1 == 0);
		break;
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
	default:
		M0_IMPOSSIBLE("XXX");
	}

	return rc;
}

//usecase 5: user calls write request
static int tx_group_fom_tick(struct m0_fom *fom)
{
	switch() {
	//...
	case TGS_PLACING:
		struct m0_be_pd_request       *request;
		struct m0_be_pd_request_pages  rpages;
		struct m0_be_pd               *paged = m->tgf_paged;

		m0_be_tx_group__tx_state_post(gr, M0_BTS_LOGGED, false);

		//m0_be_tx_group_seg_place_prepare(gr);
		//m0_be_tx_group_seg_place(gr, op);

		/* XXX: remove M0_ALLOC_PTR() with pool-like allocator */
		m0_be_pd_request_pages_init(&rpages, M0_PRT_WRITE,
					    &gr->tg_reg_area, NULL);
		M0_ALLOC_PTR(request);
		M0_ASSERT(request != NULL);
		m0_be_pd_request_init(request, &rpages);
		m0_be_pd_request_push(paged, request);

		return m0_be_op_tick_ret(&request->prt_op, fom, TGS_PLACED);
	//...
	case TGS_PLACED:
		//...
		/* get corresponding request from somewhere */
		struct m0_be_pd_request       *request;

		m0_be_pd_request_fini(request);
		m0_free(request);
		//...
	}
	;
}
#undef M0_TRACE_SUBSYSTEM

/** @} end of PageD group */

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
