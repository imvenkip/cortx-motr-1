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
 * Original creation date: 08/14/2012
 */

#pragma once

#ifndef __MERO_ADDB_ADDB_PVT_H__
#define __MERO_ADDB_ADDB_PVT_H__

#include "addb/addb.h"
#include "lib/vec.h"
#include "lib/bitmap.h"

/**
 * @todo REMOVE this comment, when this file included in addb.h
 * is removed
 */
#if 0
#include "mero/magic.h"
#endif

/**
   @page ADDB-DLD-fspec Functional Specification
   - @ref ADDB-DLD-fspec-ds
   - @ref ADDB-DLD-fspec-sub
   - @ref ADDB-DLD-fspec-cli
   - @ref ADDB-DLD-fspec-usecases
     - @ref ADDB-DLD-fspec-uc-TSMC
     - @ref ADDB-DLD-fspec-uc-PSMC
     - @ref ADDB-DLD-fspec-uc-AWKMC
     - @ref ADDB-DLD-fspec-uc-reg
     - @ref ADDB-DLD-fspec-uc-mod-ctx
     - @ref ADDB-DLD-fspec-uc-post
     - @ref ADDB-DLD-fspec-uc-cntr
     - @ref ADDB-DLD-fspec-uc-read
   - Detailed functional specifications:
     - @ref addb "Analysis and Diagnostics Data-Base API"
     - @ref addb_pvt "ADDB Internal Interfaces"

   <hr>
   @section ADDB-DLD-fspec-ds Data Structures

   The following data types are used to describe data and meta-data:
   - ::m0_addb_base_rec_type
   - m0_addb_counter
   - m0_addb_ctx_type
   - m0_addb_rec
   - m0_addb_rec_seq
   - m0_addb_rec_type
   - m0_addb_rf_u
   - m0_addb_uint64_seq

   The following data types are used during record posting:
   - m0_addb_ctx
   - m0_addb_rec

   The following data structures are involved in an ADDB machine:
   - m0_addb_mc
   - m0_addb_mc_evmgr
   - m0_addb_mc_recsink

   <hr>
   @section ADDB-DLD-fspec-sub Subroutines and Macros

   Interfaces to handle meta-data definition and declaration
   - M0_ADDB_CT()
   - M0_ADDB_RT_CNTR()
   - M0_ADDB_RT_DP()
   - M0_ADDB_RT_EX()
   - M0_ADDB_RT_SEQ()

   The four M0_ADDB_RT_ macro variants create declarations or definitions of the
   record type described by their arguments, depending on whether the predicate
   macro ::M0_ADDB_RT_CREATE_DEFINITION is defined before the <addb/addb.h>
   header file is included.

   The M0_ADDB_CT macro creates declarations or definitions of the context
   type described by its arguments, depending on whether the predicate macro
   ::M0_ADDB_CT_CREATE_DEFINITION is defined before the <addb/addb.h> header
   file is included.

   Meta-data must be registered before use with the following interfaces:
   - m0_addb_ctx_type_register()
   - m0_addb_rec_type_register()

   Interfaces to manage ADDB machines:
   - m0_addb_mc_cache_evmgr_flush()
   - m0_addb_mc_can_post_awkward()
   - m0_addb_mc_configure_cache_evmgr()
   - m0_addb_mc_configure_pt_evmgr()
   - m0_addb_mc_configure_rpc_sink()
   - m0_addb_mc_configure_stob_sink()
   - m0_addb_mc_dup()
   - m0_addb_mc_fini()
   - m0_addb_mc_has_evmgr()
   - m0_addb_mc_has_recsink()
   - m0_addb_mc_init()
   - m0_addb_mc_is_configured()
   - m0_addb_mc_is_initialized()

   ADDB machine components are configured in a fixed order: first the record
   sink, then the event manager.  An ADDB machine can also be configured by
   duplicating the configuration of another.
   See @ref ADDB-DLD-lspec-mc-confs "ADDB Machine Configurations" and
   @ref ADDB-DLD-lspec-mc-global "The Global ADDB Machine" for more details.

   Interfaces involved in managing contexts:
   - m0_addb_ctx_export()
   - M0_ADDB_CTX_INIT()
   - m0__addb_ctx_init()
   - m0_addb_ctx_fini()
   - m0_addb_ctx_import()

   Interfaces involved in posting ADDB records:
   - M0_ADDB_CTX_VEC()
   - M0_ADDB_POST()
   - M0_ADDB_POST_CNTR()
   - M0_ADDB_POST_SEQ()
   - m0__addb_post()

   Interfaces to manage ADDB counters:
   - m0_addb_counter_init()
   - m0_addb_counter_fini()
   - m0_addb_counter_update()
   - m0_addb_counter_reset()

   Interfaces for record retrieval:
   - m0_addb_ctx_type_lookup()
   - m0_addb_cursor_fini()
   - m0_addb_cursor_init()
   - m0_addb_cursor_next()
   - m0_addb_rec_is_ctx()
   - m0_addb_rec_is_event()
   - m0_addb_rec_rid_make()
   - m0_addb_rec_rid_to_brt()
   - m0_addb_rec_rid_to_id()
   - m0_addb_rec_type_lookup()
   - m0_addb_stob_iter_alloc()
   - m0_addb_stob_iter_free()

   <hr>
   @section ADDB-DLD-fspec-cli Command Usage
   @todo The usage for the dump retrieval CLI will be added as part of
   task addb.api.retrieval.

   <hr>
   @section ADDB-DLD-fspec-usecases Recipes
   - @ref ADDB-DLD-fspec-uc-TSMC
   - @ref ADDB-DLD-fspec-uc-PSMC
   - @ref ADDB-DLD-fspec-uc-AWKMC
   - @ref ADDB-DLD-fspec-uc-reg
   - @ref ADDB-DLD-fspec-uc-post
   - @ref ADDB-DLD-fspec-uc-cntr
   - @ref ADDB-DLD-fspec-uc-read

   @subsection ADDB-DLD-fspec-uc-TSMC Configure a Transient Store Machine
   An ADDB machine with a transient store configuration is set up as follows:
@code
     m0_addb_mc_init(&mc);
     rc = m0_addb_mc_configure_rpc_sink(&mc, rpc_machine);
     rc = m0_addb_mc_configure_pt_evmgr(&mc);
@endcode
   See @ref ADDB-DLD-lspec-mc-confs "ADDB Machine Configurations" for more
   details.

   @subsection ADDB-DLD-fspec-uc-PSMC Configure a Persistent Store Machine
   An ADDB machine with a persistent store configuration is set up as follows:
@code
     m0_addb_mc_init(&mc);
     rc = m0_addb_mc_configure_stob_sink(&mc, addb_stob, segment_size);
     rc = m0_addb_mc_configure_pt_evmgr(&mc);
@endcode
   See @ref ADDB-DLD-lspec-mc-confs "ADDB Machine Configurations" for more
   details.

   @subsection ADDB-DLD-fspec-uc-AWKMC Configure an Awkward Context Machine
   An ADDB machine with a support for awkward context posting is set up as
   as follows:
@code
     m0_addb_mc_init(&cache_mc);
     rc = m0_addb_mc_configure_cache_evmgr(&cache_mc, size);
@endcode
   The cached records must be flushed periodically to a transient or persistent
   store ADDB machine, as follows:
@code
     m0_addb_mc_cache_evmgr_flush(&cache_mc, &sink_mc);
@endcode
   See @ref ADDB-DLD-lspec-mc-confs "ADDB Machine Configurations" for more
   details.

   @subsection ADDB-DLD-fspec-uc-reg Record and context type registration
   Record and context declaration is done through the module's external
   header file:
@code
#ifndef __MERO_MOD_MOD_H__
#define __MERO_MOD_MOD_H__
#include "addb/addb.h"

M0_ADDB_RT_EX(m0_mod_ex, 12344, "rc");
M0_ADDB_RT_DP(m0_mod_dp, 12345, "num");
M0_ADDB_RT_CNTR(m0_mod_cntr_nh, 12346);  // no histogram
M0_ADDB_RT_CNTR(m0_mod_cntr_h, 12347, 10, 20, 30, 40); // 5 histogram buckets
                                  // (there is an implicit 0th histogram bucket)
M0_ADDB_CT(m0_mod_ct, 67890);
#endif
@endcode
   Actual definition is done typically in the module's main C file by first
   defining the required ADDB macro guards before including the module external
   header file.  Typically record and context types are registered at run time
   from the module's initialization logic, invoked via the
   @ref init "initialization" mechanism.
@code
#define M0_ADDB_CT_CREATE_DEFINITION
#define M0_ADDB_RT_CREATE_DEFINITION
#include "mod/mod.h"

int mod_init() {
  m0_addb_rec_type_register(&m0_mod_ex);
  m0_addb_rec_type_register(&m0_mod_dp);
  m0_addb_rec_type_register(&m0_mod_cntr_nh);
  m0_addb_rec_type_register(&m0_mod_cntr_h);
  m0_addb_ctx_type_register(&m0_mod_ct);
  ...
}
@endcode
   Note that registration will fail (by assertion) if the identifier numbers are
   not unique.
   There are no de-registration APIs so nothing to undo during module
   finalization.

   @subsection ADDB-DLD-fspec-uc-mod-ctx Module private context objects
   Most modules will want to create private context objects to
   represent their module.  These context objects are primarily intended
   for use in exception posts made with the global ADDB machine, but can
   also be used in any other posts within the module.

   Such context objects should be created as children of the
   ::m0_addb_proc_ctx object, during module initialization.
   The global ADDB machine, ::m0_addb_gmc, should be used; it is initialized but
   not configured at this time, but context object initialization in this manner
   is supported as a special case.
   See @ref ADDB-DLD-CTXOBJ-gi "Context Initialization with the Global Machine"
   for more details.

   This is illustrated in the following pseudo-code:
@code
static struct m0_addb_ctx mod_ctx; // private to module

int mod_init() {
  ...
  M0_ADDB_CTX_INIT(&m0_addb_gmc, &mod_ctx, &m0_mod_ct, &m0_addb_proc_ctx);
  ...
}
@endcode

   @subsection ADDB-DLD-fspec-uc-post Posting ADDB records
   ADDB records are posted with the M0_ADDB_POST() macro or one of its variants
   (see @ref ADDB-DLD-fspec-uc-cntr later).
   ADDB records are distinguished externally by their context vector and
   record type.

   A simple posting example is shown below:
@code
 M0_ADDB_POST(&m0_addb_gmc, &m0_mod_ex, M0_ADDB_CTX_VEC(&m0_addb_proc_ctx), rc);
@endcode
   The example uses the global ADDB machine and the pre-defined software
   container context.  This is typically how exception records are posted.

   A more complex example of creating a context and posting an ADDB record
   is shown below.
@code
mod_runtime_sub(const struct m0_addb_uint64_seq *impctx_data) {
  struct m0_addb_ctx ctx;
  struct m0_addb_ctx impctx;
  ...
  M0_ADDB_CTX_INIT(mc, &ctx, &m0_mod_ct, &m0_addb_node_ctx);
  m0_addb_ctx_import(&impctx, impctx_data);
  ...
  M0_ADDB_POST(mc, &m0_mod_dp, M0_ADDB_CTX_VEC(&ctx, &impctx, &mod_ctx), num);
  ...
  m0_addb_ctx_fini(&impctx);
  m0_addb_ctx_fini(&ctx);
}
@endcode
  This is typically how data point records are posted.
  It is expected that dynamically created contexts would usually not be created
  on the stack but in an ambient data structure representing the run time state,
  such as a FOM data structure.  In such a case the initialization need only
  be done when the data structure is created.
  The example also illustrates how to import context data from elsewhere
  and use it in the context vector, as well as the use of the module's
  private context object.
  @note the file which use the actual serializable impctx_data
  (of struct m0_addb_uint64_seq) embedded at some other serializable
  fop data structure should include addb/addb_wire_xc.h header file also
  (generated by gccxml2xcode). See example at ioservice/io_fops.h.

  The ADDB machine to use is usually defined by the execution context.  In
  servers, typically the @ref reqh "request handler" defines the machine for FOM
  usage; the global ::m0_addb_gmc machine is always available for use.

  As an alternative to the ::M0_ADDB_CTX_VEC macro one can simply create a null
  terminated array of pointers to contexts.  For example, the following
  illustrates an equivalent way of creating the context vector shown in the
  previous example:
@code
  ...
  struct m0_addb_ctx *cv[] = { &ctx, &impctx, &mod_ctx, NULL };
  ...
  M0_ADDB_POST(mc, &m0_mod_dp, cv, num);
  ...
@endcode
   This is more efficient if there are multiple posts in the same subroutine,
   especially in the kernel where stack space is limited and the compiler can
   raise errors on violation of the maximum stack frame size if there are too
   many posts in the same subroutine.

   @subsection ADDB-DLD-fspec-uc-cntr Counter usage
   Counters are used to record simple statistical data.  They must be
   initialized before use, and finalized when no longer needed.  They are
   updated with data samples as needed, and, on a periodic basis the counter
   value should be posted with the M0_ADDB_POST_CNTR() macro, whereupon its
   internal statistical values get reset.

   Counters are distinguished externally by their context vector and counter
   record type.
   Counter record types must be defined and registered before use, as
   illustrated in @ref ADDB-DLD-fspec-uc-reg.  The counter record type defines
   the number of histogram buckets to maintain for counters of that type, if
   any, and their boundary values.

   Counters are typically embedded in some ambient data structure or defined
   statically, and are not allocated on the stack.

   The following illustrates counter usage:
@code
   struct m0_addb_counter cntr;
   ...
   rc = m0_addb_counter_init(&cntr, &m0_mod_cntr_h);
   ...
   rc = m0_addb_counter_update(&cntr, sample1);
   ...
   rc = m0_addb_counter_update(&cntr, sample2);
   ...
   M0_ADDB_POST_CNTR(mc, cv, &cntr); // resets counter
   ...
   m0_addb_counter_fini(&cntr);
@endcode
   Posting should be done periodically at a fixed frequency if possible.
   The application is entirely responsible for the serialization of its
   counters.

   It should be noted that a counter is restricted to 64 bit arithmetic.
   Internal values such as the sum-of-squares can easily overflow over time
   if the counter is not reset by posting.  The application should carefully
   determine the magnitude of the sample values added to the counter: it may not
   make sense, for example, to count in units of bytes or nanoseconds where
   mega-bytes and milliseconds are more appropriate.

   @subsection ADDB-DLD-fspec-uc-read Record retrieval
   @todo Record retrieval examples will be provided by the addb.api.retrieval
   task.
 */

