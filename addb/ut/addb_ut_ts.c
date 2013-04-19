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
 * Original author: Rohan Puri <rohan_puri@xyratex.com>
 * Original creation date: 01/28/2013
 */

/* This file is designed to be included by addb/ut/addb_ut.c */

static void addb_ut_ts_test(void)
{
	int                     i;
	int                     j;
	int                     rc;
	uint32_t                page_idx;
	struct m0_addb_ts       ts;
	struct m0_addb_ts_rec  *ts_rec;
	struct m0_addb_ts_rec  *ts_rec1;
	struct m0_addb_ts_rec  *get_ts_rec;
	struct m0_addb_ts_rec  *ts_recs[74] = { 0 };
	struct m0_addb_ts_rec  *ts_recs1[146] = { 0 };
	struct m0_addb_ts_page *page;

	/* Test 01: TS init/fini SUCCESS */
	rc = addb_ts_init(&ts, 1, 2, 4096);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(ts.at_curr_pidx == 0);
	M0_UT_ASSERT(ts.at_curr_widx == 0);
	M0_UT_ASSERT(ts.at_max_pages == 2);
	addb_ts_fini(&ts);
	M0_UT_ASSERT(ts.at_curr_pidx == -1);
	M0_UT_ASSERT(ts.at_curr_widx == -1);
	M0_UT_ASSERT(ts.at_max_pages == -1);

	/* Test 02: Verification of TS record alloc & free SUCCESS */
	rc = addb_ts_init(&ts, 1, 2, 4096);
	M0_UT_ASSERT(rc == 0);
	ts_rec = addb_ts_alloc(&ts, 8 * 1);
	M0_UT_ASSERT(ts_rec != NULL);
	M0_UT_ASSERT(ts_rec->atr_magic == M0_ADDB_TS_REC_MAGIC);
	M0_UT_ASSERT(ts_rec->atr_header.atrh_magic == M0_ADDB_TS_LINK_MAGIC);
	M0_UT_ASSERT(ts_rec->atr_header.atrh_nr == 7);
	page_idx = ts_rec->atr_header.atrh_pg_idx;
	page = ADDB_TS_PAGE((&ts), page_idx);
	for (i = 0; i < 7; ++i)
		M0_UT_ASSERT(m0_bitmap_get(ts.at_bitmaps[page_idx], i));
	addb_ts_free(&ts, ts_rec);
	M0_UT_ASSERT(ts_rec->atr_magic == 0);
	M0_UT_ASSERT(page != NULL);
	for (i = 0; i < 7; ++i)
		M0_UT_ASSERT(!m0_bitmap_get(ts.at_bitmaps[page_idx], i));
	addb_ts_fini(&ts);

	/* Test 03: Verification of TS extend */
	rc = addb_ts_init(&ts, 1, 10, 4096);
	M0_UT_ASSERT(rc == 0);
	rc = addb_ts_extend(&ts, 5);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(ADDB_TS_CUR_PAGES(&ts) == 6);
	rc = addb_ts_extend(&ts, 4);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(ADDB_TS_CUR_PAGES(&ts) == 10);
	rc = addb_ts_extend(&ts, 1);
	M0_UT_ASSERT(rc == -E2BIG);
	M0_UT_ASSERT(ADDB_TS_CUR_PAGES(&ts) == 10);
	addb_ts_fini(&ts);

	/* Test 04: Verification of TS get record */
	rc = addb_ts_init(&ts, 1, 2, 4096);
	M0_UT_ASSERT(rc == 0);
	ts_rec = addb_ts_alloc(&ts, 8 * 1);
	M0_UT_ASSERT(ts_rec != NULL);
	M0_UT_ASSERT(ts_rec->atr_magic == M0_ADDB_TS_REC_MAGIC);
	M0_UT_ASSERT(ts_rec->atr_header.atrh_magic == M0_ADDB_TS_LINK_MAGIC);
	M0_UT_ASSERT(ts_rec->atr_header.atrh_nr == 7);
	get_ts_rec = addb_ts_get(&ts, 8 * 1);
	M0_UT_ASSERT(get_ts_rec == NULL);
	addb_ts_save(&ts, ts_rec);
	get_ts_rec = addb_ts_get(&ts, 8 * 1);
	M0_UT_ASSERT(ts_rec == get_ts_rec);
	get_ts_rec = addb_ts_get(&ts, 8 * 1);
	M0_UT_ASSERT(get_ts_rec == NULL);
	addb_ts_free(&ts, ts_rec);

	ts_rec = addb_ts_alloc(&ts, 8 * 3);
	M0_UT_ASSERT(ts_rec != NULL);
	addb_ts_save(&ts, ts_rec);
	ts_rec1 = addb_ts_alloc(&ts, 8 * 1);
	M0_UT_ASSERT(ts_rec1 != NULL);
	addb_ts_save(&ts, ts_rec1);
	get_ts_rec = addb_ts_get(&ts, 10);
	M0_UT_ASSERT(get_ts_rec == ts_rec1);
	get_ts_rec = addb_ts_get(&ts, 5);
	M0_UT_ASSERT(get_ts_rec == NULL);
	get_ts_rec = addb_ts_get(&ts, 8 *3);
	M0_UT_ASSERT(get_ts_rec == ts_rec);

	ts_rec = addb_ts_alloc(&ts, 8 * 2);
	M0_UT_ASSERT(ts_rec != NULL);
	addb_ts_save(&ts, ts_rec);
	ts_rec1 = addb_ts_alloc(&ts, 8 * 2);
	M0_UT_ASSERT(ts_rec1 != NULL);
	addb_ts_save(&ts, ts_rec1);
	get_ts_rec = addb_ts_get(&ts, 8 * 2);
	M0_UT_ASSERT(get_ts_rec == ts_rec);
	get_ts_rec = addb_ts_get(&ts, 8 * 2);
	M0_UT_ASSERT(get_ts_rec == ts_rec1);

	addb_ts_fini(&ts);

	/*
	 * Test 05: TS record allocation in full page fails, extend the
	 * TS and then allocation succeeds.
	 */
	rc = addb_ts_init(&ts, 1, 2, 4096);
	M0_UT_ASSERT(rc == 0);
	for (i = 0; i < 73; ++i) {
		ts_recs[i] = addb_ts_alloc(&ts, 8 * 1);
		M0_UT_ASSERT(ts_recs[i] != NULL);
		M0_UT_ASSERT(ts_recs[i]->atr_magic == M0_ADDB_TS_REC_MAGIC);
		M0_UT_ASSERT(ts_recs[i]->atr_header.atrh_nr == 7);
		page_idx = ts_recs[i]->atr_header.atrh_pg_idx;
		M0_UT_ASSERT(page_idx == 0);
		page = ADDB_TS_PAGE((&ts), page_idx);
		M0_UT_ASSERT(page != NULL);
		for (j = 0; j < 7 * (i > 0 ? i : 1); ++j)
			M0_UT_ASSERT(m0_bitmap_get(ts.at_bitmaps[page_idx], j));
	}
	ts_recs[73] = addb_ts_alloc(&ts, 8 * 1);
	M0_UT_ASSERT(ts_recs[73] == NULL);

	rc = addb_ts_extend(&ts, 1);
	M0_UT_ASSERT(rc == 0);
	ts_recs[73] = addb_ts_alloc(&ts, 8 * 1);
	M0_UT_ASSERT(ts_recs[73] != NULL);
	M0_UT_ASSERT(ts_recs[73]->atr_magic == M0_ADDB_TS_REC_MAGIC);
	M0_UT_ASSERT(ts_recs[73]->atr_header.atrh_nr == 7);
	page_idx = ts_recs[73]->atr_header.atrh_pg_idx;
	M0_UT_ASSERT(page_idx == 1);
	page = ADDB_TS_PAGE((&ts), page_idx);
	M0_UT_ASSERT(page != NULL);
	for (j = 0; j < 7; ++j)
		M0_UT_ASSERT(m0_bitmap_get(ts.at_bitmaps[page_idx], j));
	for (i = 0; i < 73; ++i)
		addb_ts_free(&ts, ts_recs[i]);
	addb_ts_fini(&ts);

	/** Test 06: Verification of TS pages wrap-around */
	rc = addb_ts_init(&ts, 2, 2, 4096);
	M0_ASSERT(rc == 0);
	for (i = 0; i < 146; ++i) {
		ts_recs1[i] = addb_ts_alloc(&ts, 8 * 1);
		M0_UT_ASSERT(ts_recs1[i] != NULL);
		M0_UT_ASSERT(ts_recs1[i]->atr_magic == M0_ADDB_TS_REC_MAGIC);
		M0_UT_ASSERT(ts_recs1[i]->atr_header.atrh_nr == 7);
		page_idx = ts_recs1[i]->atr_header.atrh_pg_idx;
		if (i < 73)
			M0_UT_ASSERT(page_idx == 0);
		else
			M0_UT_ASSERT(page_idx == 1);
		page = ADDB_TS_PAGE((&ts), page_idx);
		M0_UT_ASSERT(page != NULL);
		for (j = ts_recs1[i]->atr_header.atrh_widx;
		     j < ts_recs1[i]->atr_header.atrh_widx +
		         ts_recs1[i]->atr_header.atrh_nr; ++j)
			M0_UT_ASSERT(m0_bitmap_get(ts.at_bitmaps[page_idx], j));
	}

	addb_ts_free(&ts, ts_recs1[0]);

	M0_UT_ASSERT(ts.at_curr_pidx == 1);

	ts_recs1[0] = addb_ts_alloc(&ts, 8 * 1);
	M0_UT_ASSERT(ts_recs1[0] != NULL);
	M0_UT_ASSERT(ts_recs1[0]->atr_magic == M0_ADDB_TS_REC_MAGIC);
	M0_UT_ASSERT(ts_recs1[0]->atr_header.atrh_nr == 7);
	/* Wrap-round */
	M0_UT_ASSERT(ts.at_curr_pidx == 0);
	for (i = 0; i < 146; ++i)
		addb_ts_free(&ts, ts_recs1[i]);
	addb_ts_fini(&ts);
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
