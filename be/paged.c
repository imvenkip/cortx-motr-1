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


// usecase 1: be segments
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

M0_INTERNAL void m0_be_seg_close(struct m0_be_seg *seg)
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
	if (m0_be_pd_pages_are_in(paged, &F(rpages))) {

		return;
	}

	/* XXX: remove M0_ALLOC_PTR() with pool-like allocator */
	M0_ALLOC_PTR(F(request));
	M0_ASSERT(request != NULL);
	m0_be_pd_request_init(F(request), &F(rpages));

	M0_ASSERT(op->bo_sm.sm_state == M0_BOS_ACTIVE);
	/* XXX: potential race near this point: current fom may go
	        to sleep here */
	m0_fom_wait_on(fom, F(request)->pr_op.bo_sm.sm_chan, &fom->fo_cb);
	m0_be_pd_request_push(F(paged), F(request));
	M0_CO_YIELD();
}

// usecase 3: PageD FOM: read, write requests
static struct m0_be_pd_fom *fom2be_pd_fom(struct m0_fom *fom);
M0_INTERNAL int m0_be_pd_tick(struct m0_fom *fom)
{
	enum tx_group_fom_state phase = m0_fom_phase(fom);
	struct m0_be_pd_fom *pd_fom   = fom2be_pd_fom(fom);

	switch(phase) {
	case PFS_INIT:
	case PFS_FINISH:
	case PFS_IDLE:
	case PFS_READ:
	case PFS_READ_DONE:
	case PFS_WRITE:
	case PFS_WRITE_DONE:
	default:
	}

	return M0_FSO_WAIT;
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
