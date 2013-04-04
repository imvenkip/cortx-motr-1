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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation date: 09/25/2012
 */

/* This file is designed to be included by addb/ut/addb_ut.c */

/*
 ****************************************************************************
 * addb_ut_ct_test
 ****************************************************************************
 */

/* fake contexts - their static id's overridden when temporarily registered */
M0_ADDB_CT(m0__addb_ut_ct0, 10);
M0_ADDB_CT(m0__addb_ut_ct1, 11, "A1");
M0_ADDB_CT(m0__addb_ut_ct2, 12, "A1", "A2");
M0_ADDB_CT(m0__addb_ut_ct3, 13, "A1", "A2", "A3");
M0_ADDB_CT(m0__addb_ut_ct4, 14, "A1", "A2", "A3", "A4");
M0_ADDB_CT(m0__addb_ut_ct5, 15, "A1", "A2", "A3", "A4", "A5");
M0_ADDB_CT(m0__addb_ut_ct6, 16, "A1", "A2", "A3", "A4", "A5", "A6");
M0_ADDB_CT(m0__addb_ut_ct7, 17, "A1", "A2", "A3", "A4", "A5", "A6", "A7");
M0_ADDB_CT(m0__addb_ut_ct8, 18, "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8");
M0_ADDB_CT(m0__addb_ut_ct9, 19, "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8",
	   "A9");

/* template CT - not registered */
M0_ADDB_CT(m0__addb_ut_ct_tmpl, 999); /* no inline strings */

struct addb_ut_ct_thread_arg {
	struct m0_addb_ctx_type *cts;
	size_t                   cts_nr;
	int                      start;
	int                      incr;
	struct m0_semaphore     *sem;
};

static void addb_ut_ct_thread(struct addb_ut_ct_thread_arg *ta)
{
	int i;

	m0_semaphore_down(ta->sem);
	for (i = ta->start; i < ta->cts_nr; i += ta->incr)
		m0_addb_ctx_type_register(&ta->cts[i]);
	return;
}