#include "lib/mutex.h"
#include "lib/refs.h"
#include "lib/uuid.h"

struct m0_bufvec_cursor;

/**
   @defgroup addb_pvt ADDB Internal Interfaces
   @ingroup addb

   @see @ref ADDB-DLD-fspec "ADDB Functional Specification"
   @see @ref ADDB-DLD "ADDB Detailed Design"
   @see @ref addb "Analysis and Data-Base API"
   @{
*/

/**
   Abstract event manager component of an ADDB machine.
 */
struct m0_addb_mc_evmgr {
	uint64_t evm_magic;
	/** Indicates if posting in awkward contexts is supported */
	bool     evm_post_awkward;
	/** Acquire the component */
	void   (*evm_get)(struct m0_addb_mc *mc, struct m0_addb_mc_evmgr *mgr);
	/** Release the component */
	void   (*evm_put)(struct m0_addb_mc *mc, struct m0_addb_mc_evmgr *mgr);
	/**
	   Record space allocator.
	   This method allocates sufficient contiguous space to accomodate an
	   ADDB record of a particular size. It is assumed that the memory will
	   be returned via a subsequent @a evm_post() method call in the same
	   execution environment, with no other intermediate method calls.
	   @param len Space required for the record and its contained sequences.
	   When constructing the record, the invoker should ensure that sequence
	   data pointers point within the returned memory chunk.
	*/
	struct m0_addb_rec *(*evm_rec_alloc)(struct m0_addb_mc *mc, size_t len);
	/**
	   Post an ADDB record if possible.
	   The memory pointed to by @a rec should have been allocated by this
	   component via the @a evm_rec_alloc() method immediately previous to
	   this call, and should not be referenced again by the invoker.
	*/
	void   (*evm_post)(struct m0_addb_mc *mc, struct m0_addb_rec *rec);
	/**
	   Copy cache to another machine's post interfaces.
	   The posting context must be supported by both machines.
	   This will be NULL in a passthrough event manager.
	 */
	int    (*evm_copy)(struct m0_addb_mc *mc, struct m0_addb_mc *dest_mc);
	/**
	   Record an ADDB exception in the system log.
	   This may be NULL, in which case no logging can occur.
	 */
	void   (*evm_log)(struct m0_addb_mc *mc, struct m0_addb_post_data *pd);
};

