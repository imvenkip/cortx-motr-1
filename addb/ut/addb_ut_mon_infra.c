/*-*- C -*- */
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
 * Original author: Rohan Puri <rohan_puri@xyratex.com>
 * Original creation date: 09/30/2013
 */

#include "stats/stats_srv.h"

enum {
	UT_ADDB_MON_TS_INIT_PAGES  = 16,
	UT_ADDB_MON_TS_MAX_PAGES   = 36,
	UT_ADDB_MON_TS_PAGE_SIZE   = 4096, /* bytes */
	UT_ADDB_MONS_NR            = 10,
};

static char *addb_mon_infra_server_argv[] = {
	"addb_mon_infra_ut", "-p", "-T", "linux", "-D", SERVER_DB_NAME,
	"-S", SERVER_STOB_NAME, "-A", SERVER_ADDB_STOB_NAME,
	"-e", SERVER_ENDPOINT, "-R", SERVER_ENDPOINT, "-s", "addb",
	"-s", "stats", "-w", "10"
};

struct m0_addb_monitor  ut_mon[UT_ADDB_MONS_NR];
uint32_t                ut_mon_data_key[UT_ADDB_MONS_NR];
struct m0_reqh         *ut_srv_reqh;
uint32_t                stats_id[UT_ADDB_MONS_NR];
struct m0_addb_ctx     *cv[4] = { NULL, &m0_addb_proc_ctx,
				     &m0_addb_node_ctx, NULL };
struct stats_svc       *stats_srv;

struct ut_monitor_sum_data1 {
	uint64_t umsd_field1;
}ut_mon_sum_data1;

struct ut_monitor_sum_data2 {
	uint64_t umsd_field1;
	uint64_t umsd_field2;
}ut_mon_sum_data2;

struct ut_monitor_sum_data3 {
	uint64_t umsd_field1;
	uint64_t umsd_field2;
	uint64_t umsd_field3;
}ut_mon_sum_data3;

struct ut_monitor_sum_data4 {
	uint64_t umsd_field1;
	uint64_t umsd_field2;
	uint64_t umsd_field3;
	uint64_t umsd_field4;
}ut_mon_sum_data4;

struct ut_monitor_sum_data5 {
	uint64_t umsd_field1;
	uint64_t umsd_field2;
	uint64_t umsd_field3;
	uint64_t umsd_field4;
	uint64_t umsd_field5;
}ut_mon_sum_data5;

struct ut_monitor_sum_data6 {
	uint64_t umsd_field1;
	uint64_t umsd_field2;
	uint64_t umsd_field3;
	uint64_t umsd_field4;
	uint64_t umsd_field5;
	uint64_t umsd_field6;
}ut_mon_sum_data6;

struct ut_monitor_sum_data7 {
	uint64_t umsd_field1;
	uint64_t umsd_field2;
	uint64_t umsd_field3;
	uint64_t umsd_field4;
	uint64_t umsd_field5;
	uint64_t umsd_field6;
	uint64_t umsd_field7;
}ut_mon_sum_data7;

struct ut_monitor_sum_data8 {
	uint64_t umsd_field1;
	uint64_t umsd_field2;
	uint64_t umsd_field3;
	uint64_t umsd_field4;
	uint64_t umsd_field5;
	uint64_t umsd_field6;
	uint64_t umsd_field7;
	uint64_t umsd_field8;
}ut_mon_sum_data8;

struct ut_monitor_sum_data9 {
	uint64_t umsd_field1;
	uint64_t umsd_field2;
	uint64_t umsd_field3;
	uint64_t umsd_field4;
	uint64_t umsd_field5;
	uint64_t umsd_field6;
	uint64_t umsd_field7;
	uint64_t umsd_field8;
	uint64_t umsd_field9;
}ut_mon_sum_data9;


void *mon_sum_data_arrp[] = { NULL,
			    &ut_mon_sum_data1,
			    &ut_mon_sum_data2,
			    &ut_mon_sum_data3,
			    &ut_mon_sum_data4,
			    &ut_mon_sum_data5,
			    &ut_mon_sum_data6,
			    &ut_mon_sum_data7,
			    &ut_mon_sum_data8,
			    &ut_mon_sum_data9
			  };
#undef MON_DATA
#define MON_DATA(n) mon_sum_data_arrp[n]

