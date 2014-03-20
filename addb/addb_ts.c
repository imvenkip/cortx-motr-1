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
 *                  Rohan Puri <Rohan_Puri@xyratex.com>
 * Original creation: 11/07/2012
 */
/**
   @page ADDB-DLD-TS Transient Store
   - @ref ADDB-DLD-TS-ovw
   - @ref ADDB-DLD-TS-fspec
      - @ref ADDB-DLD-TS-fspec-ds
      - @ref ADDB-DLD-TS-fspec-sub_macros
   - @ref ADDB-DLD-TS-lspec
      - @ref ADDB-DLD-TS-lspec-init
      - @ref ADDB-DLD-TS-lspec-pages
      - @ref ADDB-DLD-TS-lspec-trans_rec
      - @ref ADDB-DLD-TS-lspec-alloc
      - @ref ADDB-DLD-TS-lspec-save
      - @ref ADDB-DLD-TS-lspec-extend
      - @ref ADDB-DLD-TS-lspec-free
      - @ref ADDB-DLD-TS-lspec-query
      - @ref ADDB-DLD-TS-lspec-fini
   - @ref ADDB-DLD-TS-thread
   - @ref ADDB-DLD-TS-ut

   <hr>
   @section ADDB-DLD-TS-ovw Overview
   Transient store is pool of memory buffers to holds ADDB records
   temporarily before they are sent to the persistent store on server.

   Transient store is used by @ref ADDB-DLD-RSINK and
   @ref ADDB-DLD-EVMGR-cache .

   <hr>
   @section ADDB-DLD-TS-fspec Functional Specification
   @subsection ADDB-DLD-TS-fspec-ds Data Structures
   - m0_addb_ts
   - m0_addb_ts_page
   - m0_addb_ts_rec
   - m0_addb_ts_rec_header

   @subsection ADDB-DLD-TS-fspec-sub_macros Subroutines and Macros
   - addb_ts_init()
   - addb_ts_fini()
   - addb_ts_alloc()
   - addb_ts_save()
   - addb_ts_extend()
   - addb_ts_free()
   - addb_ts_get()
   - ADDB_TS_PAGE()

   <hr>
   @section ADDB-DLD-TS-lspec Logical Specification

   @subsection ADDB-DLD-TS-lspec-init Transient Store Initialization
   The transient store allocates a specified initial number of store pages
   and increases its allocation up to a specified maximum size.  In the case
   that the maximum size is the same as the initial size, the transient store
   will not grow and can be used in awkward contexts.

   Initialization of the transient store initializes a bitmap of transient store
   pages. It is implemented by addb_ts_init().

   @subsection ADDB-DLD-TS-lspec-pages Transient Store Pages
   @verbatim
   +---------+-------------+-------------+-----+-------------+
   | Bitmap  | 64bit word0 | 64bit word1 | ... | 64bit wordN |
   +---------+-------------+-------------+-----+-------------+
	Fig 1: Transient Store Page
   @endverbatim

   Pre-allocated buffer (m0_bufvec) stores ADDB transient records. Each
   page (m0_bufvec segment) contains a bitmap and array of 64bit words.
   Page bitmap is used to identify used/free 64bit words. Set of 64bit
   words used to store ADDB transient records. One ADDB record will use
   a set of 64bit data units.

   bitmap_size = (page_size/word_size)/8 bytes = page_size/64 bytes
	       = page_size/512 words

   @subsection ADDB-DLD-TS-lspec-trans_rec Transient Record
   @verbatim
		+--------+
		|Bitmap  |
		+--------+ <-------+---------+-
		|word0   |	   |	     |
		+--------+	   |	     |
		|word1   |	   |	     |
		+--------+	   |	     |
		|word2   |	transient    |
		+--------+	 record	     |
		|word3   |	 header	     |
  ADDB rec	+--------+	   |	     |
  pointer       |word4   |	   |	transient
  returned ---->+--------+ <-------+--	 record
  to application|word5   |	   |	     |
		+--------+	   |	     |
		|   .	 |	transient    |
		Z   .	 Z	record       |
		|   .	 |	body         |
		+--------+	   |	     |
		|wordm   |	   |	     |
		+--------+ <-------+---------+-
		|wordm+1 |
		+--------+
		|   .	 |
		Z   .	 Z
		|   .	 |
		+--------+
		|wordn-1 |
		+--------+
		|wordn   |
		+--------+
	Fig 2: Transient Record in transient Store Page
   @endverbatim

   A transient record is comprised of a header(m0_addb_ts_rec_header)
   and array of 64bit word record body. Transient record header will be
   used by upper layer to retrieve the record from store for further
   processing. Transient record body contains actual ADDB on-wire record.
   Transient record will be freed by addb_ts_free().

   @subsection ADDB-DLD-TS-lspec-alloc Transient Record Allocation
   Acquire buffer scans bitmaps of all transient store pages to find the
   first contiguous 64bit words of transient record size (transient record
   header + requested size). After getting buffer, it creates transient
   record and returns pointer to transient record body pointer to caller.
   If a buffer is not available in the transient store, it may expand transient
   store by reallocating m0_bufvec, up to a pre-specified maximum size. The
   address of transient records body will returned to caller. Interface
   addb_ts_alloc() will implement this functionality.

   Algorithm for finding first contiguous 64bit words of length "LENGTH" :
   This "LENGTH" includes addb_record length +
   sizeof (struct m0_addb_ts_rec_header)
   @code
	rec_ptr = NULL;
	page_idx = ts->at_curr_pidx;
	word_idx = ts->at_curr_widx;

	found_record_space = false;
	full_scanned	   = false;
	wrap_around	   = false;
	while (!found_record_space && !full_scanned) {
		wordcount = 0;
		page = ADDB_TS_PAGE(ts, page_idx);

		for (i = word_idx; i < page->atp_bitmap.b_nr; ++i) {
			if (wrap_around &&
			    page_idx == ts->at_curr_pidx &&
			    i == ts->at_curr_widx)
				break;
			if (!m0_bitmap_get(&page->atp_bitmap, i)
				++wordcount;
			else
				wordcount = 0;

			if (wordcount == LENGTH) {
				found_record_space = true;
				rec_ptr = &(page->atp_words[i- (wordcount-1)]);
				ts->at_curr_pidx = page_idx;
				ts->at_curr_widx = i; // next word index
				break;
			}
		}

		if (!found_record_space && page_idx == nPages - 1) {
			page_idx = 0;
			word_idx = 0;
			wrap_around = true;
		} else if (!found_record_space) {
			++page_idx;
			word_idx = 0;
		}
		if (!found_record_space &&
			   wrap_around &&
			   page_idx == ts->at_curr_pidx)
			full_scanned = true;
	}

   @endcode

   Use case of addb_ts_alloc() shown in following code segment:
   @verbatim
   #define M0_ADDB_TS_EXTEND_SIZE 16
   struct m0_addb_rec *addb_rsink_alloc(m0_addb_ts *ts, size_t len)
   {
	struct m0_addb_ts_rec *ts_rec;

	do {
		ts_rec = addb_ts_alloc(ts, len);
	} while(ts_rec == NULL &&
		addb_ts_extend(ts, M0_ADDB_TS_EXTEND_SIZE) == 0);

	if (ts_rec != NULL)
		return (struct m0_addb_rec *)ts_rec->atr_data;
	else
		return NULL;
   }
   @endverbatim

   @subsection ADDB-DLD-TS-lspec-save Transient record header save
   After a ts record is successfully allocated, its consumer is expected
   to fill it with valid addb record. This api addb_ts_save() would add such a
   ts record to the ts record's header list.

   (NOTE: Another context where this addb_ts_save() would be invoked from is:
   Suppose consumer of transient records consumes a particular transient record,
   it tries to send it over the network, but sending fails due to any particular
   reason, then its the responsibility of the consumer to add the ts record back
   to the header's list.)

   @subsection ADDB-DLD-TS-lspec-extend Transient Store Extend
   If addb_ts_alloc() is not able to find buffers of requested size transient
   record, it will expand the transient store, up to a pre-specified maximum
   size. Interface addb_ts_extend() will do the expansion of transient store.

   @subsection ADDB-DLD-TS-lspec-free Transient Record Free
   Release 64bit words back to transient store and mark these words as
   free to use for other records. This will be used by RPC sink
   internally on RPC item sent callback with success, implemented by
   addb_ts_free().

   @subsection ADDB-DLD-TS-lspec-fini Transient Store Finalization
   Transient store pages and bitmaps related to those pages will be freed
   during finalization of transient store. Interface implemented by
   addb_ts_fini().

   @subsection ADDB-DLD-TS-lspec-query Transient Record Query
   First fit record with respect to provided record size returned by
   transient record query interface. Interface implemented by addb_ts_get().

   Algorithm for finding first fit record for size "SIZE" :
   @verbatim
	foreach (transient_record in transient_store) {
		if (transient_record->size <= SIZE) {
			firstfit_record = transient_record;
			break;
		}
	}

   @endverbatim

   <hr>
   @section ADDB-DLD-TS-thread Threading and Concurrency Model
   Transient store access is assumed to be externally serialized. RPC-sink
   and cached event manager will use their mutex to serialize transient
   store access.

   <hr>
   @section ADDB-DLD-TS-ut Unit Tests

   - Test 01 : Verification of TS initialization/finalization.
   - Test 02 : Verification of TS record allocation/freeing.
   - Test 03 : Verification of TS extend.
		- SUCCESS case.
		- -E2BIG error case. (cannot extend to total no.of pages
		  more than max_pages mentioned during TS init)
   - Test 04 : Verification of TS record get
		- ts_init-->ts_rec_alloc->ts_rec_get. (returns NULL)
		- ts_init->ts_rec_alloc->ts_rec_save->ts_rec_get.
		   (returns valid ts rec)
		- ts_init->ts_rec_alloc->ts_rec_save->ts_rec_get->ts_rec_get
		   (2nd ts_rec_get returns NULL)
		- ts_init->ts_rec_alloc(size = 24)->ts_rec_save(size = 24)->
		   ts_rec_alloc(size = 8)->ts_rec_save(size = 8)->
		   ts_rec_get(size = 10) (returns ts_rec with size 8)
		- ts_init->ts_rec_alloc(rec1, size = 16)->ts_rec_save(size = 16)
		   ->ts_rec_alloc(rec2, size = 16)->ts_rec_save(size = 16)->
		   ts_rec_get(size = 16) (returns ts_rec1).
   - Test 05 : Verification of wrap-around condition.
 */

