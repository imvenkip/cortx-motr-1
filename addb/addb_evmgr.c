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
 * Original author: Dave Cohrs <dave_cohrs@xyratex.com>
 *                  Carl Braganza <carl_braganza@xyratex.com>
 * Original creation: 10/17/2012
 */

/**
   @page ADDB-DLD-EVMGR Event Management

   This design relates to ADDB event management.
   This is a sub-component design document and hence not all sections are
   present.  Refer to @ref ADDB-DLD "ADDB Detailed Design" for the design
   requirements.
   - @ref ADDB-DLD-EVMGR-fspec
   - @ref ADDB-DLD-EVMGR-lspec
     - @ref ADDB-DLD-EVMGR-pt
     - @ref ADDB-DLD-EVMGR-cache
     - @ref ADDB-DLD-EVMGR-post
     - @ref ADDB-DLD-EVMGR-std
     - @ref ADDB-DLD-EVMGR-thread

   <hr>
   @section ADDB-DLD-EVMGR-fspec Functional Specification
   The primary data structures involved are:
   - m0_addb_mc_evmgr
   - m0_addb_rec

   The following external interfaces must be implemented:
   - m0_addb_mc_configure_pt_evmgr()
   - m0_addb_mc_configure_cache_evmgr()
   - m0_addb_mc_cache_evmgr_flush()
   - M0_ADDB_POST()
   - M0_ADDB_POST_CNTR()
   - M0_ADDB_POST_SEQ()
   - m0__addb_post()

   <hr>
   @section ADDB-DLD-EVMGR-lspec Logical Specification

   @subsection ADDB-DLD-EVMGR-pt The Passthrough Event Manager

   The passthrough event manager is a thin layer built above the Record-Sink
   defined by m0_addb_mc_recsink.

   The m0_addb_mc_configure_pt_evmgr() configures a passthrough event manager.
   It verifies that the ADDB machine is valid and has a Record-Sink configured,
   but it has no event manager.  It performs several operations:
   - It allocates an addb_pt_evmgr object, assigned the address of its
   addb_pt_evmgr::ape_evmgr to the supplied m0_addb_mc::am_evmgr.
   - It sets the m0_addb_mc_evmgr::evm_magic field to #M0_ADDB_PT_EVMGR_MAGIC.
   - It sets the m0_addb_mc_evmgr::evm_post_awkward to false.
   - It sets the m0_addb_mc_evmgr::evm_rec_alloc and m0_addb_mc_evmgr::evm_post
   function pointers to m0_addb_mc_recsink::rs_rec_alloc() and
   m0_addb_mc_recsink::rs_save() respectively.
   - It initializes the addb_pt_evmgr::ape_ref with an initial value of 1.
   - It assigns the m0_addb_mc_evmgr::evm_get() and m0_addb_mc_evmgr::evm_put()
   function pointers.
   - It sets the m0_addb_mc_evmgr::evm_copy() function pointer to NULL.
   - It sets the m0_addb_mc_evmgr::evm_log() to a function that can record
   exceptions to the system log.

   The addb_pt_evmgr_get() increments the reference count of the
   addb_pt_evmgr::ape_ref.

   The addb_pt_evmgr_put() decrements the reference count of the
   addb_pt_evmgr::ape_ref, depending on the associated release function to
   handle clean-up.

   @subsection ADDB-DLD-EVMGR-cache The Caching Event Manager

   The caching event manager holds the cache for ADDB records. This cache
   is implemented using transient store @see @ref ADDB-DLD-TS "Transient Store".

   The caching event manager is used for posting of addb records in awkward
   contexts. The m0_addb_mc_configure_cache_evmgr() configures a caching event
   manager. It verifies that the ADDB machine is valid and does not have a
   Record-Sink & event manager configured.  It performs several operations:
   - It initializes the transient store embedded in addb_cache_evmgr with valid
   arguments like pg_size, no_of_pages.
   - It allocates an addb_cache_evmgr object, assigned the address of its
   addb_cache_evmgr::ace_evmgr to the supplied m0_addb_mc::am_evmgr.
   - It sets the m0_addb_mc_evmgr::evm_magic field to
   #M0_ADDB_CACHE_EVMGR_MAGIC.
   - It sets the m0_addb_mc_evmgr::evm_post_awkward to true.
   - It sets the m0_addb_mc_evmgr::evm_rec_alloc and m0_addb_mc_evmgr::evm_post
   function pointers to addb_cache_evmgr_rec_alloc() and
   addb_cache_evmgr_rec_save() respectively.
   (addb_cache_evmgr_rec_alloc() allocates the memory for the rec of size
    rec_len from the initialized transient store
    addb_cache_evmgr_rec_save() adds the transient store addb record to the
    transient stores headers list)
   - It initializes the addb_cache_evmgr::ace_ref with an initial value of 1.
   - It assigns the m0_addb_mc_evmgr::evm_get() and m0_addb_mc_evmgr::evm_put()
   function pointers.
   - It sets the m0_addb_mc_evmgr::evm_copy() function pointer to
   addb_cache_evmgr_flush(). This method gets all the transient store's
   addb rec from the source addb_machine in FIFO & post them on destination
   addb machine. This posting includes invocation of evm_rec_alloc() &
   evm_post() of the destination machines evmgr.
   - It sets the m0_addb_mc_evmgr::evm_log() to NULL.

   The addb_cache_evmgr_get() increments the reference count of the
   addb_cache_evmgr::ace_ref.

   The addb_cache_evmgr_put() decrements the reference count of the
   addb_cache_evmgr::ace_ref, on last ref put, addb_cache_release() is called,
   this function finalizes the cache evmgrs transient store and then frees
   addb_cache_evmgr object.

   @subsection ADDB-DLD-EVMGR-post Event Posting

   Event posting causes data from an ADDB exception, data point, counter or
   sequence to be copied to a m0_addb_rec and sent either to a Record-Sink in
   the case of a passthrough event manager or cached in the case of a caching
   event manager.  Because the differences between the two variants of event
   managers are hidden behind function pointers, posting itself can be described
   in a generic way.

   Several macros are provided to applications to post events:
   - M0_ADDB_POST()
   - M0_ADDB_POST_CNTR()
   - M0_ADDB_POST_SEQ()

   First, a m0_addb_post_data object is initialized.  This object is initialized
   by reference.  The dynamic data will be copied in a later step by
   m0__addb_post().
   - The m0_addb_post_data::apd_rt and m0_addb_post_data::apd_cv are set
   based on the corresponding macro parameters.
   - M0_ADDB_POST() sets the m0_addb_post_data::apd_args to an array of
   the macro __VA_ARGS__ parameters.
   - M0_ADDB_POST_CNTR() sets the m0_addb_post_data::apd_cntr to the supplied
   counter.
   - M0_ADDB_POST_SEQ() sets the m0_addb_post_data::apd_seq to the supplied
   sequence.

   In the case of M0_ADDB_POST(), the number of parameters present is compared
   in a M0_ASSERT() with the number specified in m0_addb_rec_type::art_rf_nr.
   No such check is required for the other macros; they only take a single,
   required parameter.

   The m0__addb_post() function is called to actually post the record.  This
   function is discussed in more detail below.

   After this function returns, M0_ADDB_POST_CNTR() performs required the reset
   of the counter, as discussed in @ref ADDB-DLD-CNTR-lspec-usage.  No
   post-processing is required in the other posting macros.

   The m0__addb_post() function performs the following operations.
   - In the case of ADDB exceptions only, an attempt is made to record the
   exception in the system logging facility, using printk(), syslog() or
   M0_LOG().  The specific logging mechanism is implemented by
   m0_addb_mc_evmgr::evm_log().
   - It allocates memory large enough to contain a m0_addb_rec and all of the
   variable sized data referenced by that record.  The
   m0_addb_mc_evmgr::evm_rec_alloc() function is used to do the allocation.
   The specific implementation varies in each macro.
     - In all cases, the size of the context vector is determined and added
     to the total size required.
     - In the case of exceptions and data points, the size required
     also depends directly on m0_addb_rec_type::art_rf_nr.
     - In the case of counters, the size allocated depends on a base size as
     discussed in @ref ADDB-DLD-CNTR-lspec-rec plus the size required for the
     histogram, m0_addb_rec_type::art_rf_nr.
     - In the case of a sequence, the size required depends on the variable
     length sequence m0_addb_post_data::apd_seq with the size
     m0_addb_uint64_seq::au64s_nr.
   - The fields of the m0_addb_rec are set.
     - Pointers in the m0_addb_rec are set to refer to the memory returned by
     m0_addb_mc_evmgr::evm_rec_alloc(), beyond the initial m0_addb_rec as
     follows (F is the total number of data fields, as discussed above and
     N is the number of contexts in the context vector):
   @verbatim
    +-------------------------+
    | m0_addb_rec             |
    |   ar_data.au64s_nr=F    |
    |   ar_data.au64s_data ---+------\
    |   ar_ctxids.acis_nr=N   |      |
    |   ar_ctxids.acis_data --+---\  |
    |   ...                   |   |  |
    +-------------------------+   |  |
    | m0_addb_uint64_seq[0]   |<--/  |
    |   au64s_nr=depth        |      |
    |   au64s_data -----------+--\   |
    | ...                     |  |   |
    | m0_addb_uint64_seq[N-1] |  |   |
    |   au64s_nr=depth        |  |   |
    |   au64s_data -----------+--+-\ |
    +-------------------------+  | | |
    | root ac_id              |<-/ | |
    |   ...                   |    | |
    | ctx->ac_id              |    | |
    +-------------------------+    | |
    | ...                     |    | |
    +-------------------------+    | |
    | root ac_id              |<---/ |
    |   ...                   |      |
    | ctx->ac_id              |      |
    +-------------------------+      |
    | f0                      |<-----/
    | f1                      |
    | ...                     |
    +-------------------------+
@endverbatim
   - The m0_addb_mc_evmgr::evm_post() function is called to post the record, if
   possible.  The m0_addb_mc_evmgr::evm_post() function takes ownership of
   the memory allocated by m0_addb_mc_evmgr::evm_rec_alloc().

   @subsection ADDB-DLD-EVMGR-std Standard Exceptions

   @todo Determine which of the deprecated standard exceptions to bring forward
   and if any additional standard exceptions are required.

   @subsection ADDB-DLD-EVMGR-thread Threading and Concurrency Model

   The passthrough event manager, as discussed in the
   @ref ADDB-DLD-lspec-thread "main ADDB Design",
   performs no serialization of its own, depending on the Record-Sink to
   perform any necessary synchronization while posting.
 */