static void addb_ut_ct_test(void)
{
	/*
	 * TEST
	 * Verify that the UT context types are initialized as expected.
	 */

	/* verify their names */
#undef ASSERT_CTNAME
#define ASSERT_CTNAME(n) M0_UT_ASSERT(strcmp(n.act_name, #n) == 0)
	ASSERT_CTNAME(m0__addb_ut_ct0);
	ASSERT_CTNAME(m0__addb_ut_ct1);
	ASSERT_CTNAME(m0__addb_ut_ct2);
	ASSERT_CTNAME(m0__addb_ut_ct3);
	ASSERT_CTNAME(m0__addb_ut_ct4);
	ASSERT_CTNAME(m0__addb_ut_ct5);
	ASSERT_CTNAME(m0__addb_ut_ct6);
	ASSERT_CTNAME(m0__addb_ut_ct7);
	ASSERT_CTNAME(m0__addb_ut_ct8);
	ASSERT_CTNAME(m0__addb_ut_ct9);
#undef ASSERT_CTNAME

	/* verify their ids, number of fields, and field names */
	{
		struct m0_addb_ctx_type *ctp[10] = {
#undef CTP
#define CTP(n) &m0__addb_ut_ct ## n
			CTP(0), CTP(1), CTP(2), CTP(3), CTP(4),
			CTP(5), CTP(6), CTP(7), CTP(8), CTP(9),
#undef CTP
		};
		char *f[9] = {
			"A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "A9"
		};
		int i, j;

		for (i = 0; i < ARRAY_SIZE(ctp); ++i) {
			M0_UT_ASSERT(ctp[i]->act_id == i + 10);
			M0_UT_ASSERT(ctp[i]->act_cf_nr == i);
			M0_UT_ASSERT(ctp[i]->act_magic == 0);
			for (j = 0; j < i; ++j) {
				M0_UT_ASSERT(j < ARRAY_SIZE(f));
				M0_UT_ASSERT(strcmp(ctp[i]->act_cf[j],
						    f[j]) == 0);
			}
		}
	}

	/*
	 * TEST
	 * Verify the pre-defined context types.
	 */

	/* check that they are registered */
#undef CT_IS_REG
#define CT_IS_REG(ptr, id) M0_UT_ASSERT(m0_addb_ctx_type_lookup(id) == ptr)
	CT_IS_REG(&m0_addb_ct_node_hi, M0_ADDB_CTXID_NODE_HI);
	CT_IS_REG(&m0_addb_ct_node_lo, M0_ADDB_CTXID_NODE_LO);
	CT_IS_REG(&m0_addb_ct_kmod,    M0_ADDB_CTXID_KMOD);
	CT_IS_REG(&m0_addb_ct_process, M0_ADDB_CTXID_PROCESS);
#undef CT_IS_REG

	/* check their fields */
#undef CTF
#define CTF(ct,idx,f) M0_UT_ASSERT(strcmp(ct.act_cf[idx], #f) == 0)
	M0_UT_ASSERT(m0_addb_ct_node_hi.act_cf_nr == 0);
	M0_UT_ASSERT(m0_addb_ct_node_lo.act_cf_nr == 0);
	M0_UT_ASSERT(m0_addb_ct_kmod.act_cf_nr    == 1);
	CTF(m0_addb_ct_kmod,    0, ts);
	M0_UT_ASSERT(m0_addb_ct_process.act_cf_nr == 2);
	CTF(m0_addb_ct_process, 0, ts);
	CTF(m0_addb_ct_process, 1, procid);
#undef CTF

	/*
	 * TEST
	 * Stress the registration by registering a multiple more of context
	 * types than the hash table size concurrently.
	 */
	{
#undef CT_UT_MULTIPLIER
#define CT_UT_MULTIPLIER 3
		int i;
		int rc;
		struct m0_thread t[CT_UT_MULTIPLIER];
		struct addb_ut_ct_thread_arg ta[CT_UT_MULTIPLIER];
		struct m0_semaphore sem;
		struct m0_addb_ctx_type *cts;
		size_t cts_nr = ARRAY_SIZE(addb_ct_htab) * CT_UT_MULTIPLIER;
		size_t htab_len_orig = 0;
		size_t htab_len_new = 0;
		size_t len;

		M0_ALLOC_ARR(cts, cts_nr);
		M0_UT_ASSERT(cts != NULL);

		for (i = 0; i < ARRAY_SIZE(addb_ct_htab); ++i)
			htab_len_orig += addb_ct_tlist_length(&addb_ct_htab[i]);
		M0_UT_ASSERT(htab_len_orig >= 4);

		/* init the fake cts */
		for (i = 0; i < cts_nr; ++i) {
			/* copy the template - no inline strings */
			memcpy(&cts[i], &m0__addb_ut_ct_tmpl, sizeof(cts[0]));
			cts[i].act_id = i + addb_ct_max_id + 1; /* unique id */
		}
		/* register concurrently on background threads */
		rc = m0_semaphore_init(&sem, 0);
		M0_UT_ASSERT(rc == 0);
		for (i = 0; i < CT_UT_MULTIPLIER; ++i) {
			ta[i].cts = cts;
			ta[i].cts_nr = cts_nr;
			ta[i].start = i;
			ta[i].incr = CT_UT_MULTIPLIER;
			ta[i].sem = &sem;
			M0_SET0(&t[i]);
			rc = M0_THREAD_INIT(&t[i],
					    struct addb_ut_ct_thread_arg *,
					    NULL, &addb_ut_ct_thread, &ta[i],
					    "addb_ut_ct%d", i);
			M0_UT_ASSERT(rc == 0);
		}
		for (i = 0; i < CT_UT_MULTIPLIER; ++i)
			m0_semaphore_up(&sem); /* unblock threads */
		for (i = 0; i < CT_UT_MULTIPLIER; ++i) {
			m0_thread_join(&t[i]);
			m0_thread_fini(&t[i]);
		}
		m0_semaphore_fini(&sem);
		for (i = 0; i < cts_nr; ++i)
			M0_UT_ASSERT(m0_addb_ctx_type_lookup(cts[i].act_id)
				     == &cts[i]);
		/*
		 * We expect that the new context types are spread evenly across
		 * the hash buckets as the ids are contiguous.
		 */
		for (i = 0; i < ARRAY_SIZE(addb_ct_htab); ++i) {
			len = addb_ct_tlist_length(&addb_ct_htab[i]);
			M0_UT_ASSERT(len >= CT_UT_MULTIPLIER);
			htab_len_new += len;
		}
		M0_UT_ASSERT(htab_len_new == htab_len_orig + cts_nr);

		/*
		 * There is no de-registration API, so unlink the context
		 * type objects explicitly before releasing the memory.
		 */
		for (i = 0; i < cts_nr; ++i)
			addb_ct_tlist_del(&cts[i]);
		for (i = 0, len = 0; i < ARRAY_SIZE(addb_ct_htab); ++i)
			len += addb_ct_tlist_length(&addb_ct_htab[i]);
		M0_UT_ASSERT(len == htab_len_orig);
		for (i = 0; i < cts_nr; ++i)
			M0_UT_ASSERT(m0_addb_ctx_type_lookup(cts[i].act_id)
				     == NULL);
		m0_free(cts);
#undef CT_UT_MULTIPLIER
	}
}

