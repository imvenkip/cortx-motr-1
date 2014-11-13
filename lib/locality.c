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

static struct m0_locality  locs_fallback;
static struct m0_sm_group  locs_grp;
static struct m0_locality *locs;
static m0_processor_nr_t   locs_nr;
static m0_processor_nr_t   locs_allocated;
static struct m0_thread    locs_ast_thread;
static bool                locs_shutdown;

M0_INTERNAL struct m0_locality *m0_locality_here(void)
{
	return m0_locality_get(m0_processor_id_get());
}

M0_INTERNAL struct m0_locality *m0_locality_get(uint64_t value)
{
	struct m0_locality *loc = &locs[value % locs_nr];

	return loc->lo_grp != NULL ? loc : &locs_fallback;
}

M0_INTERNAL struct m0_locality *m0_locality0_get(void)
{
	return &locs_fallback;
}

M0_INTERNAL void m0_locality_set(m0_processor_nr_t id, struct m0_locality *val)
{
	struct m0_locality *loc = &locs[id];

	M0_PRE(id < locs_allocated);
	if (loc->lo_grp == NULL) {
		M0_ASSERT(loc->lo_reqh == NULL);
		*loc = *val;
	}
	locs_nr = max_check(locs_nr, id + 1);
}

static void locs_ast_handler(void *__unused)
{
	while (!locs_shutdown) {
		while (!m0_chan_timedwait(&locs_grp.s_clink,
					  m0_time_from_now(10, 0))) {
		}
		m0_sm_group_lock(&locs_grp);
		m0_sm_asts_run(&locs_grp);
		m0_sm_group_unlock(&locs_grp);
	}
}

M0_INTERNAL int m0_localities_init(void)
{
	int result;

	m0_sm_group_init(&locs_grp);
	locs_fallback.lo_grp = &locs_grp;
	locs_allocated = m0_processor_nr_max();
	M0_ALLOC_ARR(locs, locs_allocated);
	if (locs != NULL) {
		result = M0_THREAD_INIT(&locs_ast_thread, void *, NULL,
					&locs_ast_handler,
					NULL, "m0_fallback_ast");
		if (result != 0)
			m0_free(locs);
	} else
		result = -ENOMEM;
	return result;
}

M0_INTERNAL void m0_localities_fini(void)
{
	locs_shutdown = true;
	m0_clink_signal(&locs_grp.s_clink);
	m0_thread_join(&locs_ast_thread);
	m0_thread_fini(&locs_ast_thread);
	m0_free(locs);
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