/**
   @addtogroup addb_pvt
   @{
 */

static bool addb_evmgr_invariant(struct m0_addb_mc_evmgr *mgr)
{
	if (mgr == NULL || mgr->evm_get == NULL || mgr->evm_put == NULL ||
	    mgr->evm_rec_alloc == NULL || mgr->evm_post == NULL)
		return false;
	if (mgr->evm_magic == M0_ADDB_CACHE_EVMGR_MAGIC)
		return mgr->evm_post_awkward && mgr->evm_copy != NULL;
	else if (mgr->evm_magic == M0_ADDB_PT_EVMGR_MAGIC)
		return !mgr->evm_post_awkward && mgr->evm_copy == NULL;
	return false;
}

static bool addb_pt_evmgr_invariant(struct addb_pt_evmgr *mgr)
{
	return mgr != NULL && addb_evmgr_invariant(&mgr->ape_evmgr) &&
	    mgr->ape_evmgr.evm_magic == M0_ADDB_PT_EVMGR_MAGIC;
}

static void addb_pt_evmgr_get(struct m0_addb_mc *mc,
			      struct m0_addb_mc_evmgr *mgr)
{
	struct addb_pt_evmgr *pt = container_of(mgr, struct addb_pt_evmgr,
						ape_evmgr);

	M0_PRE(addb_pt_evmgr_invariant(pt));
	m0_ref_get(&pt->ape_ref);
}

