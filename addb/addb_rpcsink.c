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
 *                  Dave Cohrs <dave_cohrs@xyratex.com>
 * Original creation: 08/17/2012
 * Revision: Rajanikant Chirmade <rajanikant_chirmade@xyratex.com>
 * Revision Date: 02/11/2013
 */

/**
   @page ADDB-DLD-RSINK RPC Sink Detailed Design

   This is a component DLD and hence not all sections are present.
   Refer to @ref ADDB-DLD "ADDB Detailed Design"
   for the design requirements.

   - @ref ADDB_DLD_RSINK-ovw
   - @ref ADDB-DLD-RSINK-depends
	- @ref ADDB-DLD-RSINK-depends-rpc
	- @subpage ADDB-DLD-TS "Transient Store"
   - @ref ADDB-DLD-RSINK-fspec
      - @ref ADDB-DLD-RSINK-fspec-ds
      - @ref ADDB-DLD_RSINK-fspecs-fop
      - @ref ADDB-DLD-RSINK-fspec-int_if
      - @ref ADDB-DLD-RSINK-fspec-ext_if
   - @ref ADDB-DLD-RSINK-lspec
      - @ref ADDB-DLD-RSINK-lspec-populate_addb_rpc_sink_fop
      - @ref ADDB-DLD-RSINK-lspec-state
      - @ref ADDB-DLD-RSINK-lspec-thread
      - @ref ADDB-DLD-RSINK-lspec-numa
   - @ref ADDB-DLD-RSINK-ut
   - @ref ADDB-DLD-RSINK-O
      - @ref ADDB-DLD-RSINK-O-perf
      - @ref ADDB-DLD-RSINK-O-rationale

   <hr>
   @section ADDB_DLD_RSINK-ovw Overview
   The ADDB RPC Sink is used by an @ref ADDB-DLD-lspec-mc "ADDB Machine" to
   store ADDB records temporarily in @ref ADDB-DLD-TS "Transient Store" on
   mero nodes which are not configured with persistent ADDB repository.
   These ADDB-DLD-TS-lspec-trans_rec "Transient Records" are pulled by RPC
   subsystem to fill remaining space in RPC packet intended to send mero
   server running ADDB service.

   <hr>
   @section ADDB-DLD-RSINK-depends RPC Sink Dependencies
   @subsection ADDB-DLD-RSINK-depends-rpc RPC Pull Mechanism

   The RPC sink component depends on the RPC subsystem to provide the
   mechanisms needed to send ADDB records to an ADDB service:

   -# ADDB records will be sent in a ::m0_rpc_item, either reusing the existing
   @ref fop "fop" data structure, or, alternatively, using a new non-fop data
   structure with corresponding changes to the request handler service delivery
   interface.  On the client side, instead of using m0_rpc_post() to transmit a
   pre-assembled item, a "pull" model will be used to create an item on demand
   to fill in the remaining space in an RPC packet during formation.  This is
   represented by a proposed m0_rpc_item_source object, which has to be
   registered by the higher level application after establishing a connection to
   a service with the m0_rpc_conn_create() subroutine:
@code
   struct m0_rpc_item_source_ops {
	bool             (*riso_has_item)(const struct m0_rpc_item_source *ris);
	struct m0_rpc_item *(*riso_get_item)(struct m0_rpc_item_source *ris,
                                             struct m0_rpc_session     *sess,//?
                                             size_t           available_space);
   };
   struct m0_rpc_item_source {
        uint64_t                             ris_magic;
        const char                          *ris_name;
	bool				     rios_flush;
	const struct m0_rpc_item_source_ops *ris_ops;
        struct m0_rpc_machine               *ris_machine;
        struct m0_tlink                      ris_linkage;
   };
   struct m0_rpc_machine {
        ...
        struct m0_tl rm_item_sources;
   };
   int m0_rpc_item_source_init(struct m0_rpc_item_source *ris,
                               const char *name,
			       const struct m0_rpc_item_source_ops *ops);
   int m0_rpc_item_source_fini(struct m0_rpc_item_source *ris);
   int m0_rpc_item_source_register(struct m0_rpc_conn *conn,
                                   struct m0_rpc_item_source *ris);
   int m0_rpc_item_source_deregister(struct m0_rpc_item_source *ris);
@endcode
   The callback subroutines are invoked within the scope of the RPC machine
   lock so should not make re-entrant calls to the RPC subsystem other than
   the following:
        - m0_rpc_item_init()
	- m0_rpc_item_fini()
	- m0_rpc_item_size()
   -# The ADDB subsystem will define a ::m0_rpc_item_source object for the
   higher level application (typically the Mero file system) to register on
   its behalf.  The application has access to configuration information and will
   use this to ensure that the ADDB provided source is only registered for
   connections to Mero servers that define an ADDB service.
   -# The RPC machine must make periodic sweeps over item sources to
   drain pending items that could not be packed into RPC packets.
   Presumably the RPC machine could do this off its existing timer threads.
   The following pseudo-code illustrates this:
@code
   size_t max_size = rpcmach->rm_min_rec_size - ITEM_ONWIRE_HEADER_SIZE;
   m0_tl_for(..., &rpcmach->rm_item_sources, itsrc) {
       while (itsrc->ris_ops->riso_has_item()) {
             // loop over sessions?
             struct m0_rpc_item *ri = itsrc->ris_ops->riso_get_item(...,
	                                                            max_size);
             if (ri != NULL)
	          m0_rpc__post_locked(ri);
       }
   } m0_tl_endfor;
@endcode
   The source creates a single ::m0_rpc_item that gets immediately posted; there
   may still be items left, so the inner loop will continue until each
   individual source is drained.  It would be overly complicated to try and
   optimize further by filling in any remaining space in an RPC packet with an
   item from a subsequent source.
   -# The RPC machine must provide one-way, best-effort support to transmit
     the ADDB rpc item.  No indication of remote reception is required.

   <hr>
   @section ADDB-DLD-RSINK-fspec RPC Sink Functional Specification
   @subsection ADDB-DLD-RSINK-fspec-ds Data Structures
       - rpcsink
       - rpcsink_item_source

   @subsection ADDB-DLD_RSINK-fspecs-fop ADDB FOP Definition
   Instead of defining some other structure to send ADDB messages re-use of
   FOP infrastructure is best option. ADDB fop can skip fop_ops which are
   are not useful in this case.
   Analysis on why ADDB FOP used to send ADDB records is here.
   @ref ADDB-DLD-RSINK-O-rationale

       - m0_addb_rpc_sink_fop

   @subsection ADDB-DLD-RSINK-fspec-int_if Internal Interfaces
       - rpcsink_get()
       - rpcsink_put()
       - rpcsink_alloc()
       - rpcsink_save()

   @subsection ADDB-DLD-RSINK-fspec-ext_if External Interfaces
       - m0_addb_mc_configure_rpc_sink()
       - m0_addb_mc_has_rpc_sink()
       - rpcsink_has_item()
       - rpcsink_get_item()
       - m0_addb_mc_rpc_sink_source_add();
       - m0_addb_mc_rpc_sink_source_del();

   <hr>
   @section ADDB-DLD-RSINK-lspec RPC Sink Logical Specification
   @subsection ADDB-DLD-RSINK-lspec-ts RPC sink transient store
   RPC sink uses @subpage ADDB-DLD-TS "transient store" to keep ADDB records
   temporarily before sending them to ADDB persistent store. Transient store is
   configured to extend itself to some maximum value if all the pre-allocated
   buffers are used. After the maximum value, transient record allocation will
   fail. Transient records will be deleted after RPC sub-system successfully
   send them to persistent store. Here successfully send means client side send
   call successful. One-way RPC item are used to send ADDB FOP.

   @subsection ADDB-DLD-RSINK-lspec-populate_addb_rpc_sink_fop Populate ADDB FOP
   Algorithm for ADDB RPC Sink FOP populate:

   @code
   int nrecs = 0;

   tsrecords_tlist_init(tsrecords_readytosend);
   while (1) {
	addb_rec = addb_ts_get(ts, SIZE);
	if (addb_rec == NULL)
		break;
	tsrecords_tlist_add(tsrecords_readytosend, addb_rec);
	++nrecs;
	// addb record length is in # of words (8 bytes)
	SIZE -= (addb_rec->atr_header.atrh_nr * 8);
   }

   if (nrecs > 0) {
	struct m0_addb_rpc_sink_fop addb_rpc_sink_fop_recs;
	int i;

	M0_ALLOC_PTR(addb_rpc_sink_fop_recs);

	addb_rpc_sink_fop_recs->af_records.af_nr = nrecs;
	M0_ALLOC_ARR(addb_rpc_sink_fop_recs->af_records.ars_data, nrecs);

	foreach (tsrecords_readytosend, addb_rec) {
		addb_rpc_sink_fop_recs->af_records.ars_data[i] = addb_rec;
	}
   }
   @endcode

   @subsection ADDB-DLD-RSINK-lspec-state Transient Record State Transition
   @dot
   digraph rpcsink {
       S0 [label="Allocated"]
       S1 [label="TSRecordList"]
       S2 [label="RPCSubmittedList"]
       S3 [label="Freed"]
       S0 -> S1 [label="Saved record in TS"];
       S1 -> S2 [label="Record submitted to RPC"];
       S2 -> S1 [label="Failed to send record"];
       S2 -> S3 [label="Record successfully sent"];
   }
   @enddot
   - Record sink allocates transient records from pre-allocated transient store
   buffers. After copying ADDB record at transient record pointer, that record
   is saved into transient record list.
   - RPC formation pulls the transient records of remaining size in RPC packet
   to send it to mero server node having persistence store.
   - If RPC subsystem fails to send transient records, these are placed back
   into transient record list to be sent again.

   @subsection ADDB-DLD-RSINK-lspec-thread Threading and Concurrency Model
   RPC sink is used by application thread to add ADDB record in transient store.
   It is also used by RPC formation on mero nodes to send transient ADDB
   records to servers. Two different threads do that work -
   -# Application thread on submission of RPC item to RPC subsystem and
      formation algorithm trigged,
   -# RPC worker threads which try to send timed-out RPC items

   A mutex, rpcsink::rs_mutex is used to serialize access through
   implementation of RPC item source operations (rpcsink_has_item()
   and rpcsink_get_item()).

   @subsection ADDB-DLD-RSINK-lspec-numa RPC Sink NUMA optimizations
   RPC sink has no threads of its own, and uses only the thread of its caller.
   RPC sink users (application threads who add ADDB records & threads runs RPC
   formation which pulls ADDB records to send them to persistent store) may
   not be from same locality in which transient store buffers allocated. Since
   these are application threads of execution, RPC sink does not have
   control on these threads. RPC sink can not do NUMA optimizations in these
   cases.

   <hr>
   @section ADDB-DLD-RSINK-ut RPC Sink Unit Tests
   @test Call m0_addb_mc_configure_rpc_sink(), RPC sink object allocation
   failed.

   @test Call m0_addb_mc_configure_rpc_sink(), Transient store initialization
   failed.

   @test Call m0_addb_mc_configure_rpc_sink(), ADDB item sources add failed.

   @test Call m0_addb_mc_configure_rpc_sink(), which configures record sink
   transient store.

   @test Call rpcsink_has_item() on empty transient store.

   @test Call rpcsink_alloc(), extends transient store by RPCSINK_TS_EXT_PAGES
   page if it not able to extend by default value.

   @test Call rpcsink_alloc(), extends transient store by 1 page if it not able
   to extend by default value.

   @test Call rpcsink_has_item() on non-empty transient store.

   @test Call rpcsink_get_item(), get transient records failed.

   @test Call rpcsink_get_item(), ADDB FOP preperation failed.

   @test Call rpcsink_get_item() with a single ADDB record in RPC item.

   @test Call rpcsink_get_item() with multiple ADDB records in RPC item.

   @test Call rpcsink_get_item() with transient store has more records
   than can fill in the remaining space but still it returned no record since
   transient store not having record which can fit in remaining space.

   @test Call rpcsink_item_sent() with send failed.

   @test Call rpcsink_item_sent() with send succeed.

   <hr>
   @section ADDB-DLD-RSINK-O RPC Sink Analysis
   @subsection ADDB-DLD-RSINK-O-perf Performance
   This may delay RPC packet send procedure, since RPC sink needs to allocate
   an RPC item for ADDB records. Unit benchmark can be added to analyse RPC
   packets transfer with ADDB records and without ADDB records.

   @subsection ADDB-DLD-RSINK-O-rationale Rationale
   There is an option using different structure (other than FOP) say ADDB_OP
   for sending ADDB records to mero server. But still ADDB_OP also needs
   RPC item embedded in it. Since RPC item type comes from fop type itself,
   ADDB_OP needs additional code to manage this dependency. This also
   require some more changes to create fom using ADDB_OP.

 */


