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
 * Original creation date: 10/25/2012
 */

/* This file is designed to be included by addb/ut/addb_ut.c */

static void addb_ut_cntr_test(void)
{
	static struct m0_addb_counter c[10];
	struct m0_addb_rec_type      *cntrrtp[10] = {
#define CNTRRTP(n) &m0__addb_ut_rt_cntr ## n
		CNTRRTP(0), CNTRRTP(1), CNTRRTP(2), CNTRRTP(3),
		CNTRRTP(4), CNTRRTP(5), CNTRRTP(6), CNTRRTP(7),
		CNTRRTP(8), CNTRRTP(9),
#undef CNTRRTP
	};
	static uint64_t               update_cntrs[10] = {
		0, 10, 20, 30, 40, 50, 60, 70, 80, 90 };
	static uint64_t               min = 0;
	static uint64_t               max = 90;
	static uint64_t               total = 450;
	static uint64_t               sum_sq = 28500;
	static uint64_t               nr = 10;
	uint64_t                     *lower_boundp = NULL;
	int                           rc;
	int                           i;
	int                           j;

	/*
	 * TEST
	 * Fini of null counter does not fail.
	 */
	M0_SET0(&c[0]);
	m0_addb_counter_fini(&c[0]);

	/*
	 * TEST
	 * Counter usage
	 */
	for (i = 0; i < ARRAY_SIZE(cntrrtp); ++i) {
		cntrrtp[i]->art_id = addb_rt_max_id + 1;
		m0_addb_rec_type_register(cntrrtp[i]);
	}
	for (i = 0; i < ARRAY_SIZE(c); ++i) {
		rc = m0_addb_counter_init(&c[i], cntrrtp[i]);
		M0_UT_ASSERT(rc == 0);
		M0_UT_ASSERT(c[i].acn_magic == M0_ADDB_CNTR_MAGIC);
		M0_UT_ASSERT(c[i].acn_rt == cntrrtp[i]);
	}
	for (i = 1; i < ARRAY_SIZE(c); ++i) {
		M0_UT_ASSERT(c[i].acn_rt->art_rf_nr == i);
		lower_boundp = (uint64_t *)(c[i].acn_rt->art_rf);
		for (j = 0; j < c[i].acn_rt->art_rf_nr; ++j)
			lower_boundp[j] = (j + 1) * 10;
	}
	for (i = 0; i < ARRAY_SIZE(c); ++i) {
		for (j = 0; j < ARRAY_SIZE(update_cntrs); ++j) {
			rc = m0_addb_counter_update(&c[i], update_cntrs[j]);
			M0_UT_ASSERT(rc == 0);
			M0_UT_ASSERT(m0_addb_counter_nr(&c[i]) == j + 1);
		}
	}
	for (i = 0; i < ARRAY_SIZE(c); ++i) {
		if (c[i].acn_rt->art_rf_nr != 0)
			for (j = 0; j < c[i].acn_rt->art_rf_nr + 1; ++j)
				M0_UT_ASSERT(c[i].acn_data->acd_hist[j] >= 1);
		M0_UT_ASSERT(m0_addb_counter_nr(&c[i]) == nr);
		M0_UT_ASSERT(c[i].acn_data->acd_nr     == nr);
		M0_UT_ASSERT(c[i].acn_data->acd_min    == min);
		M0_UT_ASSERT(c[i].acn_data->acd_max    == max);
		M0_UT_ASSERT(c[i].acn_data->acd_total  == total);
		M0_UT_ASSERT(c[i].acn_data->acd_sum_sq == sum_sq);
	}
	/*
	 * TEST
	 * counter overflow
	 */
	m0_addb_counter_fini(&c[1]);
	rc = m0_addb_counter_init(&c[1], cntrrtp[1]);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(c[1].acn_magic == M0_ADDB_CNTR_MAGIC);
	M0_UT_ASSERT(c[1].acn_rt == cntrrtp[1]);
	for (i = 0; i < 15; ++i) {
		rc = m0_addb_counter_update(&c[1], 1 << 30);
		M0_UT_ASSERT(rc == 0);
	}
	rc = m0_addb_counter_update(&c[1], 1 << 30);
	M0_UT_ASSERT(rc == -EOVERFLOW);
	M0_UT_ASSERT(c[1].acn_data->acd_nr == 15);
	M0_UT_ASSERT(c[1].acn_data->acd_total == (unsigned long long)
		     (1 << 30) * 15);
	M0_UT_ASSERT(c[1].acn_data->acd_min == (1 << 30));
	M0_UT_ASSERT(c[1].acn_data->acd_max == (1 << 30));

	/*
	 * TEST
	 * counter_reset(): success case
	 */
	m0__addb_counter_reset(&c[1]);
	M0_UT_ASSERT(c[1].acn_magic == M0_ADDB_CNTR_MAGIC);
	M0_UT_ASSERT(c[1].acn_rt == cntrrtp[1]);
	M0_UT_ASSERT(c[1].acn_data->acd_seq == 1);
	M0_UT_ASSERT(c[1].acn_data->acd_nr == 0);
	M0_UT_ASSERT(c[1].acn_data->acd_total == 0);
	M0_UT_ASSERT(c[1].acn_data->acd_min == 0);
	M0_UT_ASSERT(c[1].acn_data->acd_max == 0);
	M0_UT_ASSERT(c[1].acn_data->acd_sum_sq == 0);
	M0_UT_ASSERT(c[1].acn_data->acd_hist[0] == 0);
	M0_UT_ASSERT(c[1].acn_data->acd_hist[1] == 0);

	/*
	 * cleanup
	 */
	for (i = 0; i < ARRAY_SIZE(c); ++i) {
		m0_addb_counter_fini(&c[i]);
		M0_UT_ASSERT(c[i].acn_magic == 0);
		M0_UT_ASSERT(c[i].acn_data == 0);
	}
	for (i = 0; i < ARRAY_SIZE(c); ++i)
		addb_rt_tlist_del(cntrrtp[i]);
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