/**
 * This enum contains type of serialized addb records sequence.
 */
enum {
	ADDB_REC_SEQ_XC = 1,
	ADDB_RPC_SINK_FOP_XC
};

/**
   Abstract record sink component of an ADDB machine.
 */
struct m0_addb_mc_recsink {
	uint64_t rs_magic;
	/** Acquire the component */
	void   (*rs_get)(struct m0_addb_mc *mc, struct m0_addb_mc_recsink *snk);
	/** Release the component */
	void   (*rs_put)(struct m0_addb_mc *mc, struct m0_addb_mc_recsink *snk);
	/**
	   Record space allocator.
	   This method allocates sufficient contiguous space to accomodate an
	   ADDB record of a particular size. It is assumed that the memory will
	   be returned via a subsequent @a rs_save() method call in the same
	   execution environment, with no other intermediate method calls on the
	   sink.
	   @param len Space required for the record and its contained sequences.
	   When constructing the record, the invoker should ensure that sequence
	   data pointers point within the returned memory chunk.
	*/
	struct m0_addb_rec *(*rs_rec_alloc)(struct m0_addb_mc *mc, size_t len);
	/**
	   Subroutine to process an ADDB record.
	   The memory pointed to by @a rec should have been allocated by this
	   component via the @a rs_rec_alloc() method immediately previous to
	   this call, and should not be referenced again by the invoker.
	 */
	void   (*rs_save)(struct m0_addb_mc *mc, struct m0_addb_rec *rec);
	/**
	   Subroutine to process an xcoded ADDB record sequence.
	   @param cur A cursor to pointing to the start of an xcoded
	   m0_addb_rec_seq.  This will typically be somewhere within a m0_bufvec
	   encoding an entire xcoded m0_rpc_packet.
	   @param len Length of the xcoded m0_addb_rec_seq within the packet.
	 */
	void   (*rs_save_seq)(struct m0_addb_mc       *mc,
			      struct m0_bufvec_cursor *cur,
			      m0_bcount_t              len);
	/**
	   Subroutine to perform a unit of background activity in the sink.
	 */
	void   (*rs_skulk)(struct m0_addb_mc *mc);
};

