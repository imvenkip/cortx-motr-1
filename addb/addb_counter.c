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
 *		    Rohan Puri <rohan_puri@xyratex.com>
 * Original creation: 10/11/2012
 */

#include "sm/sm.h"

/**
   @page ADDB-DLD-CNTR Counter Detailed Design

   This is a component DLD and hence not all sections are present.
   Refer to the @ref ADDB-DLD "ADDB Detailed Design"
   for the design requirements.

   - @ref ADDB-DLD-CNTR-ovw
   - @ref ADDB-DLD-CNTR-fspec
   - @ref ADDB-DLD-CNTR-lspec
      - @ref ADDB-DLD-CNTR-lspec-usage
      - @ref ADDB-DLD-CNTR-lspec-rec
      - @ref ADDB-DLD-CNTR-lspec-thread

   <hr>
   @section ADDB-DLD-CNTR-ovw Overview

   An ADDB Counter collects statistics of numeric samples of an
   application-defined behavior.  The application defines the semantics of what
   is being sampled.  Helper functions are provided to manage the counter.

   <hr>
   @section ADDB-DLD-CNTR-fspec Functional Specification

   @see @ref addb_counter

   <hr>
   @section ADDB-DLD-CNTR-lspec Logical Specification
   - @ref ADDB-DLD-CNTR-lspec-usage
   - @ref ADDB-DLD-CNTR-lspec-rec
   - @ref ADDB-DLD-CNTR-lspec-sm_counter
   - @ref ADDB-DLD-CNTR-lspec-thread

   @subsection ADDB-DLD-CNTR-lspec-usage Counter Behavior

   A counter is based on a counter record type.  Counter record types are
   defined using the M0_ADDB_RT_CNTR() macro.

   Given a counter record type, a counter can be initialized using
   m0_addb_counter_init().  Internally, this function will set the fields of the
   provided m0_addb_counter object.  It will also allocate space for accounting
   for the histogram buckets if they are required for the counter, as defined in
   the provided m0_addb_rec_type.

   The application is responsible for updating the counter with sample data by
   calling m0_addb_counter_update().  Internally, this function updates the
   various statistical fields of the counter.  One field in particular,
   m0_addb_counter::acn_sum_sq, can overflow.  -EOVERFLOW is returned if
   such an overflow is detected, and the counter is not updated.  This allows
   the application to post the counter using M0_ADDB_POST_CNTR() and re-apply
   the update.  Because the sum of squares is stored as a @c uint64_t, the
   effective maximum datum that can be sampled is \f$ 2^{31} - 1 \f$.  The
   application should scale its data accordingly.

   The application is also responsible for periodically posting the counter.
   General posting behavior is covered in @ref ADDB-DLD-EVMGR.  The
   M0_ADDB_POST_CNTR() macro is used to post an ADDB counter.  This macro
   internally allocates an appropriately sized m0_addb_post_data and copies the
   current values of the counter into this object and calls the lower level
   m0__addb_post() to post the counter to the event manager.  Then, the
   statistical fields of the counter are reset using the internal
   m0__addb_counter_reset() and the m0_addb_counter::acn_seq is incremented.
   This behavior is necessary to keep the counter fields from overflowing.

   An analysis application that wishes to make statistical evaluations across
   posting intervals can, assuming it uses higher precision fields, compute its
   own statistics across several posting intervals.  It can detect missing data
   in a time-ordered stream of counter events when there is a gap between
   consecutive m0_addb_counter::acn_seq values.  It is up to the analysis
   application to handle such gaps appropriately.

   When a counter is no longer needed, the application calls
   m0_addb_counter_fini().  This function will free any memory allocated for
   collecting histogram statistics.

   @subsection ADDB-DLD-CNTR-lspec-rec Counter record representation

   When the counter is posted, its fields must be copied to an ADDB event
   record, m0_addb_rec.  Specifically, the m0_addb_rec::ar_data for a counter
   contains the following:

   @verbatim
                +---------------+
   ar_data[0]   | acn_seq       |
                +---------------+
   ar_data[1]   | acn_nr        |
                +---------------+
   ar_data[2]   | acn_total     |
                +---------------+
   ar_data[3]   | acn_min       |
                +---------------+
   ar_data[4]   | acn_max       |
                +---------------+
   ar_data[5]   | acn_sum_sq    |
                +---------------+
   ar_data[6]   | acn_hist[0]   |
                +---------------+
   ar_data[7]   | acn_hist[1]   |
                +---------------+
   ar_data[...] | acn_hist[...] |
                +---------------+
   @endverbatim

   Note that if the record type specifies no histogram, no histogram
   information will be included in the record.

   @subsection ADDB-DLD-CNTR-lspec-sm_counter State Machine Counter

   State machine counter (m0_addb_sm_counter) is the slight extension
   of ordinary counter (m0_addb_counter) to efficiently support an
   array of counters data for state machines transitions.

   State machine counter record type references the static state
   machine configuration which describes its transitions. Each
   transition in state machine counter has its own counter data
   element in the array (m0_addb_sm_counter::asc_data).  The size
   of counter data element is determined from counter record type
   (in the same way as for ordinary counter). The number of elements
   in the array correspondent to the number of transitions in the
   state machine configuration.

   There is one essential difference between ordinary counter and
   state machine counter in respect to memory allocation: sm counter
   do not allocate the memory for its data. sm counter user should
   provide sufficient amount of memory via m0_addb_sm_counter()
   routine. m0_addb_sm_counter_data_size() routine can be used by
   user to determine the amount of needed memory.

   @see <a href="http://goo.gl/vlrJ5">ADDB Implementation Plan</a> document.

   @subsection ADDB-DLD-CNTR-lspec-thread Threading and Concurrency Model

   It is incumbent on the application to serialize calls to the ADDB counter
   functions and the post macro for a given counter.  The ADDB counter has no
   inherent serialization.
 */