static void addb_pt_evmgr_put(struct m0_addb_mc *mc,
			      struct m0_addb_mc_evmgr *mgr)
{
	struct addb_pt_evmgr *pt = container_of(mgr, struct addb_pt_evmgr,
						ape_evmgr);

	M0_PRE(addb_pt_evmgr_invariant(pt));
	m0_ref_put(&pt->ape_ref);
}

/**
 * Frees the addb_pt_evmgr object associated with the reference.
 */
static void addb_pt_release(struct m0_ref *ref)
{
	struct m0_addb_mc_evmgr *evmgr;
	struct addb_pt_evmgr    *pt = container_of(ref, struct addb_pt_evmgr,
						   ape_ref);

	M0_PRE(addb_pt_evmgr_invariant(pt));
	evmgr = &pt->ape_evmgr;
	evmgr->evm_magic = 0;
	m0_free(pt);
}

static void addb_pt_log(struct m0_addb_mc *mc, struct m0_addb_post_data *pd)
{
	/** @todo implement */
}

static bool addb_cache_evmgr_invariant(struct addb_cache_evmgr *mgr)
{
	return mgr != NULL && addb_evmgr_invariant(&mgr->ace_evmgr) &&
	    mgr->ace_evmgr.evm_magic == M0_ADDB_CACHE_EVMGR_MAGIC;
}

static void addb_cache_evmgr_get(struct m0_addb_mc *mc,
				 struct m0_addb_mc_evmgr *mgr)
{
	struct addb_cache_evmgr *cache =
		container_of(mgr, struct addb_cache_evmgr, ace_evmgr);

	M0_PRE(addb_cache_evmgr_invariant(cache));
	m0_ref_get(&cache->ace_ref);
}