void *dprt_arrp[] = { NULL,
		      &m0__addb_ut_rt_dp1,
		      &m0__addb_ut_rt_dp2,
		      &m0__addb_ut_rt_dp3,
		      &m0__addb_ut_rt_dp4,
		      &m0__addb_ut_rt_dp5,
		      &m0__addb_ut_rt_dp6,
		      &m0__addb_ut_rt_dp7,
		      &m0__addb_ut_rt_dp8,
		      &m0__addb_ut_rt_dp9,
		    };
#undef DPRT_P
#define DPRT_P(n) dprt_arrp[n]

#undef DPRTP
#define DPRTP(n) &m0__addb_ut_rt_dp ## n

static struct m0_addb_sum_rec *ut_mon_sum_rec(struct m0_addb_monitor *mon,
				              struct m0_reqh         *reqh)
{
	struct m0_addb_sum_rec *sum_rec;
	int                     idx;

	for (idx = 1; idx < UT_ADDB_MONS_NR; ++idx) {
		if (mon == &ut_mon[idx])
			break;
	}
	sum_rec = m0_reqh_lockers_get(reqh, ut_mon_data_key[idx]);
	M0_ASSERT(sum_rec != NULL);

	return sum_rec;
}

void ut_mon_watch(struct m0_addb_monitor   *monitor,
		  const struct m0_addb_rec *rec,
		  struct m0_reqh           *reqh)
{
	struct m0_addb_sum_rec *sum_rec;
	int                     idx;

	/* Get the index */
	for (idx = 1; idx < UT_ADDB_MONS_NR; ++idx) {
		if (monitor == &ut_mon[idx])
			break;
	}
	M0_UT_ASSERT(idx < 10);

	if (m0_addb_rec_rid_to_id(rec->ar_rid) == stats_id[idx]) {
		m0_rwlock_read_lock(&reqh->rh_rwlock);
		sum_rec = m0_reqh_lockers_get(reqh, ut_mon_data_key[idx]);
		m0_rwlock_read_unlock(&reqh->rh_rwlock);
		M0_ASSERT(sum_rec != NULL);
		if (((struct ut_monitor_sum_data1 *)MON_DATA(idx))->umsd_field1
		    == 0) {
			m0_mutex_lock(&sum_rec->asr_mutex);
			memcpy(MON_DATA(idx), rec->ar_data.au64s_data,
			       idx * sizeof(uint64_t));
			sum_rec->asr_dirty = true;
			m0_mutex_unlock(&sum_rec->asr_mutex);
		}
	}
}

const struct m0_addb_monitor_ops ut_mon_ops = {
	.amo_watch   = ut_mon_watch,
	.amo_sum_rec = ut_mon_sum_rec
};

enum {
	MON_TEST_1 = 1,
	MON_TEST_2 = 2,
	MON_TEST_3 = 3,
};

static void addb_ut_mon_init(struct m0_addb_rec_type *rtype, int idx,
			     int test_no)
{
	struct m0_addb_sum_rec *sum_rec;

	M0_ALLOC_PTR(sum_rec);
	M0_ASSERT(ut_srv_reqh != NULL);

	M0_ASSERT(sum_rec != NULL);
	m0_addb_monitor_init(&ut_mon[idx], &ut_mon_ops);
	m0_addb_monitor_sum_rec_init(sum_rec, rtype,
				     (uint64_t *)MON_DATA(idx), idx);
	if (test_no == MON_TEST_1)
		ut_mon_data_key[idx] = m0_reqh_lockers_allot();
	m0_rwlock_write_lock(&ut_srv_reqh->rh_rwlock);
	m0_reqh_lockers_set(ut_srv_reqh, ut_mon_data_key[idx], sum_rec);
	m0_rwlock_write_unlock(&ut_srv_reqh->rh_rwlock);
	m0_addb_monitor_add(ut_srv_reqh, &ut_mon[idx]);
}