/**
   @addtogroup addb_pvt
   @see @ref addb "Analysis and Data-Base API"
   @{
 */

static bool addb_counter_invariant(const struct m0_addb_counter *c)
{
	return c != NULL &&
	    c->acn_magic == M0_ADDB_CNTR_MAGIC &&
	    c->acn_rt != NULL && addb_rec_type_invariant(c->acn_rt) &&
	    c->acn_rt->art_base_type == M0_ADDB_BRT_CNTR &&
	    c->acn_data != NULL;
}

static bool addb_sm_counter_invariant(const struct m0_addb_sm_counter *c)
{
	return c != NULL &&
	    c->asc_magic == M0_ADDB_CNTR_MAGIC &&
	    c->asc_rt != NULL && addb_rec_type_invariant(c->asc_rt) &&
	    c->asc_rt->art_base_type == M0_ADDB_BRT_SM_CNTR &&
	    c->asc_data != NULL &&
	    c->asc_cntr_data_sz > 0 &&
	    c->asc_rt->art_sm_conf != NULL &&
	    c->asc_rt->art_sm_conf->scf_trans != NULL &&
	    c->asc_rt->art_sm_conf->scf_trans_nr > 0;
}

static size_t counter_data_sz(const struct m0_addb_rec_type *rt)
{
	return (sizeof(struct m0_addb_counter_data) +
	        ((rt->art_rf_nr > 0) ?
		 (rt->art_rf_nr + 1) * sizeof(uint64_t) : 0));
}

/* public interfaces */
M0_INTERNAL int m0_addb_counter_init(struct m0_addb_counter *c,
				     const struct m0_addb_rec_type *rt)
{
	M0_ENTRY();
	M0_PRE(c != NULL);
	M0_PRE(c->acn_data == NULL);
	M0_PRE(c->acn_magic == 0);
	M0_PRE(addb_rec_type_invariant(rt));
	M0_PRE(rt->art_base_type == M0_ADDB_BRT_CNTR);

	c->acn_rt = rt;
	c->acn_data = m0_alloc(counter_data_sz(rt));
	if (c->acn_data == NULL)
		return M0_ERR(-ENOMEM, "counter_init");
	c->acn_magic = M0_ADDB_CNTR_MAGIC;

	M0_POST(addb_counter_invariant(c));
	return M0_RC(0);
}
M0_EXPORTED(m0_addb_counter_init);

M0_INTERNAL void m0_addb_counter_fini(struct m0_addb_counter *c)
{
	M0_ENTRY();
	if (c->acn_magic != 0) {
		M0_PRE(addb_counter_invariant(c));
		c->acn_magic = 0;
		c->acn_rt = NULL;
		m0_free0(&c->acn_data);
	}
	M0_LEAVE();
}
M0_EXPORTED(m0_addb_counter_fini);

static int counter_data_update(struct m0_addb_counter_data *data,
			       const struct m0_addb_rec_type *rt,
			       uint64_t datum)
{
 
/**
 * @todo Counter overflow issue started comming again while
 *       running ST after added FOM phase statistics counters.
 *
 * Reopening Jira "Mero-8: ADDB Counter update error."
 */
	if (m0_addu64_will_overflow(data->acd_sum_sq, datum * datum))
		return M0_ERR(-EOVERFLOW, "%s: counter's sum of samples "
				"square overflow: datum=%llu", rt->art_name,
			  (unsigned long long)datum);

	++data->acd_nr;
	data->acd_total += datum;
	if (data->acd_nr > 1) {
		data->acd_min = min64u(data->acd_min, datum);
		data->acd_max = max64u(data->acd_max, datum);
	} else {
		data->acd_min = datum;
		data->acd_max = datum;
	}
	data->acd_sum_sq += datum * datum;

	/*
	 * Update histogram values
	 */
	if (rt->art_rf_nr != 0) {
		int i;
		for (i = 0; i < rt->art_rf_nr; ++i)
			if (datum < rt->art_rf[i].arfu_lower)
				break;
		++data->acd_hist[i];
	}
	return 0;
}

