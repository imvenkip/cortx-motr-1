/* -*- C -*- */
/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Rajanikant Chirmade <Rajanikant_Chirmade@xyratex.com>
 * Original creation date: 01/02/2014
 */

#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/time.h"
#include "lib/timer.h"
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_FOP
#include "lib/trace.h"
#include "fop/fop_addb.h"
#include "addb/addb_monitor.h"
#include "fop/fop_rate_monitor.h"

/**
 * @addtogroup fom
 * @{
 */
struct m0_reqh;

enum {
	INTERVAL_SECOND = 1,
};

static int key;
static void timer_rearm(struct m0_sm_group *grp, struct m0_sm_ast *ast);

static struct fop_rate_stats_sum_rec {
	uint64_t ssr_fop_rate;
} fop_rate_stats_sum;

M0_BASSERT((sizeof(fop_rate_stats_sum) / sizeof(uint64_t)) ==
	   M0_FOP_RATE_MON_DATA_NR);

static void __timer_fini(struct m0_timer *timer)
{
	m0_timer_stop(timer);
	m0_timer_fini(timer);
}

static unsigned long timer_callback(unsigned long arg)
{
	struct m0_fop_rate_monitor *mon = (struct m0_fop_rate_monitor *)arg;

	/**
	 * @todo Provide a proper fix in addb_counter
	 * refer MERO-136
	 */
	if (mon->frm_count == 0)
		return 0;

	m0_addb_counter_update(&mon->frm_addb_ctr, mon->frm_count);
	mon->frm_count = 0;
	m0_sm_ast_post(&mon->frm_loc->fl_group, &mon->frm_ast);

	return 0;
}

static int timer_arm(struct m0_fop_rate_monitor *mon)
{
	int              result;
	m0_time_t        expire;
	struct m0_timer *timer = &mon->frm_timer;

	result = m0_timer_init(timer, M0_TIMER_SOFT, NULL, timer_callback,
			       (unsigned long)mon);
	if (result != 0)
		return result;

	expire = m0_time_from_now(INTERVAL_SECOND, 0);
	m0_timer_start(timer, expire);

	return 0;
}

static void timer_rearm(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	int                         result;
	struct m0_fop_rate_monitor *monitor;

	monitor = container_of(ast, struct m0_fop_rate_monitor, frm_ast);

	__timer_fini(&monitor->frm_timer);
	result = timer_arm(monitor);

	if (result != 0)
		M0_LOG(M0_WARN, "Failed to re-arm fop_rate timer.");
}

static struct m0_fop_rate_monitor *
monitor_to_fmonitor(struct m0_addb_monitor *mon)
{
	M0_PRE(mon != NULL);
	return container_of(mon, struct m0_fop_rate_monitor, frm_monitor);
}

static struct m0_addb_sum_rec *monitor_sum_rec(struct m0_addb_monitor *mon,
					       struct m0_reqh         *reqh)
{
	struct m0_fop_rate_monitor *fmon = monitor_to_fmonitor(mon);

	return &fmon->frm_sum_rec;
}

static void monitor_watch(struct m0_addb_monitor   *monitor,
			  const struct m0_addb_rec *rec,
			  struct m0_reqh           *reqh)
{

	M0_PRE(monitor != NULL && reqh != NULL);

	if (m0_addb_rec_rid_make(M0_ADDB_BRT_CNTR,
			         M0_ADDB_RECID_FOP_RATE_CNTR) == rec->ar_rid) {
		struct m0_fop_rate_monitor    *fmon;
		struct fop_rate_stats_sum_rec *f_rate;
		struct m0_addb_sum_rec        *sum_rec;

		fmon = monitor_to_fmonitor(monitor);
		sum_rec = &fmon->frm_sum_rec;

		m0_mutex_lock(&sum_rec->asr_mutex);
		M0_ASSERT(sum_rec->asr_rec.ss_id == M0_ADDB_RECID_FOP_RATE);
		f_rate = (struct fop_rate_stats_sum_rec *)
			 sum_rec->asr_rec.ss_data.au64s_data;
		M0_ASSERT(f_rate != NULL);
		/*
		 * rec->ar_data.au64s_data[1] is number of samples
		 * rec->ar_data.au64s_data[2] is sum_samples
		 */
		f_rate->ssr_fop_rate =
			rec->ar_data.au64s_data[2] / rec->ar_data.au64s_data[1];
		sum_rec->asr_dirty = true;
		m0_mutex_unlock(&sum_rec->asr_mutex);
	}
}

const struct m0_addb_monitor_ops monitor_ops = {
	.amo_watch   = monitor_watch,
	.amo_sum_rec = monitor_sum_rec
};

M0_INTERNAL int m0_fop_rate_monitor_module_init(void)
{
	key = m0_fom_locality_lockers_allot();
	return 0;
}

M0_INTERNAL
int m0_fop_rate_monitor_init(struct m0_fom_locality *loc)
{
	struct m0_fop_rate_monitor *fmon;
	int                         result;

	FOP_ALLOC_PTR(fmon, FOM_RATE_MON_INIT, &m0_fop_addb_ctx);
	if (fmon == NULL)
		return M0_RC(-ENOMEM);
	result = m0_addb_counter_init(&fmon->frm_addb_ctr,
				      &m0_addb_rt_fop_rate_cntr);
	if (result != 0)
		goto err1;

	result = timer_arm(fmon);
	if (result != 0)
		goto err0;

	m0_addb_monitor_init(&fmon->frm_monitor, &monitor_ops);

	m0_addb_monitor_sum_rec_init(&fmon->frm_sum_rec, &m0_addb_rt_fop_rate,
				     fmon->frm_md, M0_FOP_RATE_MON_DATA_NR);
	fmon->frm_loc = loc;
	fmon->frm_ast.sa_cb = timer_rearm;

	m0_fom_locality_lockers_set(loc, key, fmon);
	m0_addb_monitor_add(loc->fl_dom->fd_reqh, &fmon->frm_monitor);
	return 0;

err0:
	m0_addb_counter_fini(&fmon->frm_addb_ctr);
err1:
	m0_free(fmon);
	return M0_RC(result);
}

M0_INTERNAL
void m0_fop_rate_monitor_fini(struct m0_fom_locality *loc)
{
	struct m0_fop_rate_monitor *fmon = m0_fop_rate_monitor_get(loc);

	__timer_fini(&fmon->frm_timer);
	m0_fom_locality_lockers_clear(loc, key);
	m0_addb_monitor_del(loc->fl_dom->fd_reqh, &fmon->frm_monitor);
	m0_addb_monitor_fini(&fmon->frm_monitor);
	m0_addb_counter_fini(&fmon->frm_addb_ctr);
	m0_addb_monitor_sum_rec_fini(&fmon->frm_sum_rec);
	m0_free(fmon);
}

M0_INTERNAL
struct m0_fop_rate_monitor *m0_fop_rate_monitor_get(struct m0_fom_locality *loc)
{
	return m0_fom_locality_lockers_get(loc, key);
}

/** @} endgroup fom */
#undef M0_TRACE_SUBSYSTEM
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
