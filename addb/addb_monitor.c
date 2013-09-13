/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Rohan Puri <rohan_puri@xyratex.com>
 * Original creation: 06/14/2013
 */

/**
   <!-- 06/13/2013 -->
   @page ADDB-MON-INFRA-DLD ADDB monitoring infrastructure  Detailed Design

   Refer to the @ref ADDB-DLD "ADDB Detailed Design"
   for the ADDB design requirements.

   - @ref ADDB-MON-INFRA-DLD-ovw
   - @ref ADDB-MON-INFRA-DLD-def
   - @ref ADDB-MON-INFRA-DLD-req
   - @ref ADDB-MON-INFRA-DLD-highlights
   - @subpage ADDB-MON-INFRA-DLD-fspec "Functional Specification" <!-- Note @subpage -->
   - @ref ADDB-MON-INFRA-DLD-lspec
      - @ref ADDB-MON-INFRA-DLD-lspec-comps
      - @ref ADDB-MON-INFRA-DLD-lspec-thread
      - @ref ADDB-MON-INFRA-DLD-lspec-state
      - @ref ADDB-MON-INFRA-DLD-lspec-numa
   - @ref ADDB-MON-INFRA-ut
   - @ref ADDB-MON-INFRA-it
   - @ref ADDB-MON-INFRA-st
   - @ref ADDB-MON-INFRA-O
   - @ref ADDB-MON-INFRA-ref

   <hr>
   @section ADDB-MON-INFRA-DLD-ovw Overview
   This design describes the ADDB monitoring infrastructure along with posting
   of addb summary records to stats service.

   <hr>
   @section ADDB-MON-INFRA-DLD-def Definitions
   - <b> Summary ADDB records</b> These are summary records that are generated
   by ADDB monitor for a particular statistic metric.
   - <b> ADDB monitor</b> These are objects present on every node in the cluster
   (client or server), they generate summary records.

   <hr>
   @section ADDB-MON-INFRA-DLD-req Requirments

   The following requirements are fully described in
   - @b r.addb.monitor.remove.runtime
   - @b r.addb.monitor.summary.post-stats-records
   - @b r.addb.monitor.summary.post-stats-addb-records
   - @b @todo r.addb.monitor.summary.post-exception-records
   - @b r.addb.monitor.nesting

   <hr>
   @section ADDB-MON-INFRA-DLD-highlights Design Highlights

   <hr>
   @section ADDB-MON-INFRA-DLD-lspec Logical Specification
   - @ref ADDB-MON-INFRA-DLD-lspec-comps
   - @ref ADDB-DLD-CNTR-lspec-thread
   - @ref ADDB-DLD-CNTR-lspec-state
   - @ref ADDB-DLD-CNTR-lspec-numa

   @subsection ADDB-MON-INFRA-DLD-lspec-comps ADDB Monitor infrastructure Overview

   ADDB monitors are filters which monitor the type/s of addb records they are
   interested in. They generate & maintain summary data from this monitoring.
   This summary data structure is monitor specific and is stored using request
   handlers locker data structure.
   This summary data is sent periodically to the stats service through a fop &
   also as addb records to the addb service if present on client or to the addb
   stob if present on server. This is done through enhancing the
   functionality of addb pfom @ref ADDB-DLD-SVC-pstats on both the server as
   well as the client.

   @verbatim
		Global ADDB monitors list
             ____    ____    ____          ____
	    |mon1|->|mon2|->|mon3|-... -> |monn|
	    |____|  |____|  |____|        |____|


	addb_rec_1
	addb_rec_2 monitor stream    ____
	addb_rec_3----------------->|monx|->Generate/Update ADDB summary record
	    .      of addb recs     |____|
	    .
	    .
	addb_rec_n

   @endverbatim

	Periodically running fom will send all the addb summary records for
	all the monitors to the stats service & post the summary data as addb
	summary records on the globally available addb machine.

   @subsection ADDB-MON-INFRA-DLD-lspec-thread Threading and Concurrency Model

   ADDB monitors update addb summary data by processing the addb records. Also,
   ADDB stats posting fom periodically reads this addb summary data for
   each monitor and sends this data to stats service. Hence,
   need to synchronize amongst these two accesses of addb summary data. This
   is achieved by adding m0_mutex to m0_addb_sum_rec structure.

   @subsection ADDB-MON-INFRA-DLD-lspec-state FOM states for sending stats fop

   On the mero server (m0d), addb pfom's ADDB_PFOM_PHASE_POST state does this.
   For the mero client (m0t1fs), there would be a simple fom that would do this.

   @subsection ADDB-MON-INFRA-DLD-lspec-numa NUMA optimizations
   These foms that post stats summary records are executed in the same locality.


   @section ADDB-MON-INFRA-ut Unit Tests
   -# Test init, add, delete a particular monitor in user-space
           & verify the summary data.
   -# Test init, add, delete a particular monitor in kernel-space
           & verify the summary data.

   @section ADDB-MON-INFRA-it Integration Tests
   Verify the summary records sent to stats service from a node.

   @section ADDB-MON-INFRA-st
   Add real monitor for fom rate, check the fom rate by querying
   the stats service and verify the data.

   @section ADDB-MON-INFRA-O Analysis
   Monitor related information (list of monitors, their specific data)
   are all kept in memory. Monitor implementors are advised to take
   locks on monitor specific data viz. m0_addb_sum_rec:asr_mutex
   only when they require data consistency.

   @section ADDB-MON-INFRA-ref
   - <a href="https://docs.google.com/a/xyratex.com/document/d/
14uPeE0mNkRu3oF32Ys_EnpvSZtGWbf8hviPHTBTOXso/edit">
   HLD of ADDB Monitoring</a>

*/