#include "fop/fop.h"
#include "addb/addb_pvt.h"
#include "addb/addb_fops.h"
#include "rpc/rpc_machine.h"
#include "rpc/item_source.h"
#include "rpc/conn_internal.h"
#include "rpc/rpc_machine_internal.h"
#include "lib/finject.h"

/**
 * @defgroup addb_rpcsink ADDB RPC Sink interfaces
 * @{
 */

extern const struct m0_tl_descr rpc_conn_tl;

enum {
        RPCSINK_TS_EXT_PAGES  = 16,
};

static bool rpcsink_has_item(const struct m0_rpc_item_source *ris);
static void rpcsink_conn_terminating(struct m0_rpc_item_source *ris);
static struct m0_rpc_item *rpcsink_get_item(struct m0_rpc_item_source *ris,
					    m0_bcount_t                size);

static void rpcsink_item_sent(struct m0_rpc_item *item);

static const struct m0_rpc_item_source_ops rpcsink_source_ops = {
	.riso_has_item		= rpcsink_has_item,
	.riso_get_item		= rpcsink_get_item,
	.riso_conn_terminating  = rpcsink_conn_terminating,
};

static bool rpcsink_shutdown;

const struct m0_rpc_item_ops addb_rpc_sink_rpc_item_ops = {
	.rio_sent    = rpcsink_item_sent,
};