M0_INTERNAL int m0_addb_counter_update(struct m0_addb_counter *c,
				       uint64_t datum)
{
	int res;

	M0_ENTRY();
	M0_PRE(addb_counter_invariant(c));

	res = counter_data_update(c->acn_data, c->acn_rt, datum);
	if (res != 0)
		return M0_RC(res);

	M0_POST(addb_counter_invariant(c));
	return M0_RC(0);
}
M0_EXPORTED(m0_addb_counter_update);

M0_INTERNAL uint64_t m0_addb_counter_nr(const struct m0_addb_counter *c)
{
	M0_PRE(addb_counter_invariant(c));
	return c->acn_data->acd_nr;
}
M0_EXPORTED(m0_addb_counter_nr);

static void counter_data_reset(struct m0_addb_counter_data *cd, size_t sz)
{
	uint64_t seq = cd->acd_seq + 1;

	memset(cd, 0, sz);
	cd->acd_seq = seq;
}

M0_INTERNAL void m0__addb_counter_reset(struct m0_addb_counter *c)
{
	M0_ENTRY();
	M0_PRE(addb_counter_invariant(c));

	counter_data_reset(c->acn_data, counter_data_sz(c->acn_rt));

	M0_POST(addb_counter_invariant(c));
	M0_LEAVE();
}
M0_EXPORTED(m0__addb_counter_reset);

M0_INTERNAL void m0_addb_post_cntr(struct m0_addb_mc       *mc,
				   struct m0_addb_ctx     **cv,
				   struct m0_addb_counter  *c)
{
	M0_PRE(addb_counter_invariant(c));

	if (m0_addb_counter_nr(c) > 0)
                M0_ADDB_POST_CNTR(mc, cv, c);
}
M0_EXPORTED(m0_addb_post_cntr);

M0_INTERNAL void m0_addb_sm_counter_init(struct m0_addb_sm_counter *c,
					 const struct m0_addb_rec_type *rt,
					 void *data, size_t data_sz)
{
	M0_ENTRY();
	M0_PRE(c != NULL);
	M0_PRE(c->asc_data == NULL);
	M0_PRE(c->asc_magic == 0);
	M0_PRE(addb_rec_type_invariant(rt));
	M0_PRE(rt->art_base_type == M0_ADDB_BRT_SM_CNTR);
	M0_PRE(data != NULL);
	M0_PRE(data_sz >= m0_addb_sm_counter_data_size(rt));

	c->asc_rt = rt;
	c->asc_data = data;
	c->asc_cntr_data_sz = counter_data_sz(rt);
	c->asc_magic = M0_ADDB_CNTR_MAGIC;
	c->asc_nr = 0;

	M0_POST(addb_sm_counter_invariant(c));
}
M0_EXPORTED(m0_addb_sm_counter_init);

M0_INTERNAL void m0_addb_sm_counter_fini(struct m0_addb_sm_counter *c)
{
	M0_ENTRY();
	if (c->asc_magic != 0) {
		M0_PRE(addb_sm_counter_invariant(c));
		c->asc_magic = 0;
		c->asc_rt    = NULL;
		c->asc_data  = NULL;
		c->asc_nr    = 0;
	}
	M0_LEAVE();
}
M0_EXPORTED(m0_addb_sm_counter_fini);

M0_INTERNAL int m0_addb_sm_counter_update(struct m0_addb_sm_counter *c,
					  uint32_t idx,
					  uint64_t datum)
{
	int res;

	M0_ENTRY();
	M0_PRE(addb_sm_counter_invariant(c));
	M0_PRE(idx < c->asc_rt->art_sm_conf->scf_trans_nr);

	res = counter_data_update((void*)c->asc_data +
				  c->asc_cntr_data_sz * idx,
				  c->asc_rt, datum);
	if (res != 0)
		return M0_RC(res);

	++c->asc_nr;

	M0_POST(addb_sm_counter_invariant(c));
	return M0_RC(0);
}
M0_EXPORTED(m0_addb_sm_counter_update);

M0_INTERNAL uint64_t m0_addb_sm_counter_nr(const struct m0_addb_sm_counter *c)
{
	return c->asc_nr;
}
M0_EXPORTED(m0_addb_sm_counter_nr);

M0_INTERNAL void m0__addb_sm_counter_reset(struct m0_addb_sm_counter *c)
{
	int i;

	M0_ENTRY();
	M0_PRE(addb_sm_counter_invariant(c));

	for (i = 0; i < c->asc_rt->art_sm_conf->scf_trans_nr; ++i)
		counter_data_reset((void*)c->asc_data + c->asc_cntr_data_sz * i,
		                   c->asc_cntr_data_sz);
	c->asc_nr = 0;

	M0_POST(addb_sm_counter_invariant(c));
	M0_LEAVE();
}
M0_EXPORTED(m0__addb_sm_counter_reset);

M0_INTERNAL size_t
m0_addb_sm_counter_data_size(const struct m0_addb_rec_type *rt)
{
	M0_PRE(addb_rec_type_invariant(rt) &&
	       rt->art_base_type == M0_ADDB_BRT_SM_CNTR);

	return counter_data_sz(rt) * rt->art_sm_conf->scf_trans_nr;
}
M0_EXPORTED(m0_addb_sm_counter_data_size);

/** @} addb_pvt */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