static void addb_ut_mon_fini(int idx)
{
	struct m0_addb_sum_rec *sum_rec;

	M0_ASSERT(ut_srv_reqh != NULL);
	sum_rec = ut_mon[idx].am_ops->amo_sum_rec(&ut_mon[idx], ut_srv_reqh);
	M0_ASSERT(sum_rec != NULL);

	m0_addb_monitor_del(ut_srv_reqh, &ut_mon[idx]);
	m0_rwlock_write_lock(&ut_srv_reqh->rh_rwlock);
	m0_reqh_lockers_clear(ut_srv_reqh, ut_mon_data_key[idx]);
	m0_rwlock_write_unlock(&ut_srv_reqh->rh_rwlock);
	m0_addb_monitor_sum_rec_fini(sum_rec);
	m0_free(sum_rec);
	m0_addb_monitor_fini(&ut_mon[idx]);
}

static void addb_post_record(struct m0_addb_mc *mc, int idx,
			     struct m0_addb_ctx *cv[])
{
	switch (idx) {
	case 1:
		M0_ADDB_POST(&ut_srv_reqh->rh_addb_mc, DPRTP(1), cv, 10);
		break;
	case 2:
		M0_ADDB_POST(&ut_srv_reqh->rh_addb_mc, DPRTP(2), cv, 10, 20);
		break;
	case 3:
		M0_ADDB_POST(&ut_srv_reqh->rh_addb_mc, DPRTP(3), cv, 10, 20,
			     30);
		break;
	case 4:
		M0_ADDB_POST(&ut_srv_reqh->rh_addb_mc, DPRTP(4), cv, 10, 20, 30,
			     40);
		break;
	case 5:
		M0_ADDB_POST(&ut_srv_reqh->rh_addb_mc, DPRTP(5), cv, 10, 20, 30,
			     40, 50);
		break;
	case 6:
		M0_ADDB_POST(&ut_srv_reqh->rh_addb_mc, DPRTP(6), cv, 10, 20, 30,
			     40, 50, 60);
		break;
	case 7:
		M0_ADDB_POST(&ut_srv_reqh->rh_addb_mc, DPRTP(7), cv, 10, 20, 30,
			     40, 50, 60, 70);
		break;
	case 8:
		M0_ADDB_POST(&ut_srv_reqh->rh_addb_mc, DPRTP(8), cv, 10, 20, 30,
			     40, 50, 60, 70, 80);
		break;
	case 9:
		M0_ADDB_POST(&ut_srv_reqh->rh_addb_mc, DPRTP(9), cv, 10, 20, 30,
			     40, 50, 60, 70, 80, 90);
		break;
	default:
		M0_UT_ASSERT(0);
	}
}

static void addb_ut_mon_verify_stats_data(struct stats_svc *stats_srv, int idx,
					  int test_no)
{
	struct m0_stats *stats;
	int              i;

	stats = m0_stats_get(&stats_srv->ss_stats, stats_id[idx]);
	M0_UT_ASSERT(stats != NULL);
	for (i = 0; i < idx; ++i) {
		if (test_no == MON_TEST_3)
		M0_UT_ASSERT(stats->s_sum.ss_data.au64s_data[i] == 0);
		else
		M0_UT_ASSERT(stats->s_sum.ss_data.au64s_data[i]
			     == ((i + 1) * 10));
	}
}

static void clear_stats(struct stats_svc *stats_srv, int idx)
{
	struct m0_stats *stats;
	int              i;

	stats = m0_stats_get(&stats_srv->ss_stats, stats_id[idx]);
	M0_UT_ASSERT(stats != NULL);
	for (i = 0; i < idx; ++i)
		stats->s_sum.ss_data.au64s_data[i] = 0;
}

static void mon_test(int test_no)
{
	m0_time_t rem;
	int	  rc;
	int	  i;

	for (i = 1; i < UT_ADDB_MONS_NR; ++i)
		addb_post_record(&ut_srv_reqh->rh_addb_mc, i, cv);
	rem = m0_time(0, 400 * 1000 * 1000);
	do {
		rc = m0_nanosleep(rem, &rem);
	} while (rc != 0);
	for (i = 1; i < UT_ADDB_MONS_NR; ++i) {
		addb_ut_mon_verify_stats_data(stats_srv, i, test_no);
		addb_ut_mon_fini(i);
	}
}

static void mon_test_1(void)
{
	struct m0_addb_rec_type   *dp;
	int                        i;

	/* Register addb record types & initialize addb monitors for them */
	for (i = 1; i < UT_ADDB_MONS_NR; ++i) {
		dp = DPRT_P(i);
		dp->art_magic = 0;
		stats_id[i] = dp->art_id = addb_rt_max_id + i;

		/* Register ADDB summary record type */
		m0_addb_rec_type_register(dp);
		addb_ut_mon_init(DPRT_P(i), i, MON_TEST_1);
	}
	mon_test(MON_TEST_1);
}