struct rpcsink_fop {
	uint64_t        rf_magic;
	struct m0_fop   rf_fop;
	struct rpcsink *rf_rsink;
};

struct rpcsink_item_source {
	uint64_t		   ris_magic;
	struct m0_rpc_item_source  ris_source;
	struct rpcsink		  *ris_rsink;
	/** Linkage to rpc sink item sources */
	struct m0_tlink            ris_link;
};

/**
 *  Internal RPC sink structure.
 */
struct rpcsink {
	/** Record sink object */
	struct m0_addb_mc_recsink rs_sink;
	/** Transient store for RPC sink */
	struct m0_addb_ts	  rs_ts;
	/** List of transient records submitted to RPC subsystem */
	struct m0_tl		  rs_rpc_submitted;
	/** RPC record sink sources */
	struct m0_tl		  rs_sources;
	struct m0_ref		  rs_ref;
	/* Lock to protect this data structure */
	struct m0_mutex		  rs_mutex;
	struct m0_addb_mc	 *rs_mc;
	struct m0_cond            rs_cond;
};

M0_TL_DESCR_DEFINE(rpcsink_trans_rec, "rpcsink transient records", static,
		   struct m0_addb_ts_rec_header, atrh_linkage, atrh_magic,
		   M0_ADDB_TS_LINK_MAGIC, M0_ADDB_RPCSINK_TS_HEAD_MAGIC1);