/*
 ****************************************************************************
 * addb_ut_rt_ex_test
 ****************************************************************************
 */

/* fake EX record types - static id's overridden when temporarily registered */
M0_ADDB_RT_EX(m0__addb_ut_rt_ex0, 20);
M0_ADDB_RT_EX(m0__addb_ut_rt_ex1, 21, "B1");
M0_ADDB_RT_EX(m0__addb_ut_rt_ex2, 22, "B1", "B2");
M0_ADDB_RT_EX(m0__addb_ut_rt_ex3, 23, "B1", "B2", "B3");
M0_ADDB_RT_EX(m0__addb_ut_rt_ex4, 24, "B1", "B2", "B3", "B4");
M0_ADDB_RT_EX(m0__addb_ut_rt_ex5, 25, "B1", "B2", "B3", "B4", "B5");
M0_ADDB_RT_EX(m0__addb_ut_rt_ex6, 26, "B1", "B2", "B3", "B4", "B5", "B6");
M0_ADDB_RT_EX(m0__addb_ut_rt_ex7, 27, "B1", "B2", "B3", "B4", "B5", "B6", "B7");
M0_ADDB_RT_EX(m0__addb_ut_rt_ex8, 28, "B1", "B2", "B3", "B4", "B5", "B6", "B7",
	      "B8");
M0_ADDB_RT_EX(m0__addb_ut_rt_ex9, 29, "B1", "B2", "B3", "B4", "B5", "B6", "B7",
	      "B8", "B9");

static void addb_ut_rt_ex_test(void)
{
	/*
	 * TEST
	 * Verify that the UT context types are initialized as expected.
	 */

	/* verify their names */
#undef ASSERT_EXNAME
#define ASSERT_EXNAME(n) M0_UT_ASSERT(strcmp(n.art_name, #n) == 0)
	ASSERT_EXNAME(m0__addb_ut_rt_ex0);
	ASSERT_EXNAME(m0__addb_ut_rt_ex1);
	ASSERT_EXNAME(m0__addb_ut_rt_ex2);
	ASSERT_EXNAME(m0__addb_ut_rt_ex3);
	ASSERT_EXNAME(m0__addb_ut_rt_ex4);
	ASSERT_EXNAME(m0__addb_ut_rt_ex5);
	ASSERT_EXNAME(m0__addb_ut_rt_ex6);
	ASSERT_EXNAME(m0__addb_ut_rt_ex7);
	ASSERT_EXNAME(m0__addb_ut_rt_ex8);
	ASSERT_EXNAME(m0__addb_ut_rt_ex9);
#undef ASSERT_EXNAME

	/* verify their ids, number of fields, and field names */
	{
		struct m0_addb_rec_type *exrtp[10] = {
#undef EXRTP
#define EXRTP(n) &m0__addb_ut_rt_ex ## n
			EXRTP(0), EXRTP(1), EXRTP(2), EXRTP(3), EXRTP(4),
			EXRTP(5), EXRTP(6), EXRTP(7), EXRTP(8), EXRTP(9),
#undef EXRTP
		};
		char *f[9] = {
			"B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", "B9"
		};
		int i, j;

		for (i = 0; i < ARRAY_SIZE(exrtp); ++i) {
			M0_UT_ASSERT(exrtp[i]->art_id == i + 20);
			M0_UT_ASSERT(exrtp[i]->art_rf_nr == i);
			M0_UT_ASSERT(exrtp[i]->art_magic == 0);
			for (j = 0; j < i; ++j) {
				M0_UT_ASSERT(j < ARRAY_SIZE(f));
				M0_UT_ASSERT(strcmp(exrtp[i]->art_rf[j].
						    arfu_name, f[j]) == 0);
			}
		}
	}
}