/**
 * Transient store.
 */
struct m0_addb_ts {
	/** Transient store pre-allocated pages */
	struct m0_bufvec   at_pages;
	/** Bitmaps for all the TS pages */
	struct m0_bitmap **at_bitmaps;
	/** Current page index for record allocation */
	int32_t	           at_curr_pidx;
	/** Current word index for record allocation */
	int32_t	           at_curr_widx;
	/** Maximum size of transient store in terms of no of pages */
	int32_t	           at_max_pages;
	/** Page size in bytes */
	m0_bcount_t        at_page_size;
	/** List of transient record headers */
	struct m0_tl	   at_rec_queue;
};

/**
 * Transient store page.
 */
struct m0_addb_ts_page {
	/** Array of 64bit words */
	uint64_t	 atp_words[0];
};

/**
 * Transient store record header.
 */
struct m0_addb_ts_rec_header {
	uint64_t	    atrh_magic;
	/** page index of transient record */
	uint32_t	    atrh_pg_idx;
	/** word index of transient record */
	uint32_t	    atrh_widx;
	/** Record len (no. of 64bit words) */
	uint32_t	    atrh_nr;
	/** Linkage to transient record headers list */
	struct m0_tlink	    atrh_linkage;
};

/**
 * Transient store record.
 */
struct m0_addb_ts_rec {
	uint64_t		     atr_magic;
	/** transient record header */
	struct m0_addb_ts_rec_header atr_header;
	/** transient record data */
	uint64_t		     atr_data[0];
};