M0_TL_DEFINE(rpcsink_trans_rec, static, struct m0_addb_ts_rec_header);

M0_TL_DESCR_DEFINE(rpcsink_item_sources, "rpcsink item sources", static,
		   struct rpcsink_item_source, ris_link, ris_magic,
		   M0_ADDB_RPCSINK_ITEM_SOURCE_MAGIC,
		   M0_ADDB_RPCSINK_IS_HEAD_MAGIC);
M0_TL_DEFINE(rpcsink_item_sources, static, struct rpcsink_item_source);

M0_TL_DESCR_DEFINE(tsrecords, "tsrecords_readytosend", static,
		   struct m0_addb_ts_rec_header, atrh_linkage, atrh_magic,
		   M0_ADDB_TS_LINK_MAGIC,
		   M0_ADDB_RPCSINK_TS_HEAD_MAGIC2);
M0_TL_DEFINE(tsrecords, static, struct m0_addb_ts_rec_header);

static struct m0_rpc_machine *
rpcmachine_from_rpc_item_sources(const struct rpcsink *rsink)
{
	struct rpcsink_item_source *item_source;

	M0_PRE(!rpcsink_item_sources_tlist_is_empty(&rsink->rs_sources));

	item_source = rpcsink_item_sources_tlist_tail(&rsink->rs_sources);
	M0_ASSERT(item_source != NULL);

	return item_source->ris_source.ris_conn->c_rpc_machine;
}

/**
 * RPC sink item source invariant.
 */
static bool
rpcsink_item_source_invariant(const struct rpcsink_item_source *item_source)
{
	return item_source != NULL &&
	       item_source->ris_magic == M0_ADDB_RPCSINK_ITEM_SOURCE_MAGIC;
}

/**
 * RPC sink invariant.
 */
static bool rpcsink_invariant(const struct rpcsink *rsink)
{
	return rsink != NULL &&
	       rsink->rs_sink.rs_magic == M0_ADDB_RPCSINK_MAGIC;
}

static bool rpcsink_fop_invariant(const struct rpcsink_fop *rsfop)
{
	return rsfop != NULL &&
	       rsfop->rf_magic == M0_ADDB_RPCSINK_FOP_MAGIC;
}

static inline struct rpcsink *rpcsink_from_mc(struct m0_addb_mc *mc)
{
	M0_PRE(mc != NULL && mc->am_sink != NULL);
	return container_of(mc->am_sink, struct rpcsink, rs_sink);
}

static inline struct rpcsink_fop *rsfop_from_fop(struct m0_fop *fop)
{
	struct rpcsink_fop *rsfop;

	rsfop = container_of(fop, struct rpcsink_fop, rf_fop);
	M0_PRE(rpcsink_fop_invariant(rsfop));

	return rsfop;
}

static struct rpcsink *
rpcsink_from_rpc_item_src(const struct m0_rpc_item_source *ris)
{
	struct rpcsink_item_source *item_source =
		container_of(ris, struct rpcsink_item_source, ris_source);

	M0_PRE(rpcsink_item_source_invariant(item_source));
	M0_PRE(rpcsink_invariant(item_source->ris_rsink));

	return item_source->ris_rsink;
}

