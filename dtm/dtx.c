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
 * Original creation date: 27-Jan-2013
 */


/**
 * @addtogroup dtm
 *
 * @{
 */

#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/errno.h"              /* ENOMEM */

#include "dtm/dtm_internal.h"
#include "dtm/history.h"
#include "dtm/dtx.h"

struct m0_dtm_dtx_party {
	struct m0_dtm_dtx     *pa_dtx;
	struct m0_dtm_controlh pa_ch;
};

static struct m0_dtm_controlh *dtx_get(struct m0_dtm_dtx *dtx,
				       struct m0_dtm_remote *dtm);

M0_INTERNAL int m0_dtm_dtx_init(struct m0_dtm_dtx *dtx, uint64_t id,
				struct m0_dtm *dtm, uint32_t nr_max)
{
	dtx->dt_id = id;
	dtx->dt_dtm = dtm;
	dtx->dt_nr_max = nr_max;
	dtx->dt_nr = dtx->dt_nr_fixed = 0;
	M0_ALLOC_ARR(dtx->dt_party, nr_max);
	return dtx->dt_party == NULL ? -ENOMEM : 0;
}

M0_INTERNAL void m0_dtm_dtx_fini(struct m0_dtm_dtx *dtx)
{
	if (dtx->dt_party != NULL) {
		uint32_t i;

		for (i = 0; i < dtx->dt_nr; ++i)
			m0_dtm_controlh_fini(&dtx->dt_party[i].pa_ch);
		m0_free(dtx->dt_party);
	}
}

M0_INTERNAL void m0_dtm_dtx_add(struct m0_dtm_dtx *dtx,
				struct m0_dtm_oper *oper)
{
	oper_for(oper, i) {
		bool unique = true;
		oper_for(oper, j) {
			if (i == j)
				break;
			if (UPDATE_DTM(i) == UPDATE_DTM(j)) {
				unique = false;
				break;
			}
		} oper_endfor;
		if (unique)
			m0_dtm_controlh_add(dtx_get(dtx, UPDATE_DTM(i)),
					    oper);
	} oper_endfor;
}

M0_INTERNAL void m0_dtm_dtx_close(struct m0_dtm_dtx *dtx)
{
	uint32_t i;

	for (i = 0; i < dtx->dt_nr; ++i)
		m0_dtm_controlh_close(&dtx->dt_party[i].pa_ch);
}

static void dtx_noop(void *unused)
{}

static int dtx_find(const struct m0_dtm_history_type *ht,
		    const struct m0_uint128 *id,
		    struct m0_dtm_history **out)
{
	M0_IMPOSSIBLE("Looking for dtx?");
}

static const struct m0_dtm_history_type_ops dtx_htype_ops = {
	.hito_find = dtx_find
};

enum {
	M0_DTM_HTYPE_DTX = 8
};

M0_INTERNAL const struct m0_dtm_history_type m0_dtm_dtx_htype = {
	.hit_id   = M0_DTM_HTYPE_DTX,
	.hit_name = "distributed transaction",
	.hit_ops  = &dtx_htype_ops
};

static void dtx_id(const struct m0_dtm_history *history, struct m0_uint128 *id)
{
	struct m0_dtm_dtx_party *pa;

	pa = container_of(history, struct m0_dtm_dtx_party, pa_ch.ch_history);
	id->u_hi = 0;
	id->u_lo = pa->pa_dtx->dt_id;
}

static void dtx_fixed(struct m0_dtm_history *history)
{
	struct m0_dtm_dtx_party *pa;
	struct m0_dtm_dtx       *dx;

	pa = container_of(history, struct m0_dtm_dtx_party, pa_ch.ch_history);
	dx = pa->pa_dtx;
	M0_ASSERT(dx->dt_nr_fixed < dx->dt_nr);
	if (++dx->dt_nr_fixed == dx->dt_nr) {
	}
}

static const struct m0_dtm_history_ops dtx_ops = {
	.hio_type       = &m0_dtm_dtx_htype,
	.hio_id         = &dtx_id,
	.hio_persistent = (void *)&dtx_noop,
	.hio_fixed      = &dtx_fixed,
};

static inline struct m0_dtm_history *pa_history(struct m0_dtm_dtx_party *pa)
{
	return &pa->pa_ch.ch_history;
}

static struct m0_dtm_controlh *dtx_get(struct m0_dtm_dtx *dtx,
				       struct m0_dtm_remote *dtm)
{
	uint32_t                 i;
	struct m0_dtm_dtx_party *pa;

	for (i = 0, pa = dtx->dt_party; i < dtx->dt_nr; ++i, ++pa) {
		if (pa_history(pa)->h_dtm == dtm)
			return &pa->pa_ch;
	}
	M0_ASSERT(dtx->dt_nr < dtx->dt_nr_max);
	m0_dtm_controlh_init(&pa->pa_ch, dtx->dt_dtm);
	pa_history(pa)->h_dtm = dtm;
	pa_history(pa)->h_ops = &dtx_ops;
	pa->pa_dtx = dtx;
	dtx->dt_nr++;
	return &pa->pa_ch;
}

/** @} end of dtm group */

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