static void mon_test_2(void)
{
	int i;

	for (i = 1; i < UT_ADDB_MONS_NR; ++i) {
		addb_ut_mon_init(DPRT_P(i), i, MON_TEST_2);
	}
	mon_test(MON_TEST_2);
}

static void mon_test_3(void)
{
	int i;

	for (i = 1; i < UT_ADDB_MONS_NR; ++i) {
		addb_ut_mon_init(DPRT_P(i), i, MON_TEST_3);
	}
	mon_test(MON_TEST_3);
}

static void addb_ut_mon_infra_test(void)
{
	struct m0_reqh_service *reqh_srv;
	m0_time_t               temp_time;
	int                     i;
	int                     default_batch = stats_batch;

	/* Do not need to collect any data */
	addb_rec_post_ut_data_enabled = false;
	temp_time = addb_pfom_period;
	addb_pfom_period = M0_MKTIME(0, 1 * 1000 * 1000 * 100);
	sctx.rsx_argv = addb_mon_infra_server_argv;
	start_rpc_client_and_server();
	ut_srv_reqh = m0_cs_reqh_get(&sctx.rsx_mero_ctx);

	reqh_srv = m0_reqh_service_find(&m0_stats_svc_type, ut_srv_reqh);
	M0_UT_ASSERT(reqh_srv != NULL);
	stats_srv = container_of(reqh_srv, struct stats_svc, ss_reqhs);

	m0__addb_ut_ct0.act_magic = 0;
	m0__addb_ut_ct0.act_id = addb_ct_max_id + 1;
	m0_addb_ctx_type_register(&m0__addb_ut_ct0);

	M0_ADDB_CTX_INIT(&ut_srv_reqh->rh_addb_mc, &ctx, &m0__addb_ut_ct0,
			 &m0_addb_proc_ctx);
	cv[0] = &ctx;

	/**
	 * End to End test :-
	 * Create 9 different addb monitors add them to the system, post
	 * 9 different addb records where each one is monitored by its
	 * corresponding added monitor, generate summary records, verify
	 * that they are communicated to stats service by traversing stats list
	 * on stats service ep.
	 * This test sends only one fop.
	 */
	mon_test_1();
	for (i = 1; i < UT_ADDB_MONS_NR; ++i) {
		((struct ut_monitor_sum_data1 *)MON_DATA(i))->umsd_field1 = 0;
		clear_stats(stats_srv, i);
	}

	m0_mutex_lock(&ut_srv_reqh->rh_addb_monitoring_ctx.amc_mutex);
	stats_batch = 5;
	m0_mutex_unlock(&ut_srv_reqh->rh_addb_monitoring_ctx.amc_mutex);

	/**
	 * Add/remove monitors dynamically.
	 * This test sends 2 fops covering both fop sending cases of
	 * m0_addb_monitor_summaries_post()
	 */
	mon_test_2();
	for (i = 1; i < UT_ADDB_MONS_NR; ++i) {
		((struct ut_monitor_sum_data1 *)MON_DATA(i))->umsd_field1 = 0;
		clear_stats(stats_srv, i);
	}
	m0_fi_enable("m0_addb_monitor_summaries_post", "mem_err");

	/**
	 * This test checks for memory failure case, where no fop
	 * gets sent.
	 */
	mon_test_3();

	/* Reset to default */
	m0_mutex_lock(&ut_srv_reqh->rh_addb_monitoring_ctx.amc_mutex);
	stats_batch = default_batch;
	m0_mutex_unlock(&ut_srv_reqh->rh_addb_monitoring_ctx.amc_mutex);

	stop_rpc_client_and_server();

	/* Reset to default */
	addb_pfom_period = temp_time;

	addb_ct_tlist_del(&m0__addb_ut_ct0);
	m0__addb_ut_ct0.act_magic = 0;
	for (i = 1; i < UT_ADDB_MONS_NR; ++i) {
		addb_rt_tlist_del((struct m0_addb_rec_type *)DPRT_P(i));
		((struct m0_addb_rec_type *)DPRT_P(i))->art_magic = 0;
	}
}
#undef DPRTP
#undef MON_DATA

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