static struct m0_fop *rpcsink_fop_prepare(struct rpcsink *rsink, uint32_t nrecs)
{
	struct rpcsink_fop *rsfop;
	int		    rc;

	M0_ALLOC_PTR(rsfop);
	if (rsfop == NULL)
		return NULL;

	rc = m0_addb_rpc_sink_fop_init(rsfop, nrecs);
	if (rc != 0) {
		m0_free(rsfop);
		return NULL;
	}

	rsfop->rf_magic = M0_ADDB_RPCSINK_FOP_MAGIC;
	rsfop->rf_rsink = rsink;

	return &rsfop->rf_fop;
}

/**
 * Implementation of the m0_addb_mc_recsink::rs_get() method.
 */
static void rpcsink_get(struct m0_addb_mc *mc, struct m0_addb_mc_recsink *sink)
{
	struct rpcsink *rsink = container_of(sink, struct rpcsink, rs_sink);

	M0_PRE(rpcsink_invariant(rsink));
	m0_ref_get(&rsink->rs_ref);
}

/**
 * Implementation of the m0_addb_mc_recsink::rs_put() method.
 */
static void rpcsink_put(struct m0_addb_mc *mc, struct m0_addb_mc_recsink *sink)
{
	struct rpcsink *rsink = container_of(sink, struct rpcsink, rs_sink);

	M0_PRE(rpcsink_invariant(rsink));
	m0_ref_put(&rsink->rs_ref);
}

#define RPCSINK_ADJUST_EXT_PAGES(ts)	\
	((ADDB_TS_CUR_PAGES(ts) + RPCSINK_TS_EXT_PAGES) > ts->at_max_pages ? \
	 1 : RPCSINK_TS_EXT_PAGES)

#define RPCSINK_TS_REC_LEN(len)   \
        (len + sizeof(struct m0_addb_ts_rec))
/**
 * Implementation of the m0_addb_mc_recsink::rs_alloc() method.
 * It allocates transient records of provided length.
 */
static struct m0_addb_rec *rpcsink_alloc(struct m0_addb_mc *mc, size_t len)
{
	struct rpcsink        *rsink = rpcsink_from_mc(mc);
	struct m0_addb_ts     *ts = &rsink->rs_ts;
	struct m0_addb_ts_rec *ts_rec;
	size_t		       ts_rec_len = RPCSINK_TS_REC_LEN(len);

	m0_mutex_lock(&rsink->rs_mutex);
	do {
		if (M0_FI_ENABLED("extend_ts"))
			{ ts_rec = NULL; goto extend_ts; }
		ts_rec = addb_ts_alloc(ts, ts_rec_len);
extend_ts:
		;
	} while (ts_rec == NULL &&
		 ts_rec_len <= ts->at_page_size &&
		 addb_ts_extend(ts, RPCSINK_ADJUST_EXT_PAGES(ts)) == 0);
	m0_mutex_unlock(&rsink->rs_mutex);

	if (ts_rec != NULL)
		return (struct m0_addb_rec *)&ts_rec->atr_data[0];

	return NULL;
}

/**
 * Implementation of the m0_addb_mc_recsink::rs_save() method.
 * It appends transient record header into transient record list.
 */
static void rpcsink_save(struct m0_addb_mc *mc, struct m0_addb_rec *rec)
{
	struct rpcsink        *rsink = rpcsink_from_mc(mc);
	struct m0_addb_ts_rec *ts_rec;

	M0_PRE(rpcsink_invariant(rsink));
	M0_PRE(rec != NULL);

	ts_rec = container_of((uint64_t *)rec,
			      struct m0_addb_ts_rec,
			      atr_data[0]);

	m0_mutex_lock(&rsink->rs_mutex);
	addb_ts_save(&rsink->rs_ts, ts_rec);
	m0_mutex_unlock(&rsink->rs_mutex);
}
static void rpcsink_sources_cleanup(struct rpcsink *rsink)
{
	struct rpcsink_item_source *rsink_item_source;

	m0_tl_for(rpcsink_item_sources, &rsink->rs_sources, rsink_item_source) {
		m0_addb_mc_rpc_sink_source_del(&rsink_item_source->ris_source);
	} m0_tl_endfor;
}