#undef M0_ADDB_CT_CREATE_DEFINITION
#define M0_ADDB_CT_CREATE_DEFINITION
#undef M0_ADDB_RT_CREATE_DEFINITION
#define M0_ADDB_RT_CREATE_DEFINITION
#include "stats/stats_addb.h"

#include "addb/addb_monitor.h"
#include "rpc/rpclib.h"
#ifndef __KERNEL__
#include "mero/setup.h"
#endif

M0_TL_DESCR_DEFINE(addb_mon, "addb monitors list", M0_INTERNAL,
		   struct m0_addb_monitor, am_linkage, am_magic,
		   M0_ADDB_MONITOR_LIST_LINK_MAGIC,
		   M0_ADDB_MONITOR_LIST_HEAD_MAGIC);

M0_TL_DEFINE(addb_mon, M0_INTERNAL, struct m0_addb_monitor);

struct m0_addb_ctx m0_addb_monitors_mod_ctx;

#ifndef __KERNEL__
static int addb_mon_rpc_client_connect(struct m0_rpc_conn    *conn,
				       struct m0_rpc_machine *rpc_mach,
				       const char            *remote_addr,
				       uint64_t               max_rpcs_in_flight)
{
	struct m0_net_end_point *ep;
	int                      rc;

	rc = m0_net_end_point_create(&ep, &rpc_mach->rm_tm, remote_addr);
	if (rc != 0)
		M0_RETURN(rc);
	rc = m0_rpc_conn_create(conn, ep, rpc_mach, max_rpcs_in_flight,
				M0_TIME_NEVER);
	m0_net_end_point_put(ep);

	M0_RETURN(rc);
}

M0_INTERNAL int m0_addb_monitor_stats_svc_conn_init(struct m0_reqh *reqh)
{
	int                           rc = 0;
	const char                    *stats_svc_ep;
	struct m0_mero                *mero;
	struct m0_rpc_machine         *rmach;
	struct m0_addb_monitoring_ctx *mon_ctx;

	M0_PRE(reqh != NULL);

	mon_ctx = &reqh->rh_addb_monitoring_ctx;

	mero = m0_cs_ctx_get(reqh);
	rmach = m0_reqh_rpc_mach_tlist_head(&reqh->rh_rpc_machines);
	M0_ASSERT(mero != NULL);
	M0_ASSERT(rmach != NULL);

	stats_svc_ep = mero->cc_stats_svc_epx.ex_endpoint;

	STATS_ALLOC_PTR(mon_ctx->amc_stats_conn, &m0_addb_monitors_mod_ctx,
			SVC_CONN_ESTABLISH_1);
	if (mon_ctx->amc_stats_conn == NULL)
		M0_RETURN(-ENOMEM);
	rc = addb_mon_rpc_client_connect(mon_ctx->amc_stats_conn,
					 rmach,
					 stats_svc_ep,
					 ADDB_STATS_MAX_RPCS_IN_FLIGHT);

	if (rc != 0) {
		STATS_ADDB_FUNCFAIL(rc, SVC_CONN_ESTABLISH_2,
				    &m0_addb_monitors_mod_ctx);
		m0_free(mon_ctx->amc_stats_conn);
		M0_RETURN(rc);
	}
	mon_ctx->amc_stats_ep = stats_svc_ep;
	return rc;
}
#endif