#include "addb/addb_pvt.h"
#include "lib/bob.h"

/**
 * Extend transient store.
 * @param ts Specify the transient store.
 * @param npages Specify extended number of pages.
 * @retval 0 Success
 * @retval -ENOMEM Memory not available
 * @retval -E2BIG Transient store reached to it's maximum size.
 * @pre ts != NULL
 * @pre npages > 0
 */
static int addb_ts_extend(struct m0_addb_ts *ts, uint32_t npages)
{
	int                     rc;
	uint32_t                pidx;
	uint32_t                orig_pidx;
	uint32_t                bits_per_page;

	M0_PRE(ts != NULL);
	M0_PRE(npages > 0);

	if (npages + ADDB_TS_CUR_PAGES(ts) > ts->at_max_pages)
		return M0_ERR(-E2BIG, "TS reached max size limit "
			       "(%d + %d > %d)", npages, ADDB_TS_CUR_PAGES(ts),
			       ts->at_max_pages);

	orig_pidx = pidx = ADDB_TS_CUR_PAGES(ts);
	bits_per_page = ts->at_page_size / WORD_SIZE;

	/* Allocate & initialize bitmap for each page */
	for (; pidx < orig_pidx + npages; ++pidx) {
		M0_ALLOC_PTR(ts->at_bitmaps[pidx]);
		if (ts->at_bitmaps[pidx] == NULL)
			goto cleanup;
		m0_bitmap_init(ts->at_bitmaps[pidx], bits_per_page);
	}
	rc = m0_bufvec_extend(&ts->at_pages, npages);
	if (rc != 0)
		goto cleanup;

	return rc;
cleanup:
	while (pidx > orig_pidx) {
		--pidx;
		m0_bitmap_fini(ts->at_bitmaps[pidx]);
		m0_free(ts->at_bitmaps[pidx]);
	}

	return -ENOMEM;
}