/*
 ****************************************************************************
 * addb_ut_rt_dp_test
 ****************************************************************************
 */

/* fake DP record types - static id's overridden when temporarily registered */
M0_ADDB_RT_DP(m0__addb_ut_rt_dp0, 30);
M0_ADDB_RT_DP(m0__addb_ut_rt_dp1, 31, "C1");
M0_ADDB_RT_DP(m0__addb_ut_rt_dp2, 32, "C1", "M0");
M0_ADDB_RT_DP(m0__addb_ut_rt_dp3, 33, "C1", "M0", "C3");
M0_ADDB_RT_DP(m0__addb_ut_rt_dp4, 34, "C1", "M0", "C3", "C4");
M0_ADDB_RT_DP(m0__addb_ut_rt_dp5, 35, "C1", "M0", "C3", "C4", "C5");
M0_ADDB_RT_DP(m0__addb_ut_rt_dp6, 36, "C1", "M0", "C3", "C4", "C5", "C6");
M0_ADDB_RT_DP(m0__addb_ut_rt_dp7, 37, "C1", "M0", "C3", "C4", "C5", "C6", "C7");
M0_ADDB_RT_DP(m0__addb_ut_rt_dp8, 38, "C1", "M0", "C3", "C4", "C5", "C6", "C7",
	      "C8");
M0_ADDB_RT_DP(m0__addb_ut_rt_dp9, 39, "C1", "M0", "C3", "C4", "C5", "C6", "C7",
	      "C8", "C9");

static void addb_ut_rt_dp_test(void)
{
	/*
	 * TEST
	 * Verify that the UT context types are initialized as expected.
	 */

	/* verify their names */
#undef ASSERT_DPNAME
#define ASSERT_DPNAME(n) M0_UT_ASSERT(strcmp(n.art_name, #n) == 0)
	ASSERT_DPNAME(m0__addb_ut_rt_dp0);
	ASSERT_DPNAME(m0__addb_ut_rt_dp1);
	ASSERT_DPNAME(m0__addb_ut_rt_dp2);
	ASSERT_DPNAME(m0__addb_ut_rt_dp3);
	ASSERT_DPNAME(m0__addb_ut_rt_dp4);
	ASSERT_DPNAME(m0__addb_ut_rt_dp5);
	ASSERT_DPNAME(m0__addb_ut_rt_dp6);
	ASSERT_DPNAME(m0__addb_ut_rt_dp7);
	ASSERT_DPNAME(m0__addb_ut_rt_dp8);
	ASSERT_DPNAME(m0__addb_ut_rt_dp9);
#undef ASSERT_DPNAME

	/* verify their ids, number of fields, and field names */
	{
		struct m0_addb_rec_type *dprtp[10] = {
#undef DPRTP
#define DPRTP(n) &m0__addb_ut_rt_dp ## n
			DPRTP(0), DPRTP(1), DPRTP(2), DPRTP(3), DPRTP(4),
			DPRTP(5), DPRTP(6), DPRTP(7), DPRTP(8), DPRTP(9),
#undef DPRTP
		};
		char *f[9] = {
			"C1", "M0", "C3", "C4", "C5", "C6", "C7", "C8", "C9"
		};
		int i, j;

		for (i = 0; i < ARRAY_SIZE(dprtp); ++i) {
			M0_UT_ASSERT(dprtp[i]->art_id == i + 30);
			M0_UT_ASSERT(dprtp[i]->art_rf_nr == i);
			M0_UT_ASSERT(dprtp[i]->art_magic == 0);
			for (j = 0; j < i; ++j) {
				M0_UT_ASSERT(j < ARRAY_SIZE(f));
				M0_UT_ASSERT(strcmp(dprtp[i]->art_rf[j].
						    arfu_name, f[j]) == 0);
			}
		}
	}
}