M0_INTERNAL void m0_addb_monitor_setup(struct m0_reqh     *reqh,
				       struct m0_rpc_conn *conn,
				       const char         *ep)
{
	struct m0_addb_monitoring_ctx *mon_ctx;

	M0_PRE(reqh != NULL && conn != NULL && ep != NULL);

	mon_ctx = &reqh->rh_addb_monitoring_ctx;

	/** Only assign stats ep for the first mentioned conf object */
	if (mon_ctx->amc_stats_ep == NULL) {
		mon_ctx->amc_stats_ep      = ep;
		mon_ctx->amc_stats_conn    = conn;
	}
}

M0_INTERNAL int m0_addb_monitors_init(struct m0_reqh *reqh)
{
	int                            rc = 0;
	struct m0_addb_monitoring_ctx *mon_ctx;
	const struct m0_addb_ctx_type *act;

	M0_PRE(reqh != NULL);

	mon_ctx = &reqh->rh_addb_monitoring_ctx;

	addb_mon_tlist_init(&mon_ctx->amc_list);
	m0_mutex_init(&mon_ctx->amc_mutex);
	act = m0_addb_ctx_type_lookup(M0_ADDB_CTXID_MONITORS_MOD);
	if (act == NULL) {
		m0_addb_ctx_type_register(&m0_addb_ct_monitors_mod);
		M0_ADDB_CTX_INIT(&m0_addb_gmc, &m0_addb_monitors_mod_ctx,
				 &m0_addb_ct_monitors_mod, &m0_addb_proc_ctx);
	}

	mon_ctx->amc_magic = M0_ADDB_MONITOR_CTX_MAGIC;

	return rc;
}

M0_INTERNAL bool m0_addb_monitor_invariant(struct m0_addb_monitor *mon)
{
	return mon != NULL && mon->am_ops != NULL;
}

M0_INTERNAL bool m0_addb_mon_ctx_invariant(struct m0_addb_monitoring_ctx *ctx)
{
	return ctx->amc_magic == M0_ADDB_MONITOR_CTX_MAGIC;
}

M0_INTERNAL void m0_addb_monitor_init(struct m0_addb_monitor           *monitor,
				      const struct m0_addb_monitor_ops *mon_ops)
{
	M0_PRE(monitor != NULL);
	M0_PRE(mon_ops != NULL);

	monitor->am_ops = mon_ops;
	addb_mon_tlink_init(monitor);
}

M0_INTERNAL void m0_addb_monitor_sum_rec_init(struct m0_addb_sum_rec        *rec,
					      const struct m0_addb_rec_type *rt,
					      uint64_t                      *md,
					      size_t                         nr)
{
	M0_PRE(rec != NULL && rt != NULL && md != NULL &&
	       nr > 0);

	m0_mutex_init(&rec->asr_mutex);
	rec->asr_rec.ss_id              = rt->art_id;
	rec->asr_rec.ss_data.au64s_nr   = nr;
	rec->asr_rec.ss_data.au64s_data = md;
}

M0_INTERNAL void m0_addb_monitor_sum_rec_fini(struct m0_addb_sum_rec *sum_rec)
{
	M0_PRE(sum_rec != NULL);

	sum_rec->asr_rec.ss_id              = M0_STATS_ID_UNDEFINED;
	sum_rec->asr_rec.ss_data.au64s_nr   = 0;
	sum_rec->asr_rec.ss_data.au64s_data = NULL;

	m0_mutex_fini(&sum_rec->asr_mutex);
}

M0_INTERNAL void m0_addb_monitor_fini(struct m0_addb_monitor *monitor)
{
	M0_PRE(m0_addb_monitor_invariant(monitor));

	monitor->am_ops = NULL;
	addb_mon_tlink_fini(monitor);
}

M0_INTERNAL void m0_addb_monitor_add(struct m0_reqh               *reqh,
				     struct m0_addb_monitor       *monitor)
{
	M0_PRE(reqh != NULL);
	M0_PRE(m0_addb_monitor_invariant(monitor));

	m0_mutex_lock(&reqh->rh_addb_monitoring_ctx.amc_mutex);
	addb_mon_tlist_add_tail(&reqh->rh_addb_monitoring_ctx.amc_list, monitor);
	m0_mutex_unlock(&reqh->rh_addb_monitoring_ctx.amc_mutex);
}

M0_INTERNAL void m0_addb_monitor_del(struct m0_reqh         *reqh,
				     struct m0_addb_monitor *monitor)
{
	struct m0_reqh_service *svc;
	struct addb_svc        *addb_svc;