static void addb_cache_evmgr_put(struct m0_addb_mc *mc,
			      struct m0_addb_mc_evmgr *mgr)
{
	struct addb_cache_evmgr *cache =
		container_of(mgr, struct addb_cache_evmgr, ace_evmgr);

	M0_PRE(addb_cache_evmgr_invariant(cache));
	m0_ref_put(&cache->ace_ref);
}

/**
 * Frees the addb_cache_evmgr object associated with the reference.
 */
static void addb_cache_release(struct m0_ref *ref)
{
	struct m0_addb_mc_evmgr *evmgr;
	struct addb_cache_evmgr *cache =
		container_of(ref, struct addb_cache_evmgr, ace_ref);

	M0_PRE(addb_cache_evmgr_invariant(cache));

	evmgr = &cache->ace_evmgr;
	evmgr->evm_magic = 0;
	addb_ts_fini(&cache->ace_ts);
	m0_free(cache);
}

static struct m0_addb_rec *addb_cache_evmgr_rec_alloc(struct m0_addb_mc *mc,
						      size_t len)
{
	struct m0_addb_ts_rec   *ts_rec;
	struct addb_cache_evmgr *cache =
		container_of(mc->am_evmgr, struct addb_cache_evmgr, ace_evmgr);

	M0_PRE(M0_IS_8ALIGNED(len));
	ts_rec = addb_ts_alloc(&cache->ace_ts, len);

	return (struct m0_addb_rec *)(ts_rec != NULL ? ts_rec->atr_data : NULL);
}

static void addb_cache_evmgr_rec_save(struct m0_addb_mc *mc,
				      struct m0_addb_rec *rec)
{
	struct m0_addb_ts_rec    *ts_rec;
	struct addb_cache_evmgr  *cache =
		container_of(mc->am_evmgr, struct addb_cache_evmgr, ace_evmgr);

	M0_PRE(rec != NULL && M0_IS_8ALIGNED(rec));

	M0_ASSERT(cache != NULL);
	ts_rec = container_of((uint64_t *)rec, struct m0_addb_ts_rec,
			      atr_data[0]);
	addb_ts_save(&cache->ace_ts, ts_rec);
}

static int addb_cache_evmgr_flush(struct m0_addb_mc *src_mc,
				  struct m0_addb_mc *dest_mc)
{
	struct m0_addb_ts_rec    *ts_rec = NULL;
	struct addb_cache_evmgr  *cache =
		container_of(src_mc->am_evmgr, struct addb_cache_evmgr,
			     ace_evmgr);
	struct m0_addb_rec       *addb_rec;
	uint32_t                  rec_len;

	M0_PRE(ergo(src_mc != NULL, addb_mc_invariant(src_mc)));
	M0_PRE(ergo(dest_mc != NULL, addb_mc_invariant(dest_mc)));

	ts_rec = addb_ts_get(&cache->ace_ts, cache->ace_ts.at_page_size);
	rec_len = ADDB_TS_GET_REC_SIZE(&ts_rec->atr_header);
	M0_ASSERT(rec_len != 0);

	while (ts_rec != NULL) {
		addb_rec = dest_mc->am_evmgr->evm_rec_alloc(dest_mc, rec_len);

		if (addb_rec == NULL) {
			/* Add addb rec back to cache evmgr's TS headers list */
			addb_ts_save(&cache->ace_ts, ts_rec);
			return M0_ERR(-ENOMEM, "Dest addb mc's rec alloc");
		}

		memcpy(addb_rec, ts_rec->atr_data, rec_len);
		dest_mc->am_evmgr->evm_post(dest_mc, addb_rec);
		addb_ts_free(&cache->ace_ts, ts_rec);
		ts_rec = addb_ts_get(&cache->ace_ts,
				     cache->ace_ts.at_page_size);
	}

	return 0;
}

/** @} addb_pvt */

/* Public interfaces */

M0_INTERNAL void m0_addb_mc_configure_pt_evmgr(struct m0_addb_mc *mc)
{
	struct addb_pt_evmgr    *pt;
	struct m0_addb_mc_evmgr *evmgr;

	M0_PRE(!m0_addb_mc_has_evmgr(mc));
	M0_PRE(m0_addb_mc_has_recsink(mc));
	M0_ALLOC_PTR(pt);
	if (pt == NULL) {
		M0_LOG(M0_ERROR, "Unable to allocate memory for pt evmgr");
		M0_ASSERT(pt != NULL);
	}
	evmgr = &pt->ape_evmgr;
	evmgr->evm_post_awkward = false;
	evmgr->evm_get = addb_pt_evmgr_get;
	evmgr->evm_put = addb_pt_evmgr_put;
	evmgr->evm_rec_alloc = mc->am_sink->rs_rec_alloc;
	evmgr->evm_post = mc->am_sink->rs_save;
	evmgr->evm_copy = NULL;
	evmgr->evm_log = addb_pt_log;
	m0_ref_init(&pt->ape_ref, 0, addb_pt_release);
	evmgr->evm_magic = M0_ADDB_PT_EVMGR_MAGIC;
	mc->am_evmgr = evmgr;
	(*evmgr->evm_get)(mc, evmgr); /* also checks the pt invariant */
}