M0_TL_DESCR_DEFINE(rec_queue, "ts rec headers", static,
		   struct m0_addb_ts_rec_header, atrh_linkage,
		   atrh_magic, M0_ADDB_TS_LINK_MAGIC, M0_ADDB_TS_HEAD_MAGIC);
M0_TL_DEFINE(rec_queue, static, struct m0_addb_ts_rec_header);

static const struct m0_bob_type addb_ts_rec_bob = {
	.bt_name         = "addb ts rec",
	.bt_magix_offset = M0_MAGIX_OFFSET(struct m0_addb_ts_rec, atr_magic),
	.bt_magix        = M0_ADDB_TS_REC_MAGIC,
	.bt_check        = NULL
};

M0_BOB_DEFINE(static, &addb_ts_rec_bob, m0_addb_ts_rec);

/**
 * Initialize transient store.
 * @param ts Specify the transient store.
 * @param npages_init Initial size of transient store in pages.
 * @param npages_max Maximum size of transient store in pages.
 * @param pgsize Page size in bytes.
 * @retval 0 Success
 * @retval -ENOMEM Memory not available
 * @pre ts != NULL
 * @pre npages_init != 0 && npages_max != 0 && pgsize != 0
 * @pre npages_init <= npages_max
 * @pre M0_IS_8ALIGNED(pgsize)
 */