	M0_PRE(m0_addb_monitor_invariant(monitor));

	svc = m0_reqh_service_find(&m0_addb_svc_type, reqh);
	if (svc != NULL) {
		addb_svc = bob_of(svc, struct addb_svc, as_reqhs,
				  &addb_svc_bob);

		m0_mutex_lock(&reqh->rh_addb_monitoring_ctx.amc_mutex);
		if (addb_svc->as_pfom.pf_mon == monitor) {
			addb_svc->as_pfom.pf_mon = NULL;
		}
		m0_mutex_unlock(&reqh->rh_addb_monitoring_ctx.amc_mutex);
	}
	m0_mutex_lock(&reqh->rh_addb_monitoring_ctx.amc_mutex);
	addb_mon_tlist_del(monitor);
	m0_mutex_unlock(&reqh->rh_addb_monitoring_ctx.amc_mutex);
}

#ifndef __KERNEL__
M0_INTERNAL void m0_addb_monitor_stats_svc_conn_fini(struct m0_reqh *reqh)
{
	int                            rc;
	struct m0_addb_monitoring_ctx *mon_ctx;

	M0_PRE(reqh != NULL);

	mon_ctx = &reqh->rh_addb_monitoring_ctx;
	rc = m0_rpc_conn_destroy(mon_ctx->amc_stats_conn, M0_TIME_NEVER);
	if (rc != 0)
		STATS_ADDB_FUNCFAIL(rc, SVC_CONN_FINI_2,
				    &m0_addb_monitors_mod_ctx);
	mon_ctx->amc_stats_conn = NULL;
	mon_ctx->amc_stats_ep   = NULL;
}
#endif

M0_INTERNAL void m0_addb_monitors_fini(struct m0_reqh *reqh)
{
	struct m0_addb_monitor *mon;

	m0_tl_for(addb_mon, &reqh->rh_addb_monitoring_ctx.amc_list, mon) {
		m0_addb_monitor_del(reqh, mon);
		m0_addb_monitor_fini(mon);
	} m0_tl_endfor;

	addb_mon_tlist_fini(&reqh->rh_addb_monitoring_ctx.amc_list);
	m0_mutex_fini(&reqh->rh_addb_monitoring_ctx.amc_mutex);
	reqh->rh_addb_monitoring_ctx.amc_magic = 0;
}

#define SUM_SIZE(sum) (sum->asr_rec.ss_data.au64s_nr * sizeof(uint64_t))

enum {
	/**
	 * Maximal number of records (dirty or clean) scanned in one
	 * invocation of m0_addb_monitor_summaries_post(). This limits
	 * processor consumption.
	 */
	SCANNED_LIMIT = 16 * 1024,
	/**
	 * Maximal number of fops that can be sent in one invocation of
	 * m0_addb_monitor_summaries_post().
	 */
	SENT_LIMIT = 3
};

/**
 * How many records to send in one fop
 */
static unsigned stats_batch = 1024;

static void addb_mon_fop_free(struct m0_stats_update_fop *fop_data,
			      uint32_t                    stats_nr)
{
	struct m0_stats_recs *stats;

	M0_PRE(fop_data != NULL);
	stats = &fop_data->suf_stats;
	if (fop_data->suf_stats.sf_stats != NULL) {
		int i;

		for (i = 0; i < stats_nr; ++i)
			m0_free(stats->sf_stats[i].ss_data.au64s_data);
		m0_free(stats->sf_stats);
	}
	m0_free(fop_data);
}

static void addb_monitor_stats_fop_release(struct m0_ref *ref)
{
	struct m0_fop *fop = container_of(ref, struct m0_fop, f_ref);

	m0_fop_fini(fop);
	m0_free(fop);
}

static int addb_monitor_stats_fop_send(struct m0_stats_update_fop *fop_data,
				       struct m0_rpc_conn         *conn)
{
	struct m0_fop      *stats_update_fop;
	struct m0_rpc_item *item;
	int                 rc;

	M0_ALLOC_PTR(stats_update_fop);
	if (stats_update_fop == NULL)
		M0_RETERR(-ENOMEM, "stats update fop");

	m0_fop_init(stats_update_fop, &m0_fop_stats_update_fopt,
		    (void *) fop_data, addb_monitor_stats_fop_release);

	item             = &stats_update_fop->f_item;
	item->ri_prio    = M0_RPC_ITEM_PRIO_MIN;
	/**
	 * @todo: Need to check what should be optimal value for
	 * item deadline. Should be done during performance optimization.
	 */
	item->ri_deadline = 0;