/*
 ****************************************************************************
 * addb_ut_rt_cntr_test
 ****************************************************************************
 */

/* fake CNTR record types - static ids overridden when temporarily registered */
M0_ADDB_RT_CNTR(m0__addb_ut_rt_cntr0, 40);
M0_ADDB_RT_CNTR(m0__addb_ut_rt_cntr1, 41, 10);
M0_ADDB_RT_CNTR(m0__addb_ut_rt_cntr2, 42, 10, 20);
M0_ADDB_RT_CNTR(m0__addb_ut_rt_cntr3, 43, 10, 20, 30);
M0_ADDB_RT_CNTR(m0__addb_ut_rt_cntr4, 44, 10, 20, 30, 40);
M0_ADDB_RT_CNTR(m0__addb_ut_rt_cntr5, 45, 10, 20, 30, 40, 50);
M0_ADDB_RT_CNTR(m0__addb_ut_rt_cntr6, 46, 10, 20, 30, 40, 50, 0xffffffff60);
M0_ADDB_RT_CNTR(m0__addb_ut_rt_cntr7, 47, 10, 20, 30, 40, 50, 0xffffffff60,
		0xffffffff70);
M0_ADDB_RT_CNTR(m0__addb_ut_rt_cntr8, 48, 10, 20, 30, 40, 50, 0xffffffff60,
		0xffffffff70, 0xffffffff80);
M0_ADDB_RT_CNTR(m0__addb_ut_rt_cntr9, 49, 10, 20, 30, 40, 50, 0xffffffff60,
		0xffffffff70, 0xffffffff80, 0xffffffff90);

static void addb_ut_rt_cntr_test(void)
{
	/*
	 * TEST
	 * Verify that the UT context types are initialized as expected.
	 */

	/* verify their names */
#undef ASSERT_CNTRNAME
#define ASSERT_CNTRNAME(n) M0_UT_ASSERT(strcmp(n.art_name, #n) == 0)
	ASSERT_CNTRNAME(m0__addb_ut_rt_cntr0);
	ASSERT_CNTRNAME(m0__addb_ut_rt_cntr1);
	ASSERT_CNTRNAME(m0__addb_ut_rt_cntr2);
	ASSERT_CNTRNAME(m0__addb_ut_rt_cntr3);
	ASSERT_CNTRNAME(m0__addb_ut_rt_cntr4);
	ASSERT_CNTRNAME(m0__addb_ut_rt_cntr5);
	ASSERT_CNTRNAME(m0__addb_ut_rt_cntr6);
	ASSERT_CNTRNAME(m0__addb_ut_rt_cntr7);
	ASSERT_CNTRNAME(m0__addb_ut_rt_cntr8);
	ASSERT_CNTRNAME(m0__addb_ut_rt_cntr9);
#undef ASSERT_CNTRNAME

	/* verify their ids, number of fields, and field names */
	{
		struct m0_addb_rec_type *cntrrtp[10] = {
#undef CNTRRTP
#define CNTRRTP(n) &m0__addb_ut_rt_cntr ## n
			CNTRRTP(0), CNTRRTP(1), CNTRRTP(2), CNTRRTP(3),
			CNTRRTP(4), CNTRRTP(5), CNTRRTP(6), CNTRRTP(7),
			CNTRRTP(8), CNTRRTP(9),
#undef CNTRRTP
		};
		uint64_t b[9] = {
			10, 20, 30, 40, 50, 0xffffffff60, 0xffffffff70,
			0xffffffff80, 0xffffffff90
		};
		int i, j;

		for (i = 0; i < ARRAY_SIZE(cntrrtp); ++i) {
			M0_UT_ASSERT(cntrrtp[i]->art_id == i + 40);
			M0_UT_ASSERT(cntrrtp[i]->art_rf_nr == i);
			M0_UT_ASSERT(cntrrtp[i]->art_magic == 0);
			M0_UT_ASSERT(cntrrtp[i]->art_base_type ==
			             M0_ADDB_BRT_CNTR);
			for (j = 0; j < i; ++j) {
				M0_UT_ASSERT(j < ARRAY_SIZE(b));
				M0_UT_ASSERT(cntrrtp[i]->art_rf[j].arfu_lower
					     == b[j]);
			}
		}
	}
}