static int addb_ts_init(struct m0_addb_ts *ts, uint32_t npages_init,
			uint32_t npages_max, m0_bcount_t pgsize)
{
	int                     rc;
	int                     i;
	uint32_t                bits_per_page;
	uint32_t                page_index;

	M0_PRE(ts != NULL);
	M0_PRE(npages_init != 0 && npages_max != 0 && pgsize != 0);
	M0_PRE(npages_init <= npages_max);
	M0_PRE(M0_IS_8ALIGNED(pgsize));

	rc = m0_bufvec_alloc(&ts->at_pages, npages_init, pgsize);
	if (rc != 0)
		return M0_ERR(rc, "Transient store pages init");

	bits_per_page = pgsize / WORD_SIZE;

	/* Allocate bitmap for each page */
	M0_ALLOC_ARR(ts->at_bitmaps, npages_max);
	if (ts->at_bitmaps == NULL)
		goto cleanup_1;
	for (i = 0; i < npages_init; ++i) {
		M0_ALLOC_PTR(ts->at_bitmaps[i]);
		if (ts->at_bitmaps[i] == NULL)
			goto cleanup_2;
	}

	/* Initialize bitmap for each page */
	for (page_index = 0; page_index < npages_init; ++page_index) {
		rc = m0_bitmap_init(ts->at_bitmaps[page_index], bits_per_page);
		if (rc != 0)
			goto cleanup_3;
	}

	ts->at_curr_pidx  = ts->at_curr_widx = 0;
	ts->at_max_pages  = npages_max;
	ts->at_page_size  = pgsize;
	rec_queue_tlist_init(&ts->at_rec_queue);

	return 0;

cleanup_3:
	while (page_index > 0) {
		--page_index;
		m0_bitmap_fini(ts->at_bitmaps[page_index]);
	}
cleanup_2:
	while (i > 0) {
		--i;
		m0_free(ts->at_bitmaps[i]);
	}
	m0_free(ts->at_bitmaps);
cleanup_1:
	m0_bufvec_free(&ts->at_pages);

	return -ENOMEM;
}

static bool addb_ts_rec_invariant(struct m0_addb_ts_rec *rec)
{
	return rec != NULL && m0_addb_ts_rec_bob_check(rec);
}

/**
 * Acquire transient store buffer.
 * @param ts  Specify transient store object
 * @param len Specify the length of transient ADDB record in bytes.
 * @pre ts != NULL
 * @pre M0_IS_8ALIGNED(len)
 */
static struct m0_addb_ts_rec *addb_ts_alloc(struct m0_addb_ts *ts,
					    m0_bcount_t len)
{
	int                     i;
	bool                    wrap_around  = false;
	uint32_t                pidx;
	uint32_t                word_idx;
	uint32_t                count;
	struct m0_addb_ts_rec  *rec_ptr = NULL;
	struct m0_addb_ts_page *page = NULL;

	M0_PRE(ts != NULL);

	len += sizeof *rec_ptr;
	M0_ASSERT(M0_IS_8ALIGNED(len));

	pidx = ts->at_curr_pidx;
	word_idx = ts->at_curr_widx;
	len /= WORD_SIZE;

	while (1) {
		count = 0;
		page = ADDB_TS_PAGE(ts, pidx);

		M0_ASSERT(page != NULL);
		M0_ASSERT(word_idx < ts->at_bitmaps[pidx]->b_nr);

		for (i = word_idx; i < ts->at_bitmaps[pidx]->b_nr; ++i) {
			if (!m0_bitmap_get(ts->at_bitmaps[pidx], i))
				++count;
			else
				count = 0;

			if (count == len) {
				rec_ptr = (struct m0_addb_ts_rec *)
					   &(page->atp_words[i - (count - 1)]);
				goto found;
			}
		}

		if (pidx == ADDB_TS_CUR_PAGES(ts) - 1) {
			pidx = 0;
			wrap_around = true;
		} else {
			++pidx;
		}
		if (wrap_around && pidx == ts->at_curr_pidx)
			break;
		word_idx = 0;
	}
found:
	if (rec_ptr != NULL) {
		/* Last word in this page */
		if (i == ts->at_bitmaps[pidx]->b_nr - 1) {
			if (pidx + 1 < ADDB_TS_CUR_PAGES(ts)) {
				ts->at_curr_pidx = pidx + 1;
			} else {
				M0_ASSERT(pidx == ADDB_TS_CUR_PAGES(ts) - 1);
				ts->at_curr_pidx = 0;
			}
			ts->at_curr_widx = 0;
		} else {
			ts->at_curr_pidx = pidx;
			/* Next word index */
			ts->at_curr_widx = i + 1;
		}
		m0_addb_ts_rec_bob_init(rec_ptr);
		rec_queue_tlink_init(&rec_ptr->atr_header);
		rec_ptr->atr_header.atrh_pg_idx = pidx;
		rec_ptr->atr_header.atrh_widx   = i - (count - 1);
		rec_ptr->atr_header.atrh_nr     = len;
		for (i = rec_ptr->atr_header.atrh_widx;
		     i < rec_ptr->atr_header.atrh_widx +
		     rec_ptr->atr_header.atrh_nr;
		     ++i)
			m0_bitmap_set(ts->at_bitmaps[pidx], i, true);
		addb_ts_rec_invariant(rec_ptr);
	}
	return rec_ptr;
}