enum {
	WORD_SIZE = 8
};

/**
 * Interfaces to Transient Store
 */
#define ADDB_TS_PAGE(ts, pageindex) \
	(struct m0_addb_ts_page *)((ts)->at_pages.ov_buf[pageindex])

#define ADDB_TS_CUR_PAGES(ts) ((ts)->at_pages.ov_vec.v_nr)

#define ADDB_TS_GET_REC_SIZE(tsrh) (((tsrh)->atrh_nr * WORD_SIZE) - \
		sizeof(struct m0_addb_ts_rec))

static int		      addb_ts_init(struct m0_addb_ts *ts,
					   uint32_t npages_init,
					   uint32_t npages_max,
					   m0_bcount_t pgsize);
static void		      addb_ts_fini(struct m0_addb_ts *ts);
static int		      addb_ts_extend(struct m0_addb_ts *ts,
					     uint32_t nsegs);
static void		      addb_ts_free(struct m0_addb_ts *ts,
					   struct m0_addb_ts_rec *rec);
static struct m0_addb_ts_rec *addb_ts_alloc(struct m0_addb_ts *ts,
					    m0_bcount_t len);
static void addb_ts_save(struct m0_addb_ts *ts, struct m0_addb_ts_rec *rec);
static struct m0_addb_ts_rec *addb_ts_get(struct m0_addb_ts *ts,
					  m0_bcount_t reclen);