M0_INTERNAL int m0_addb_mc_configure_cache_evmgr(struct m0_addb_mc *mc,
						 uint32_t           npages,
						 m0_bcount_t        pgsize)
{
	struct addb_cache_evmgr *cache;
	struct m0_addb_mc_evmgr *evmgr;
	int                      rc;

	M0_PRE(mc != NULL);
	M0_PRE(npages != 0);
	M0_PRE(M0_IS_8ALIGNED(pgsize));
	M0_PRE(!m0_addb_mc_has_evmgr(mc));
	M0_PRE(!m0_addb_mc_has_recsink(mc));

	M0_ALLOC_PTR(cache);
	if (cache == NULL)
		return M0_ERR(-ENOMEM, "Unable to allocate memory for "
				"cache evmgr");

	rc = addb_ts_init(&cache->ace_ts, npages, npages, pgsize);
	if (rc != 0) {
		m0_free(cache);
		return M0_ERR(rc, "Cannot initialize addb transient store");
	}

	evmgr = &cache->ace_evmgr;
	evmgr->evm_post_awkward = true;
	evmgr->evm_get = addb_cache_evmgr_get;
	evmgr->evm_put = addb_cache_evmgr_put;
	evmgr->evm_rec_alloc = addb_cache_evmgr_rec_alloc;
	evmgr->evm_post = addb_cache_evmgr_rec_save;
	evmgr->evm_copy = addb_cache_evmgr_flush;
	evmgr->evm_log = NULL;
	m0_ref_init(&cache->ace_ref, 0, addb_cache_release);
	evmgr->evm_magic = M0_ADDB_CACHE_EVMGR_MAGIC;
	mc->am_evmgr = evmgr;
	(*evmgr->evm_get)(mc, evmgr);

	return rc;
}

M0_INTERNAL void m0__addb_post(struct m0_addb_mc *mc,
			       struct m0_addb_post_data *pd)
{
	uint64_t *fields = NULL;
	size_t    fields_nr = 0;

	if (!m0_addb_mc_is_configured(mc))
		return;
	M0_PRE(m0_addb_mc_has_evmgr(mc));
	M0_PRE(addb_rec_type_invariant(pd->apd_rt));
	M0_PRE(pd->apd_cv != NULL && pd->apd_cv[0] != NULL);

	switch (pd->apd_rt->art_base_type) {
	case M0_ADDB_BRT_EX:
	case M0_ADDB_BRT_DP:
	case M0_ADDB_BRT_STATS:
		fields_nr = pd->apd_rt->art_rf_nr;
		fields = pd->u.apd_args;
		break;
	case M0_ADDB_BRT_CNTR:
		M0_PRE(pd->apd_rt == pd->u.apd_cntr->acn_rt);
		fields_nr = sizeof(*pd->u.apd_cntr->acn_data) /
			    sizeof(uint64_t);
		if (pd->apd_rt->art_rf_nr > 0)
			fields_nr += pd->apd_rt->art_rf_nr + 1;
		fields = (uint64_t *)pd->u.apd_cntr->acn_data;
		break;
	case M0_ADDB_BRT_SM_CNTR:
		M0_PRE(pd->apd_rt == pd->u.apd_sm_cntr->asc_rt);
		fields_nr = m0_addb_sm_counter_data_size(pd->apd_rt) /
			    sizeof(uint64_t);
		fields = (uint64_t *)pd->u.apd_sm_cntr->asc_data;
		break;
	case M0_ADDB_BRT_SEQ:
		fields_nr = pd->u.apd_seq->au64s_nr;
		fields = pd->u.apd_seq->au64s_data;
		break;
	default:
		M0_ASSERT(pd->apd_rt->art_base_type <= M0_ADDB_BRT_SEQ);
	}

	addb_rec_post(mc, m0_addb_rec_rid_make(pd->apd_rt->art_base_type,
					       pd->apd_rt->art_id),
		      pd->apd_cv, fields, fields_nr);
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
