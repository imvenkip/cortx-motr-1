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
 * Original author: Rajanikant Chirmade <rajanikant_chirmade@xyratex.com>
 * Original creation date: 09/16/2013
 */

#include "errno.h"
#include "stdio.h"

#include "lib/memory.h"
#include "lib/trace.h"
#include "fop/fop.h"
#include "rpc/rpclib.h"
#include "stats/stats_fops.h"
#include "stats/stats_fops_xc.h"

static struct m0_stats_recs *stats_recs_dup(struct m0_stats_recs *stats_recs)
{
	int                   i;
	struct m0_stats_recs *recs;

	M0_PRE(stats_recs != NULL);
	M0_PRE(stats_recs->sf_nr > 0 && stats_recs->sf_stats != NULL);

	M0_ALLOC_PTR(recs);
	if (recs == NULL)
		goto error;

	recs->sf_nr = stats_recs->sf_nr;
	M0_ALLOC_ARR(recs->sf_stats, recs->sf_nr);
	if (recs->sf_stats == NULL)
		goto free_recs;

	for (i = 0; i < recs->sf_nr; ++i) {
		/* if stats type not defined. */
		if (stats_recs->sf_stats[i].ss_data.au64s_nr <= 0)
			continue;

		recs->sf_stats[i].ss_id = stats_recs->sf_stats[i].ss_id;
		recs->sf_stats[i].ss_data.au64s_nr =
			stats_recs->sf_stats[i].ss_data.au64s_nr;
		M0_ALLOC_ARR(recs->sf_stats[i].ss_data.au64s_data,
			     recs->sf_stats[i].ss_data.au64s_nr);
		if (recs->sf_stats[i].ss_data.au64s_data == NULL)
			goto free_stats;

		memcpy(recs->sf_stats[i].ss_data.au64s_data,
		       stats_recs->sf_stats[i].ss_data.au64s_data,
		       recs->sf_stats[i].ss_data.au64s_nr * sizeof (uint64_t));
	}

	return recs;

free_stats:
	for(; i >= 0; --i) {
		if (recs->sf_stats[i].ss_data.au64s_data != NULL)
			m0_free(recs->sf_stats[i].ss_data.au64s_data);
	}
	m0_free(recs->sf_stats);
free_recs:
	m0_free(recs);
error:
	return NULL;
}

static struct m0_fop *query_fop_alloc(struct m0_addb_uint64_seq *stats_ids)
{
	struct m0_fop             *fop;
	struct m0_stats_query_fop *qfop;

	M0_ALLOC_PTR(fop);
	if (fop == NULL)
		goto error;

	M0_ALLOC_PTR(qfop);
	if (qfop == NULL)
		goto free_fop;

	qfop->sqf_ids.au64s_nr = stats_ids->au64s_nr;
	M0_ALLOC_ARR(qfop->sqf_ids.au64s_data, qfop->sqf_ids.au64s_nr);
	if (qfop->sqf_ids.au64s_data == NULL)
		goto free_qfop;

	memcpy(qfop->sqf_ids.au64s_data, stats_ids->au64s_data,
	       stats_ids->au64s_nr * sizeof (uint64_t));

	m0_fop_init(fop, &m0_fop_stats_query_fopt, (void *)qfop,
		    m0_stats_query_fop_release);

	return fop;

free_qfop:
	m0_free(qfop);
free_fop:
	m0_free(fop);
error:
	return NULL;
}

int m0_stats_query(struct m0_rpc_session      *session,
		   struct m0_addb_uint64_seq  *stats_ids,
                   struct m0_stats_recs      **stats)
{
	int                            rc;
	struct m0_fop                 *fop;
	struct m0_fop                 *rfop;
	struct m0_rpc_item            *item;
	struct m0_stats_query_rep_fop *qrfop;

	M0_PRE(session != NULL);
	M0_PRE(stats_ids != NULL && stats_ids->au64s_nr != 0);
	M0_PRE(stats != NULL);

	fop = query_fop_alloc(stats_ids);
	if (fop == NULL)
		return -ENOMEM;

	item = &fop->f_item;
	rc = m0_rpc_client_call(fop, session, NULL, 0);
	if (rc != 0) {
		m0_fop_put(fop);
		return rc;
	}

	rfop  = m0_rpc_item_to_fop(item->ri_reply);
	qrfop = m0_stats_query_rep_fop_get(rfop);

	*stats = stats_recs_dup(&qrfop->sqrf_stats);

	m0_sm_group_lock(&item->ri_rmachine->rm_sm_grp);
	m0_fop_put(fop);
	m0_sm_group_unlock(&item->ri_rmachine->rm_sm_grp);
	return rc;
}

void m0_stats_free(struct m0_stats_recs *stats)
{
	struct m0_xcode_obj obj = {
                .xo_type = m0_stats_recs_xc,
                .xo_ptr  = stats,
	};

	m0_xcode_free_obj(&obj);
}
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