/**
   Passthrough event manager structure.
 */
struct addb_pt_evmgr {
	struct m0_addb_mc_evmgr ape_evmgr;
	struct m0_ref           ape_ref;
};

/**
   Caching event manager structure.
 */
struct addb_cache_evmgr {
	struct m0_addb_mc_evmgr ace_evmgr;
	struct m0_addb_ts       ace_ts;
	struct m0_ref           ace_ref;
};

static void   addb_ctx_init(void);
static void   addb_ctx_fini(void);
static void   addb_ctx_global_post(void);
static bool   addb_ctx_invariant(const struct m0_addb_ctx *ctx);
static void   addb_ctx_proc_ctx_create(void);
static void   addb_ctx_rec_post(struct m0_addb_mc *mc, struct m0_addb_ctx *ctx,
				uint64_t fields[]);
static bool   addb_ctx_type_invariant(const struct m0_addb_ctx_type *ct);
static size_t addb_ctxid_seq_build(struct m0_addb_ctxid_seq *seq,
				   struct m0_addb_ctx * const cv[]);
static size_t addb_ctxid_seq_data_size(struct m0_addb_ctx * const cv[]);
static bool   addb_mc_invariant(const struct m0_addb_mc *mc);
static int    addb_node_uuid_init(void);
static int    addb_node_uuid_string_get(char buf[M0_UUID_STRLEN + 1]);
static void   addb_rec_post(struct m0_addb_mc *mc,
			    uint64_t rid,
			    struct m0_addb_ctx **cv,
			    uint64_t *fields,
			    size_t fields_nr);
static bool   addb_rec_type_invariant(const struct m0_addb_rec_type *rt);

/** @} end of addb_pvt group */

#endif /* __MERO_ADDB_ADDB_PVT_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