/*
 ****************************************************************************
 * addb_ut_rt_smcntr_test
 ****************************************************************************
 */

/*
 * fake SM_CNTR record types,
 * static ids overridden when temporarily registered
 */
#include "sm/sm.h"

enum state {
	ST_INIT,
	ST_RUN,
	ST_SLEEP,
	ST_FINI
};

static struct m0_sm_state_descr states[] = {
	[ST_INIT] = {
		.sd_flags     = M0_SDF_INITIAL,
		.sd_name      = "Init",
		.sd_allowed   = M0_BITS(ST_RUN, ST_FINI)
	},
	[ST_RUN] = {
		.sd_name      = "Running",
		.sd_allowed   = M0_BITS(ST_SLEEP, ST_FINI)
	},
	[ST_SLEEP] = {
		.sd_name      = "Sleeping",
		.sd_allowed   = M0_BITS(ST_RUN)
	},
	[ST_FINI] = {
		.sd_flags     = M0_SDF_TERMINAL,
		.sd_name      = "Finished",
	}
};

static struct m0_sm_trans_descr trans[] = {
	{ "Start",   ST_INIT,  ST_RUN },
	{ "Fail",    ST_INIT,  ST_FINI },
	{ "Suspend", ST_RUN,   ST_SLEEP },
	{ "Finish",  ST_RUN,   ST_FINI },
	{ "Resume",  ST_SLEEP, ST_RUN },
};

static struct m0_sm_conf sm_conf = {
	.scf_name      = "sm conf",
	.scf_nr_states = ARRAY_SIZE(states),
	.scf_state     = states,
	.scf_trans_nr  = ARRAY_SIZE(trans),
	.scf_trans     = trans
};

M0_ADDB_RT_SM_CNTR(m0__addb_ut_rt_smcntr0, 400, &sm_conf);
M0_ADDB_RT_SM_CNTR(m0__addb_ut_rt_smcntr1, 401, &sm_conf, 10);
M0_ADDB_RT_SM_CNTR(m0__addb_ut_rt_smcntr2, 402, &sm_conf, 10, 20);
M0_ADDB_RT_SM_CNTR(m0__addb_ut_rt_smcntr3, 403, &sm_conf, 10, 20, 30);

static void addb_ut_rt_smcntr_test(void)
{
	/*
	 * TEST
	 * Verify that the UT context types are initialized as expected.
	 */

	/* verify their names */
#undef ASSERT_CNTRNAME
#define ASSERT_CNTRNAME(n) M0_UT_ASSERT(strcmp(n.art_name, #n) == 0)
	ASSERT_CNTRNAME(m0__addb_ut_rt_smcntr0);
	ASSERT_CNTRNAME(m0__addb_ut_rt_smcntr1);
	ASSERT_CNTRNAME(m0__addb_ut_rt_smcntr2);
	ASSERT_CNTRNAME(m0__addb_ut_rt_smcntr3);
#undef ASSERT_CNTRNAME

	/* verify their ids, number of fields, and field names */
	{
		struct m0_addb_rec_type *cntrrtp[] = {
#undef CNTRRTP
#define CNTRRTP(n) &m0__addb_ut_rt_smcntr ## n
			CNTRRTP(0), CNTRRTP(1), CNTRRTP(2), CNTRRTP(3),
#undef CNTRRTP
		};
		uint64_t b[] = {
			10, 20, 30,
		};
		int i, j;

		for (i = 0; i < ARRAY_SIZE(cntrrtp); ++i) {
			M0_UT_ASSERT(cntrrtp[i]->art_id == i + 400);
			M0_UT_ASSERT(cntrrtp[i]->art_rf_nr == i);
			M0_UT_ASSERT(cntrrtp[i]->art_magic == 0);
			M0_UT_ASSERT(cntrrtp[i]->art_base_type ==
			             M0_ADDB_BRT_SM_CNTR);
			M0_UT_ASSERT(cntrrtp[i]->art_sm_conf != NULL);
			for (j = 0; j < i; ++j) {
				M0_UT_ASSERT(j < ARRAY_SIZE(b));
				M0_UT_ASSERT(cntrrtp[i]->art_rf[j].arfu_lower
					     == b[j]);
			}
		}
	}
}