/**
 * Add the transient store record's header to ts record header's list.
 * @param ts  Specify transient store object
 * @param rec Specify transient record pointer
 * @pre ts != NULL
 * @pre M0_PRE(addb_ts_invariant(rec))
 */
static void addb_ts_save(struct m0_addb_ts *ts, struct m0_addb_ts_rec *rec)
{
	M0_PRE(ts != NULL);
	M0_PRE(addb_ts_rec_invariant(rec));

	rec_queue_tlist_add_tail(&ts->at_rec_queue, &rec->atr_header);
}

/**
 * Remove record from TS.
 * @param ts  Specify transient store object
 * @param rec Specify transient record pointer
 * @pre ts != NULL
 * @pre addb_ts_invariant(rec)
 */
static void addb_ts_free(struct m0_addb_ts *ts, struct m0_addb_ts_rec *rec)
{
	int                     i;
	uint32_t                word_idx;
	uint32_t                word_cnt;
	uint32_t                pidx;

	M0_PRE(ts != NULL);
	M0_PRE(addb_ts_rec_invariant(rec));

	word_idx = rec->atr_header.atrh_widx;
	word_cnt = rec->atr_header.atrh_nr;
	pidx     = rec->atr_header.atrh_pg_idx;

	for (i = word_idx; i < word_idx + word_cnt; ++i) {
		M0_ASSERT(m0_bitmap_get(ts->at_bitmaps[pidx], i));
		m0_bitmap_set(ts->at_bitmaps[pidx], i, false);
	}
	m0_addb_ts_rec_bob_fini(rec);
	M0_SET0(&rec->atr_header);
}

/**
 * Finalize transient store.
 * @param ts Specify the transient store.
 * @pre ts != NULL
 */
static void addb_ts_fini(struct m0_addb_ts *ts)
{
	int                     page_index;

	M0_PRE(ts != NULL);

	for (page_index = 0; page_index < ADDB_TS_CUR_PAGES(ts); ++page_index) {
		m0_bitmap_fini(ts->at_bitmaps[page_index]);
		m0_free(ts->at_bitmaps[page_index]);
	}
	m0_free(ts->at_bitmaps);
	m0_bufvec_free(&ts->at_pages);
	rec_queue_tlist_fini(&ts->at_rec_queue);
	ts->at_curr_pidx = ts->at_curr_widx = ts->at_max_pages = -1;
	ts->at_page_size = -1;
}

/**
 * Get transient record.
 * It returns first fit transient record with respect to size passed.
 * @param ts Specify transient store.
 * @param reclen Specify maximum length of record query for.
 * @pre ts != NULL
 */
static struct m0_addb_ts_rec *addb_ts_get(struct m0_addb_ts *ts,
					  m0_bcount_t reclen)
{
	struct m0_addb_ts_rec        *ts_rec    = NULL;
	struct m0_addb_ts_rec_header *first_fit;

	M0_PRE(ts != NULL);

	m0_tl_for(rec_queue, &ts->at_rec_queue, first_fit) {
		if (ADDB_TS_GET_REC_SIZE(first_fit) <= reclen) {
			ts_rec = bob_of(first_fit, struct m0_addb_ts_rec,
					atr_header, &addb_ts_rec_bob);
			rec_queue_tlist_del(first_fit);
			break;
		}
	} m0_tl_endfor;

	return ts_rec;
}

static bool addb_ts_is_empty(struct m0_addb_ts *ts)
{
	return rec_queue_tlist_is_empty(&ts->at_rec_queue);
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