static void rpcsink_release(struct m0_ref *ref)
{
	struct rpcsink             *rsink;

	rsink = container_of(ref, struct rpcsink, rs_ref);
	M0_PRE(rpcsink_invariant(rsink));

	m0_mutex_lock(&rsink->rs_mutex);
	rpcsink_shutdown = true;
	m0_mutex_unlock(&rsink->rs_mutex);
	if (!addb_ts_is_empty(&rsink->rs_ts)) {
		struct m0_rpc_machine *rm =
			rpcmachine_from_rpc_item_sources(rsink);
		m0_rpc_item_drain_sources_locked(rm);
	}

	m0_mutex_lock(&rsink->rs_mutex);

	/* Wait for submitted ADDB items to be sent by RPC subsystem. */
	if (!rpcsink_trans_rec_tlist_is_empty(&rsink->rs_rpc_submitted))
		m0_cond_wait(&rsink->rs_cond);

	addb_ts_fini(&rsink->rs_ts);

	rpcsink_trans_rec_tlist_fini(&rsink->rs_rpc_submitted);

	rpcsink_sources_cleanup(rsink);
	rpcsink_item_sources_tlist_fini(&rsink->rs_sources);
	rpcsink_shutdown = false;
	m0_mutex_unlock(&rsink->rs_mutex);

	m0_mutex_fini(&rsink->rs_mutex);
	m0_free(rsink);
}

static int rpcsink_item_source_init(struct rpcsink             *rsink,
				    struct rpcsink_item_source *rsinkis)
{
	rsinkis->ris_magic = M0_ADDB_RPCSINK_ITEM_SOURCE_MAGIC;
	rsinkis->ris_rsink = rsink;
	rpcsink_item_sources_tlink_init(rsinkis);

	return m0_rpc_item_source_init(&rsinkis->ris_source,
				       "RPC sink item source",
				       &rpcsink_source_ops);

}

static void rpcsink_item_source_fini(struct rpcsink_item_source *rsinkis)
{
	rpcsink_item_sources_tlink_fini(rsinkis);
	m0_rpc_item_source_fini(&rsinkis->ris_source);
}

static int addb_rpc_sink_get_records(struct rpcsink *rsink,
				     struct m0_tl *recs,
				     size_t size)
{
	struct m0_addb_ts_rec *addb_ts_rec;
	int                    nrecs = 0;

	while (1) {
		m0_mutex_lock(&rsink->rs_mutex);
		addb_ts_rec = addb_ts_get(&rsink->rs_ts, size);
		m0_mutex_unlock(&rsink->rs_mutex);
		if (addb_ts_rec == NULL)
			break;
		tsrecords_tlink_init(&addb_ts_rec->atr_header);
		tsrecords_tlist_add(recs, &addb_ts_rec->atr_header);
		++nrecs;
		/* addb record length is in # of words (8 bytes) */
		size -= ADDB_TS_GET_REC_SIZE(&addb_ts_rec->atr_header);
	}

	return nrecs;
}

static void addb_rpc_sink_fop_populate(struct rpcsink *rsink,
				       struct m0_addb_rpc_sink_fop *rsink_fop,
				       struct m0_tl *recs)
{
	struct m0_addb_ts_rec        *addb_ts_rec;
	struct m0_addb_ts_rec_header *header;
	struct m0_addb_ts_rec_data   *rsink_rec;
	int			      i  = 0;

	M0_PRE(rsink != NULL);
	M0_PRE(rsink_fop != NULL && rsink_fop->arsf_nr > 0);
	M0_PRE(tsrecords_tlist_length(recs) > 0);

	m0_tl_teardown(tsrecords, recs, header) {
		addb_ts_rec = container_of(header,
					   struct m0_addb_ts_rec,
					   atr_header);

		rsink_rec            = &(rsink_fop->arsf_recs[i++]);
		rsink_rec->atrd_data =
		(struct m0_addb_rec *)&addb_ts_rec->atr_data;
		rpcsink_trans_rec_tlink_init_at(header,
						&rsink->rs_rpc_submitted);
	}

	M0_POST(tsrecords_tlist_length(recs) == 0);
}

static void addb_rpc_sink_restore_recs(struct rpcsink *rsink,
				       struct m0_tl *recs)
{
	struct m0_addb_ts_rec_header *header;

	m0_tl_teardown(tsrecords, recs, header) {
		struct m0_addb_ts_rec *ts_rec =
			container_of(header, struct m0_addb_ts_rec, atr_header);

		m0_mutex_lock(&rsink->rs_mutex);
		addb_ts_save(&rsink->rs_ts, ts_rec);
		m0_mutex_unlock(&rsink->rs_mutex);
	}
}

/**
 * This operation is used by RPC formation to check if RPC sink
 * has addb records to send to mero server nodes.
 * @param ris RPC items source.
 * @return true If RPC sink has ADDB records to sink.
 * false If no ADDB records to sink.
 */
static bool rpcsink_has_item(const struct m0_rpc_item_source *ris)
{
	struct rpcsink *rsink = rpcsink_from_rpc_item_src(ris);

	return !addb_ts_is_empty(&rsink->rs_ts);
}

