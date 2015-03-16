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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 04-Jun-2013
 */

/**
 * @addtogroup locality
 *
 * @{
 */

#include "sm/sm.h"
#include "lib/chan.h"
#include "lib/arith.h"                    /* max_check */
#include "lib/errno.h"                    /* ENOMEM */
#include "lib/thread.h"
#include "lib/memory.h"
#include "lib/processor.h"
#include "lib/locality.h"
#include "fop/fom.h"

static struct m0_locality    locs_fallback;
static struct m0_sm_group    locs_grp;
static struct m0_thread      locs_ast_thread;
static bool                  locs_shutdown;
static struct m0_fom_domain *locs_dom;

M0_INTERNAL struct m0_locality *m0_locality_here(void)
{
	return m0_locality_get(m0_processor_id_get());
}

M0_INTERNAL struct m0_locality *m0_locality_get(uint64_t value)
{
	if (locs_dom != NULL) {
		struct m0_fom_locality *floc;
		int                    idx = value % locs_dom->fd_localities_nr;

		floc = locs_dom->fd_localities[idx];
		M0_ASSERT(m0_bitmap_get(&floc->fl_processors, idx));
		return &floc->fl_locality;
	} else
		return &locs_fallback;
}

M0_INTERNAL struct m0_locality *m0_locality0_get(void)
{
	return &locs_fallback;
}

M0_INTERNAL void m0_locality_dom_set(struct m0_fom_domain *dom)
{
	if (locs_dom == NULL)
		locs_dom = dom;
}

M0_INTERNAL void m0_locality_dom_clear(struct m0_fom_domain *dom)
{
	if (dom == locs_dom)
		locs_dom = NULL;
}

static void locs_ast_handler(void *__unused)
{
	while (!locs_shutdown) {
		m0_chan_wait(&locs_grp.s_clink);
		m0_sm_group_lock(&locs_grp);
		m0_sm_asts_run(&locs_grp);
		m0_sm_group_unlock(&locs_grp);
	}
}

static int ast_thread_init(void *__unused)
{
	return 0;
}

M0_INTERNAL int m0_localities_init(void)
{
	m0_sm_group_init(&locs_grp);
	locs_fallback.lo_grp = &locs_grp;
	/*
	 * Start fall-back ast processing thread. Dummy init function
	 * (ast_thread_init()) is used to guarantee that the thread has started
	 * by the time M0_THREAD_INIT() returns. This is needed to make
	 * intialisation order deterministic.
	 */
	return M0_THREAD_INIT(&locs_ast_thread, void *, &ast_thread_init,
			      &locs_ast_handler, NULL, "m0_fallback_ast");
}

M0_INTERNAL void m0_localities_fini(void)
{
	locs_shutdown = true;
	m0_clink_signal(&locs_grp.s_clink);
	m0_thread_join(&locs_ast_thread);
	m0_thread_fini(&locs_ast_thread);
	m0_sm_group_fini(&locs_grp);
}

/** @} end of locality group */

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
