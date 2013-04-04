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
 * Original author: Andriy Tkachuk <andriy_tkachuk@xyratex.com>
 * Original creation date: 3/25/2013
 */

/* This file is designed to be included by addb/ut/addb_ut.c */

static struct m0_sm       sm;
static struct m0_sm_group sm_group;

enum {
	STATS_DATA_SZ = sizeof(struct m0_addb_counter_data) * ARRAY_SIZE(trans)
};

static struct m0_addb_sm_counter cntr;
static uint8_t stats_data[STATS_DATA_SZ];

static void addb_ut_smcntr_test(void)
{
	/*
	 * TEST
	 * Counter usage
	 */
	m0_sm_conf_init(&sm_conf);

	m0__addb_ut_rt_smcntr0.art_id = addb_rt_max_id + 1;
	m0_addb_rec_type_register(&m0__addb_ut_rt_smcntr0);

	m0_sm_group_init(&sm_group);
	m0_sm_init(&sm, &sm_conf, ST_INIT, &sm_group);
	m0_addb_sm_counter_init(&cntr, &m0__addb_ut_rt_smcntr0,
				stats_data, sizeof(stats_data));
	m0_sm_stats_enable(&sm, &cntr);

	M0_UT_ASSERT(sm.sm_addb_stats != NULL);
	M0_UT_ASSERT(sm.sm_addb_stats->asc_magic == M0_ADDB_CNTR_MAGIC);
	M0_UT_ASSERT(sm.sm_addb_stats->asc_rt == &m0__addb_ut_rt_smcntr0);

	m0_sm_group_lock(&sm_group);

	/* Must sleep to advance the clock */
	m0_nanosleep(10000, NULL);
	m0_sm_state_set(&sm, ST_RUN);
	m0_nanosleep(10000, NULL);
	m0_sm_state_set(&sm, ST_SLEEP);
	m0_nanosleep(10000, NULL);
	m0_sm_state_set(&sm, ST_RUN);
	m0_nanosleep(10000, NULL);
	m0_sm_state_set(&sm, ST_FINI);

	M0_UT_ASSERT(m0_addb_sm_counter_nr(sm.sm_addb_stats) == 4);

	/* init -> run */
	M0_UT_ASSERT(sm.sm_addb_stats->asc_data[0].acd_nr == 1);
	M0_UT_ASSERT(sm.sm_addb_stats->asc_data[0].acd_total >= 10);
	/* init -> fail */
	M0_UT_ASSERT(sm.sm_addb_stats->asc_data[1].acd_nr == 0);
	M0_UT_ASSERT(sm.sm_addb_stats->asc_data[1].acd_total == 0);
	/* run -> sleep */
	M0_UT_ASSERT(sm.sm_addb_stats->asc_data[2].acd_nr == 1);
	M0_UT_ASSERT(sm.sm_addb_stats->asc_data[2].acd_total >= 10);
	/* sleep -> run */
	M0_UT_ASSERT(sm.sm_addb_stats->asc_data[3].acd_nr == 1);
	M0_UT_ASSERT(sm.sm_addb_stats->asc_data[3].acd_total >= 10);
	/* run -> fini */
	M0_UT_ASSERT(sm.sm_addb_stats->asc_data[4].acd_nr == 1);
	M0_UT_ASSERT(sm.sm_addb_stats->asc_data[4].acd_total >= 10);

	/*
	 * TEST
	 * counter_reset(): success case
	 */
	m0__addb_sm_counter_reset(sm.sm_addb_stats);
	M0_UT_ASSERT(m0_addb_sm_counter_nr(sm.sm_addb_stats) == 0);

	m0_addb_sm_counter_fini(&cntr);
	m0_sm_fini(&sm);

	m0_sm_group_unlock(&sm_group);

	m0_sm_group_fini(&sm_group);
	addb_rt_tlist_del(&m0__addb_ut_rt_smcntr0);
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