/**
 * This operation is used by RPC formation to get RPC item of ADDB records.
 * @param ris RPC items source.
 * @param size Available space in RPC packet.
 * @return pointer Pointer to RPC item.
 *         NULL if No ADDB record available <= size.
 * @pre size > 0
 */
static struct m0_rpc_item *rpcsink_get_item(struct m0_rpc_item_source *ris,
					    m0_bcount_t                size)
{
	struct m0_addb_rpc_sink_fop *addb_rsink_fop;
	struct rpcsink              *rsink = rpcsink_from_rpc_item_src(ris);
	struct m0_fop		    *fop;
	struct m0_tl		     recs;
	uint32_t		     nrecs;

	M0_PRE(size >= 0);

	tsrecords_tlist_init(&recs);
	if (M0_FI_ENABLED("get_records_failed"))
		{ nrecs = 0; goto get_records_failed; }
	nrecs = addb_rpc_sink_get_records(rsink, &recs, size);
get_records_failed:
	if (nrecs == 0)
		goto error;

	if (M0_FI_ENABLED("fop_prepare_failed"))
		{ fop = NULL; goto fop_prepare_failed; }
	fop = rpcsink_fop_prepare(rsink, nrecs);
fop_prepare_failed:
	if (fop == NULL)
		goto error;

	addb_rsink_fop = (struct m0_addb_rpc_sink_fop *)m0_fop_data(fop);
	addb_rpc_sink_fop_populate(rsink, addb_rsink_fop, &recs);

	return &fop->f_item;

error:
	if (!tsrecords_tlist_is_empty(&recs))
		addb_rpc_sink_restore_recs(rsink, &recs);

	tsrecords_tlist_fini(&recs);
	return NULL;
}

/**
 * This operation is used by RPC formation to notify rpcsink that connection
 * is terminating. m0_rpc_item_source can be free in this callback.
 * @pre ris != NULL
 */
static void rpcsink_conn_terminating(struct m0_rpc_item_source *ris)
{
	struct rpcsink_item_source *item_source =
		container_of(ris, struct rpcsink_item_source, ris_source);

	M0_PRE(rpcsink_item_source_invariant(item_source));

	rpcsink_item_sources_tlist_del(item_source);
	rpcsink_item_source_fini(item_source);

	m0_free(item_source);
}

#undef ADDB_REC
#define ADDB_REC(i) ((uint64_t *)fop_data->arsf_recs[i].atrd_data)
static void rpcsink_item_sent(struct m0_rpc_item *item)
{
	struct m0_fop               *fop;
	struct rpcsink              *rsink;
	struct rpcsink_fop          *rsfop;
	struct m0_addb_rpc_sink_fop *fop_data;
	int			     i;

	fop      = container_of(item, struct m0_fop, f_item);
	rsfop    = rsfop_from_fop(fop);
	rsink    = rsfop->rf_rsink;
	fop_data = (struct m0_addb_rpc_sink_fop *)m0_fop_data(fop);

	m0_mutex_lock(&rsfop->rf_rsink->rs_mutex);
	for (i = 0; i < fop_data->arsf_nr; ++i) {
		uint64_t              *addb_rec_ptr = ADDB_REC(i);
		struct m0_addb_ts_rec *ts_rec =
			container_of(addb_rec_ptr, struct m0_addb_ts_rec,
				     atr_data[0]);

		rpcsink_trans_rec_tlist_del(&ts_rec->atr_header);

		if (item->ri_sm.sm_rc != 0 && !rpcsink_shutdown)
			addb_ts_save(&rsfop->rf_rsink->rs_ts, ts_rec);
		else
			addb_ts_free(&rsfop->rf_rsink->rs_ts, ts_rec);
	}

	if (rpcsink_shutdown &&
	    rpcsink_trans_rec_tlist_is_empty(&rsink->rs_rpc_submitted))
			m0_cond_broadcast(&rsink->rs_cond);

	m0_mutex_unlock(&rsfop->rf_rsink->rs_mutex);
}
#undef ADDB_REC

static const struct m0_addb_mc_recsink rpcsink_ops = {
	.rs_magic	= M0_ADDB_RPCSINK_MAGIC,
	.rs_get		= rpcsink_get,
	.rs_put		= rpcsink_put,
	.rs_rec_alloc	= rpcsink_alloc,
	.rs_save	= rpcsink_save,
	.rs_save_seq	= NULL,
	.rs_skulk	= NULL,
};

/** @} */ /* end of addb_rpcsink group */