/*
 ****************************************************************************
 * addb_ut_rt_seq_test
 ****************************************************************************
 */

/* fake SEQ record type - static id overridden when temporarily registered */
M0_ADDB_RT_SEQ(m0__addb_ut_rt_seq, 50);

static void addb_ut_rt_seq_test(void)
{
	/*
	 * TEST
	 * Verify the UT sequence context type.
	 */
	M0_UT_ASSERT(strcmp(m0__addb_ut_rt_seq.art_name, "m0__addb_ut_rt_seq")
			    == 0);
	M0_UT_ASSERT(m0__addb_ut_rt_seq.art_id == 50);
	M0_UT_ASSERT(m0__addb_ut_rt_seq.art_magic == 0);
	M0_UT_ASSERT(m0__addb_ut_rt_seq.art_rf_nr == 0);
}

/*
 ****************************************************************************
 * addb_ut_rt_test
 ****************************************************************************
 */

/* template records - no inline data - not registered */
M0_ADDB_RT_EX(m0__addb_ut_rt_tmpl0, 999);
M0_ADDB_RT_DP(m0__addb_ut_rt_tmpl1, 999);
M0_ADDB_RT_CNTR(m0__addb_ut_rt_tmpl2, 999);
M0_ADDB_RT_SEQ(m0__addb_ut_rt_tmpl3, 999);

struct addb_ut_rt_thread_arg {
	struct m0_addb_rec_type *rts;
	size_t                   rts_nr;
	int                      start;
	int                      incr;
	struct m0_semaphore     *sem;
};

static void addb_ut_rt_thread(struct addb_ut_rt_thread_arg *ta)
{
	int i;

	m0_semaphore_down(ta->sem);
	for (i = ta->start; i < ta->rts_nr; i += ta->incr)
		m0_addb_rec_type_register(&ta->rts[i]);
	return;
}