	rc = m0_rpc_oneway_item_post(conn, item);
	M0_ASSERT(item->ri_rmachine);
	m0_sm_group_lock(&item->ri_rmachine->rm_sm_grp);
	m0_fop_put(stats_update_fop);
	m0_sm_group_unlock(&item->ri_rmachine->rm_sm_grp);

	return rc;
}

M0_INTERNAL int m0_addb_monitor_summaries_post(struct m0_reqh       *reqh,
					       struct addb_post_fom *fom)
{
	struct m0_tl                  *mon_list;
	struct m0_addb_monitoring_ctx *mon_ctx;
	/* Continue scanning from the place we left last time */
	struct m0_addb_monitor        *mon       = fom->pf_mon;
	struct m0_stats_update_fop    *fop_data  = NULL;
	uint32_t                       stats_nr  = 0;
	uint32_t                       used      = 0;
	uint32_t                       scanned   = 0;
	uint32_t                       sent      = 0;
	uint32_t                       result    = 0;
	struct m0_addb_ctx            *cv[]      = {&m0_addb_monitors_mod_ctx,
						    NULL};

	M0_PRE(reqh != NULL);

	mon_ctx = &reqh->rh_addb_monitoring_ctx;
	M0_ASSERT(mon_ctx->amc_stats_conn != NULL);
	mon_list = &mon_ctx->amc_list;

	while (scanned < SCANNED_LIMIT && sent < SENT_LIMIT) {
		struct m0_addb_sum_rec *sum;
		struct m0_stats_sum    *rec;
		void                   *data;

		/* end of list reached, wrap around or the list is empty */
		if (mon == NULL) {
			m0_mutex_lock(&mon_ctx->amc_mutex);
			mon = addb_mon_tlist_head(mon_list);
			m0_mutex_unlock(&mon_ctx->amc_mutex);
			if (mon == NULL ||
			/* if the entire list was scanned and not a single
			 * dirty record found, stop. */
			    (scanned > 0 && fop_data == NULL))
				break;
		}
		++scanned;
		sum = mon->am_ops->amo_sum_rec(mon, reqh);
		m0_mutex_lock(&mon_ctx->amc_mutex);
		mon = addb_mon_tlist_next(mon_list, mon);
		m0_mutex_unlock(&mon_ctx->amc_mutex);
		if (sum == NULL || !sum->asr_dirty)
			continue;
		++stats_nr;
		if (fop_data == NULL) {
			M0_ALLOC_PTR(fop_data);
			if (fop_data != NULL)
				M0_ALLOC_ARR(fop_data->suf_stats.sf_stats,
					     stats_batch);
		}
		if (M0_FI_ENABLED("mem_err")) {
			data = NULL;
			goto err1_injected;
		}
		data = m0_alloc(SUM_SIZE(sum));
err1_injected:
		if (fop_data == NULL || fop_data->suf_stats.sf_stats == NULL ||
		    data == NULL) {
			result = -ENOMEM;
			addb_mon_fop_free(fop_data, stats_nr);
			fop_data = NULL;
			break;
		}
		rec = &fop_data->suf_stats.sf_stats[used++];
		rec->ss_id = sum->asr_rec.ss_id;
		rec->ss_data.au64s_nr = sum->asr_rec.ss_data.au64s_nr;
		rec->ss_data.au64s_data = data;
		m0_mutex_lock(&sum->asr_mutex);
		memcpy(rec->ss_data.au64s_data,
		       sum->asr_rec.ss_data.au64s_data, SUM_SIZE(sum));
		sum->asr_dirty = false;
		m0_mutex_unlock(&sum->asr_mutex);
		/**
		 * Post summary record on global machine too.
		 */
		M0_ADDB_MONITOR_STATS_POST(&m0_addb_gmc,
					   m0_addb_rec_type_lookup(rec->ss_id),
					   cv, rec);
		if (used == stats_batch) {
			fop_data->suf_stats.sf_nr = stats_batch;
			result =
			addb_monitor_stats_fop_send(fop_data,
					            mon_ctx->amc_stats_conn);
			if (result != 0)
				goto out;
			used     = 0;
			fop_data = NULL;
			stats_nr = 0;
			++sent;
		}
	}
	if (fop_data != NULL) {
		fop_data->suf_stats.sf_nr = stats_nr;
		result = addb_monitor_stats_fop_send(fop_data,
					             mon_ctx->amc_stats_conn);
		if (result != 0)
			goto out;
	}
	fom->pf_mon = mon;
out:
	return result;
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