/* public interfaces */
M0_INTERNAL int
m0_addb_mc_configure_rpc_sink(struct m0_addb_mc     *mc,
			      struct m0_rpc_machine *rm,
			      uint32_t               npgs_init,
			      uint32_t		     npgs_max,
			      m0_bcount_t	     pg_size)
{
	struct rpcsink     *rsink;
	struct m0_rpc_conn *conn;
	int		    rc;

	M0_PRE(m0_addb_mc_is_initialized(mc));
	M0_PRE(!m0_addb_mc_has_recsink(mc));
	M0_PRE(rm != NULL);

	if (M0_FI_ENABLED("rsink_allocation_failed"))
		{ rsink = NULL; goto rsink_allocation_failed; }
	M0_ALLOC_PTR(rsink);
rsink_allocation_failed:
	if (rsink == NULL)
		M0_RETERR(-ENOMEM, "m0_addb_mc_configure_rpc_sink()");

	if (M0_FI_ENABLED("addb_ts_init_failed"))
		{ rc = -1; goto addb_ts_init_failed; }
	rc = addb_ts_init(&rsink->rs_ts, npgs_init, npgs_max, pg_size);
addb_ts_init_failed:
	if (rc != 0) {
		m0_free(rsink);
		M0_RETERR(rc, "m0_addb_mc_configure_rpc_sink()");
	}

	m0_ref_init(&rsink->rs_ref, 1, rpcsink_release);
	m0_mutex_init(&rsink->rs_mutex);
	m0_cond_init(&rsink->rs_cond, &rsink->rs_mutex);

	rpcsink_trans_rec_tlist_init(&rsink->rs_rpc_submitted);
	rpcsink_item_sources_tlist_init(&rsink->rs_sources);

	rsink->rs_mc   = mc;
	rsink->rs_sink = rpcsink_ops;
	mc->am_sink    = &rsink->rs_sink;

	/*
	 * Register rpcsink item source to all outgoing
	 * connections from rpc machine.
	 */
	if (M0_FI_ENABLED("skip_item_source_registration"))
		goto item_source_registration_skipped;

	if (M0_FI_ENABLED("item_source_registration_failed"))
		{ rc = -1; goto item_source_registration_failed; }

	m0_tl_for(rpc_conn, &rm->rm_outgoing_conns, conn) {
		rc = m0_addb_mc_rpc_sink_source_add(mc, conn);
		if (rc != 0)
			goto item_source_registration_failed;
	} m0_tl_endfor;

item_source_registration_skipped:
	rpcsink_shutdown = false;
	return 0;

item_source_registration_failed:
	mc->am_sink = NULL;
	m0_ref_put(&rsink->rs_ref);
	M0_RETERR(rc, "m0_addb_mc_configure_rpc_sink()");
}

M0_INTERNAL bool m0_addb_mc_has_rpc_sink(struct m0_addb_mc *mc)
{
	M0_PRE(mc != NULL);

	return mc->am_sink != NULL &&
	       mc->am_sink->rs_magic == M0_ADDB_RPCSINK_MAGIC;
}

M0_INTERNAL int m0_addb_mc_rpc_sink_source_add(struct m0_addb_mc  *mc,
					       struct m0_rpc_conn *conn)
{
	struct rpcsink_item_source *rpcsink_item_source;
	struct rpcsink             *rsink = rpcsink_from_mc(mc);
	int			    rc;

	M0_PRE(m0_addb_mc_has_rpc_sink(mc));

	M0_ALLOC_PTR(rpcsink_item_source);
	if (rpcsink_item_source == NULL)
		M0_RETERR(-ENOMEM, "m0_addb_mc_rpc_sink_source_add");

	rc = rpcsink_item_source_init(rpcsink_from_mc(mc), rpcsink_item_source);
	if (rc != 0) {
		m0_free(rpcsink_item_source);
		M0_RETERR(rc, "m0_addb_mc_rpc_sink_source_add");
	}

	m0_rpc_item_source_register(conn, &rpcsink_item_source->ris_source);

	rpcsink_item_sources_tlink_init(rpcsink_item_source);
	rpcsink_item_sources_tlist_add(&rsink->rs_sources, rpcsink_item_source);

	return rc;
}

/**
 * Stop sourcing items for the specified connection.
 * By default this would happen automatically when the
 * connection is closed, but this can be used to force
 * this behavior without closing the connection.
 * @pre m0_addb_mc_has_rpc_sink(mc);
 */
M0_INTERNAL void m0_addb_mc_rpc_sink_source_del(struct m0_rpc_item_source *src)
{
	struct rpcsink_item_source *item_source;

	item_source  = container_of(src, struct rpcsink_item_source,
				    ris_source);

	m0_rpc_item_source_deregister(src);
	rpcsink_item_sources_tlist_del(item_source);
	rpcsink_item_source_fini(item_source);

	m0_free(item_source);
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
