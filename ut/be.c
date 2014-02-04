/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 13-Aug-2013
 */

/**
 * @addtogroup ut
 *
 * @{
 */

#include "ut/be.h"
#include "ut/ut.h"         /* M0_UT_ASSERT */
#include "be/ut/helper.h"  /* m0_be_ut_backend */
#include "lib/misc.h"      /* M0_BITS */

#include "reqh/reqh.h"

M0_INTERNAL void
m0_ut_backend_init(struct m0_be_ut_backend *be, struct m0_be_ut_seg *seg)
{
	m0_be_ut_backend_init(be);
	m0_be_ut_seg_init(seg, be, 1 << 20 /* 1 MB */);
	m0_be_ut_seg_allocator_init(seg, be);
}

M0_INTERNAL void
m0_ut_backend_fini(struct m0_be_ut_backend *be, struct m0_be_ut_seg *seg)
{
//	m0_be_ut_seg_allocator_fini(seg, be);
	m0_be_ut_seg_fini(seg);
	m0_be_ut_backend_fini(be);
}

M0_INTERNAL void m0_ut_be_tx_begin(struct m0_be_tx *tx,
				   struct m0_be_ut_backend *ut_be,
				   struct m0_be_tx_credit *cred)
{
	int rc;

	m0_be_ut_tx_init(tx, ut_be);
	m0_be_tx_prep(tx, cred);
	m0_be_tx_open(tx);
	rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_ACTIVE), M0_TIME_NEVER);
	M0_ASSERT(rc == 0);
}

M0_INTERNAL void m0_ut_be_tx_end(struct m0_be_tx *tx)
{
	int rc;

	m0_be_tx_close(tx);
	rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_DONE), M0_TIME_NEVER);
	M0_ASSERT(rc == 0);
	m0_be_tx_fini(tx);
}

M0_INTERNAL void *m0_ut_be_alloc(m0_bcount_t size, struct m0_be_seg *seg,
				 struct m0_be_ut_backend *ut_be)
{
	struct m0_be_tx_credit	cred = {};
	struct m0_be_tx		tx;
	void		       *ptr;

	m0_be_allocator_credit(m0_be_seg_allocator(seg), M0_BAO_ALLOC, size, 0,
			       &cred);
	m0_ut_be_tx_begin(&tx, ut_be, &cred);
	M0_BE_OP_SYNC(op,
		      m0_be_alloc(m0_be_seg_allocator(seg),
				  &tx, &op, &ptr, size));
	m0_ut_be_tx_end(&tx);
	return ptr;
}

M0_INTERNAL void m0_ut_be_free(void *ptr, m0_bcount_t size,
			       struct m0_be_seg *seg,
			       struct m0_be_ut_backend *ut_be)
{
	struct m0_be_tx_credit cred = {};
	struct m0_be_tx	       tx;

	m0_be_allocator_credit(m0_be_seg_allocator(seg), M0_BAO_FREE, size, 0,
			       &cred);
	m0_ut_be_tx_begin(&tx, ut_be, &cred);
	M0_BE_OP_SYNC(op, m0_be_free(m0_be_seg_allocator(seg), &tx, &op, ptr));
	m0_ut_be_tx_end(&tx);
}

M0_INTERNAL void m0_ut_backend_init_with_reqh(struct m0_reqh *reqh,
					      struct m0_be_ut_backend *be,
					      struct m0_be_ut_seg *seg,
					      m0_bcount_t seg_size)
{
	int rc;

	rc = M0_REQH_INIT(reqh,
			  .rhia_dtm       = NULL,
			  .rhia_db        = NULL,
			  .rhia_mdstore   = (void*)1,
			  .rhia_fol       = NULL,
			  .rhia_svc       = (void*)1);
	M0_ASSERT(rc == 0);
	be->but_dom_cfg.bc_engine.bec_group_fom_reqh = reqh;
	m0_be_ut_backend_init(be);
	m0_be_ut_seg_init(seg, be, seg_size);
	m0_be_ut_seg_allocator_init(seg, be);
	//m0_ut_backend_init(be, seg);
	rc = m0_be_ut__seg_dict_create(&seg->bus_seg,
				       m0_be_ut_backend_sm_group_lookup(be));
	M0_ASSERT(rc == 0);
}

M0_INTERNAL void m0_ut_backend_fini_with_reqh(struct m0_reqh *reqh,
					      struct m0_be_ut_backend *be,
					      struct m0_be_ut_seg *seg)
{
	int rc;

	rc = m0_be_ut__seg_dict_destroy(&seg->bus_seg,
					m0_be_ut_backend_sm_group_lookup(be));
	M0_ASSERT(rc == 0);
	m0_be_ut_seg_allocator_fini(seg, be);
	m0_ut_backend_fini(be, seg);
	m0_reqh_fini(reqh);
}

static bool fom_domain_is_idle(const struct m0_fom_domain *dom)
{
	int  i;
	bool result = false;

	for (i = 0; i < dom->fd_localities_nr; ++i) {
		if ((i == 0 &&
			dom->fd_localities[i].fl_foms == 1) ||
			dom->fd_localities[i].fl_foms == 0)
			result = true;
		else
			return false;
	}

	return result;
}

void m0_ut_be_fom_domain_idle_wait(struct m0_reqh *reqh)
{
	struct m0_clink clink;

	M0_PRE(reqh != NULL);
	m0_clink_init(&clink, NULL);
	m0_clink_add_lock(&reqh->rh_sd_signal, &clink);
	if (!fom_domain_is_idle(&reqh->rh_fom_dom))
		m0_chan_timedwait(&clink, m0_time_from_now(2, 0));
	m0_clink_del_lock(&clink);
	m0_clink_fini(&clink);
}


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