static void addb_ut_rt_test(void)
{
	addb_ut_rt_ex_test();
	addb_ut_rt_dp_test();
	addb_ut_rt_cntr_test();
	addb_ut_rt_smcntr_test();
	addb_ut_rt_seq_test();

	/*
	 * TEST
	 * Validate the pre-defined context definition record type
	 */
	M0_UT_ASSERT(m0_addb_rec_type_lookup(M0_ADDB_RECID_CTXDEF)
		     == &addb_rt_ctxdef);
	M0_UT_ASSERT(addb_rt_ctxdef.art_base_type == M0_ADDB_BRT_CTXDEF);
	M0_UT_ASSERT(addb_rt_ctxdef.art_rf_nr == 0);

	/*
	 * TEST
	 * Validate the pre-defined exception types.
	 */
	M0_UT_ASSERT(m0_addb_rec_type_lookup(M0_ADDB_RECID_FUNC_FAIL)
		     == &m0_addb_rt_func_fail);
	M0_UT_ASSERT(m0_addb_rt_func_fail.art_base_type == M0_ADDB_BRT_EX);
	M0_UT_ASSERT(m0_addb_rt_func_fail.art_rf_nr == 2);
	M0_UT_ASSERT(strcmp(m0_addb_rt_func_fail.art_rf[0].arfu_name, "loc")
		     == 0);
	M0_UT_ASSERT(strcmp(m0_addb_rt_func_fail.art_rf[1].arfu_name, "rc")
		     == 0);

	M0_UT_ASSERT(m0_addb_rec_type_lookup(M0_ADDB_RECID_OOM)
		     == &m0_addb_rt_oom);
	M0_UT_ASSERT(m0_addb_rt_oom.art_base_type == M0_ADDB_BRT_EX);
	M0_UT_ASSERT(m0_addb_rt_oom.art_rf_nr == 1);
	M0_UT_ASSERT(strcmp(m0_addb_rt_oom.art_rf[0].arfu_name, "loc") == 0);

	/*
	 * TEST
	 * The reserved range is not available.
	 */
	{
		int i;
		for (i = M0_ADDB_RECID_RESV_FIRST;
		     i < M0_ADDB_RECID_RESV_NR; ++i)
			M0_UT_ASSERT(m0_addb_rec_type_lookup(i) != NULL);
	}

	/*
	 * TEST
	 * Stress the registration by registering a multiple more of record
	 * types than the hash table size concurrently.
	 */
	{
#undef RT_UT_MULTIPLIER
#define RT_UT_MULTIPLIER 3
		int i;
		int rc;
		struct m0_thread t[RT_UT_MULTIPLIER];
		struct addb_ut_rt_thread_arg ta[RT_UT_MULTIPLIER];
		struct m0_semaphore sem;
		struct m0_addb_rec_type *rts;
		size_t rts_nr = ARRAY_SIZE(addb_rt_htab) * RT_UT_MULTIPLIER;
		size_t htab_len_orig = 0;
		size_t htab_len_new = 0;
		size_t len;

		M0_ALLOC_ARR(rts, rts_nr);
		M0_UT_ASSERT(rts != NULL);

		for (i = 0; i < ARRAY_SIZE(addb_rt_htab); ++i)
			htab_len_orig += addb_rt_tlist_length(&addb_rt_htab[i]);
		M0_UT_ASSERT(htab_len_orig >= M0_ADDB_RECID_RESV_NR - 1);

		/* init the fake rts */
		for (i = 0; i < rts_nr; ++i) {
			struct m0_addb_rec_type *src = NULL;
			switch (i % 4) {
			case 0: src = &m0__addb_ut_rt_tmpl0; break;
			case 1: src = &m0__addb_ut_rt_tmpl1; break;
			case 2: src = &m0__addb_ut_rt_tmpl2; break;
			case 3: src = &m0__addb_ut_rt_tmpl3; break;
			}
			/* copy the template - no inline strings */
			memcpy(&rts[i], src, sizeof(rts[0]));
			rts[i].art_id = i + addb_rt_max_id + 1;
		}
		/* register concurrently on background threads */
		rc = m0_semaphore_init(&sem, 0);
		M0_UT_ASSERT(rc == 0);
		for (i = 0; i < RT_UT_MULTIPLIER; ++i) {
			ta[i].rts = rts;
			ta[i].rts_nr = rts_nr;
			ta[i].start = i;
			ta[i].incr = RT_UT_MULTIPLIER;
			ta[i].sem = &sem;
			M0_SET0(&t[i]);
			rc = M0_THREAD_INIT(&t[i],
					    struct addb_ut_rt_thread_arg *,
					    NULL, &addb_ut_rt_thread, &ta[i],
					    "addb_ut_rt%d", i);
			M0_UT_ASSERT(rc == 0);
		}
		for (i = 0; i < RT_UT_MULTIPLIER; ++i)
			m0_semaphore_up(&sem); /* unblock threads */
		for (i = 0; i < RT_UT_MULTIPLIER; ++i) {
			m0_thread_join(&t[i]);
			m0_thread_fini(&t[i]);
		}
		m0_semaphore_fini(&sem);
		for (i = 0; i < rts_nr; ++i)
			M0_UT_ASSERT(m0_addb_rec_type_lookup(rts[i].art_id)
				     == &rts[i]);
		/*
		 * We expect that the new record types are spread evenly across
		 * the hash buckets as the ids are contiguous.
		 */
		for (i = 0; i < ARRAY_SIZE(addb_rt_htab); ++i) {
			len = addb_rt_tlist_length(&addb_rt_htab[i]);
			M0_UT_ASSERT(len >= RT_UT_MULTIPLIER);
			htab_len_new += len;
		}
		M0_UT_ASSERT(htab_len_new == htab_len_orig + rts_nr);

		/*
		 * There is no de-registration API, so unlink the record
		 * type objects explicitly before releasing the memory.
		 */
		for (i = 0; i < rts_nr; ++i)
			addb_rt_tlist_del(&rts[i]);
		for (i = 0, len = 0; i < ARRAY_SIZE(addb_rt_htab); ++i)
			len += addb_rt_tlist_length(&addb_rt_htab[i]);
		M0_UT_ASSERT(len == htab_len_orig);
		for (i = 0; i < rts_nr; ++i)
			M0_UT_ASSERT(m0_addb_rec_type_lookup(rts[i].art_id)
				     == NULL);
		m0_free(rts);
#undef RT_UT_MULTIPLIER
	}
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
