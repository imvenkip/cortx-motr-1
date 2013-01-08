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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 *                  Dave Cohrs <dave_cohrs@xyratex.com>
 * Original creation: 08/27/2012
 */

/**
   @page ADDB-DLD-CTXOBJ Context Object Management

   This design relates to context object management and initialization of the
   global context objects.
   This is a sub-component design document and hence not all sections are
   present.  Refer to @ref ADDB-DLD "ADDB Detailed Design" for the design
   requirements.
   - @ref ADDB-DLD-CTXOBJ-fspec
   - @ref ADDB-DLD-CTXOBJ-lspec
     - @ref ADDB-DLD-CTXOBJ-init
     - @ref ADDB-DLD-CTXOBJ-fini
     - @ref ADDB-DLD-CTXOBJ-node
     - @ref ADDB-DLD-CTXOBJ-proc
     - @ref ADDB-DLD-CTXOBJ-global-def
     - @ref ADDB-DLD-CTXOBJ-gi

   <hr>
   @section ADDB-DLD-CTXOBJ-fspec Functional Specification
   The primary data structures involved are:
   - m0_addb_ctx
   - m0_addb_ctx_type
   - m0_addb_uint64_seq

   The following external interfaces must be implemented:
   - M0_ADDB_CTX_INIT()
   - m0__addb_ctx_init()
   - m0_addb_ctx_export()
   - m0_addb_ctx_fini()
   - m0_addb_ctx_import()

   <hr>
   @section ADDB-DLD-CTXOBJ-lspec Logical Specification

   @subsection ADDB-DLD-CTXOBJ-init Context Objects Initialization
   A context object (struct ::m0_addb_ctx) represents a context in memory. It is
   allocated by the higher level application, typically embedded in some related
   outer data structure, and must be initialized before use.  An important
   design point is that no additional memory is to be allocated within a context
   object: doing so would dilute the value of embedding the object.

   A context object is initialized in one of two ways:
   - With the M0_ADDB_CTX_INIT() macro to define a new context.
   - With the m0_addb_ctx_import() subroutine to reference an existing context.

   A context object, once initialized, can be used to post records in any ADDB
   machine.

   The M0_ADDB_CTX_INIT() macro is used to define a new context.  The macro
   performs compile time type-checking and argument count validation, and then
   posts a context definition record.  Implicit or explicit serialization may be
   required, as explained below.

   It is required that all new contexts be named relative to an existing context
   (specified by the @a parent argument).  During context creation the context
   object being initialized is automatically assigned a relative identification
   number in its @a ac_id field from the @a ac_cntr counter field of its parent
   context object, post-incrementing the parent counter while doing so.  The
   (absolute) identifier for the context associated with a given context object
   is defined as the sequence of relative identifiers of the context objects on
   the path from the root of the context object hierarchy to the concerned
   context object, and is obtained by navigating the @a ac_parent linkage upward
   from the context object; this is a frequently performed operation, so the
   depth of the path from the root is saved in each context object's @a ac_depth
   field to avoid two traversals of the object hierarchy, the first for path
   length computation and the second to assemble the path.

   The ADDB subsystem provides two pre-defined context objects that can serve as
   parents: @ref ADDB-DLD-CTXOBJ-node and @ref ADDB-DLD-CTXOBJ-proc.  The former
   has its counter primed with the Mero initialization time stamp to provide
   temporally unique identifiers for its children.  The latter is created as a
   child of the former, and provides a temporally unique context representing
   the execution environment; it is recommended to use it as the context for
   exception events.
   The ADDB subsystem will serialize the use of the counters in these
   pre-defined context objects with the internal ::addb_mutex.
   Context objects created with the M0_ADDB_CTX_INIT() macro can also be used as
   parents of new contexts, however their use in this manner must be serialized
   externally by the invoker.

   The macro invokes the private m0__addb_ctx_init() subroutine to construct the
   context identifier for a context, as described above, and then record the
   existence of the new context by posting a ::M0_ADDB_BRT_CTXDEF context
   definition record with the ADDB machine's record sink interface.

   The following pseudo-code illustrates the construction of a
   ::M0_ADDB_BRT_CTXDEF record:
@code
    r.ar_rid  = m0_addb_rec_rid_make(M0_ADDB_BRT_CTXDEF, ctx->ac_type->act_id);
    r.ar_ts   = m0_time_now();
    r.ar_ctxids.acis_nr   = 1;
    r.ar_ctxids.acis_data = context_id; // a sequence type
    r.ar_data = fields;
@endcode
   The record is then posted to the ADDB machine's record sink.  The context
   fields are not required after creation, and are not saved in the context
   object.

   The m0_addb_ctx_export() subroutine is used to obtain the context identifier
   from a context object in order to convey it to another, most likely remote,
   process.  The struct ::m0_addb_uint64_seq data type used to represent the
   context identifier is made network serialize-able for this purpose.

   A context object can also be initialized from a context identifier with the
   m0_addb_ctx_import() subroutine.  This does not create a new context nor does
   it post a context definition record.  A context object created in this manner
   can be used in ADDB event posting; however, it cannot be used to create new
   contexts, as it is incapable of generating globally unique child context
   identifiers in the remote processes into which it gets imported.

   An important point to note for a context object initialized by import, is
   that it has no parent object and no relative identifier.  Instead, it
   contains a pointer to the imported context identifier maintained in invoker
   managed memory that is assumed to be immutable across the lifetime of the
   object.  This is not as onerous as it seems: In most cases the imported
   context identifier would be in the @ref fop "fop" conveying it to a
   service, and the context object would be in the same outer data structure
   that embeds the associated @ref fom "FOM"; the context object's life span
   would not exceed that of the associated fop.

   @subsection ADDB-DLD-CTXOBJ-fini Context Object Finalization
   Finalization of a context object in memory clears memory pointers and
   invalidates the magic number.  Note that there is no finalization of a
   context in the ADDB repository; its data will eventually be overwritten and
   lost.

   @subsection ADDB-DLD-CTXOBJ-node The Node Context
   During module initialization the ADDB subsystem creates a context to
   represent the node.  Each participating Mero node, be it a client or
   a server, is assigned a UUID during software configuration.  The UUID is
   passed to the Mero kernel module as a parameter.  User space processes
   read @c /sys/module/m0mero/parameters/node_uuid to fetch the UUID value
   passed to the kernel.  The UUID is expressed as a string in standard
   8-4-4-4-12 hexadecimal digit format and has to be converted to internal
   numeric form.
   See <a href="http://en.wikipedia.org/wiki/Universally_unique_identifier">
   Universally unique identifier</a> for more details.

   ADDB context objects are constrained to use 64 bit relative identifiers, but
   a UUID is a 128 bit value.  So the ADDB subsystem creates a "root" context
   object with the upper half of the UUID, and a child context object with the
   lower half of the UUID.  The child context object is exposed as the
   ::m0_addb_node_ctx, and can serve as the parent of application created
   contexts.  The "root" context object is private, though it can be accessed
   via the @a ac_parent pointer of the ::m0_addb_node_ctx object.

   The context type of the private "root" context is @ref m0_addb_ct_node_hi and
   its context type identifier is ::M0_ADDB_CTXID_NODE_HI.  The context type of
   the ::m0_addb_node_ctx is @ref m0_addb_ct_node_lo and its context type
   identifier is ::M0_ADDB_CTXID_NODE_LO.

   The ::m0_addb_node_ctx object's leaf identifier counter is primed with the
   value of its creation time stamp to generate unique numbering for child
   contexts over time.  There are no context data fields associated with this
   context.

   @subsection ADDB-DLD-CTXOBJ-proc The Container Context
   During module initialization the ADDB subsystem creates a context to
   represent the run time execution environment, be it the kernel module or a
   user space process.  The context is represented by the ::m0_addb_proc_ctx
   object, and is created as a child of the ::m0_addb_node_ctx, with a relative
   identifier generated from the parent context counter.

   The ::m0_addb_proc_ctx object uses the @ref m0_addb_ct_kmod context type in
   the kernel, with context type identifier ::M0_ADDB_CTXID_KMOD and one
   data field, @a ts, the creation time stamp.
   The context object uses the @ref m0_addb_ct_process context type in user
   space, with context type identifier ::M0_ADDB_CTXID_PROCESS and two data
   fields, @a ts, the creation time stamp, and @a procid, the process
   identifier.

   @subsection ADDB-DLD-CTXOBJ-global-def Global Context Definition Records
   While the ADDB subsystem defined global context objects are created during
   module initialization the contexts they represent are not recorded in
   persistent storage at this time, because no ADDB machine is yet available
   through which to post their context definition records.

   Instead, when the @ref ADDB-DLD-lspec-mc-global "Global ADDB Machine" first
   gets configured by the application, the configuration subroutine involved
   will explicitly invoke the addb_ctx_global_post() subroutine to post the
   context definition records.  The possible configuration subroutines involved
   are:
   - m0_addb_mc_configure_rpc_sink()
   - m0_addb_mc_configure_stob_sink()
   - m0_addb_mc_dup()

   The addb_ctx_global_post() subroutine will also complete the processing of
   other context objects, as described in @ref ADDB-DLD-CTXOBJ-gi below.

   ADDB records saved in the repository will eventually be overwritten in the
   course of time, including those of node and container context definitions.
   ADDB servers are expected to be very long lived, and their clients only
   slightly less so.  References to long lived contexts will continue to be made
   but these references will not be resolvable by an external entity when their
   context definition records are lost.  It is necessary that an external agency
   be used to save the long lived contexts required for later analysis.  In
   addition to the node and process contexts, such contexts include those
   created by each request handler, as well as those of long lived foms.

   @subsection ADDB-DLD-CTXOBJ-gi Context Initialization with the Global Machine
   Although the global ADDB machine is not configured on start-up, it may be
   used to initialize context objects that are children of the global context
   objects.  This is typically done in module initialization logic.

   The context definition records of such contexts cannot be posted until the
   global ADDB machine gets configured, although the context objects themselves
   can be properly initialized internally.  Instead, an "extrinsic" list with
   such context objects and their creation time arguments will be maintained
   until such time that the global ADDB machine gets configured and the
   addb_ctx_global_post() subroutine gets invoked.  After the global context
   object definitions are posted, the context object definitions of the objects
   tracked by this list will be posted, and the list cleared.  The list will be
   serialized with the internal ::addb_mutex.
 */


#include "addb/addb_pvt.h"

static struct m0_addb_ctx addb_node_root_ctx; /* node context (hi uuid) */
struct m0_addb_ctx m0_addb_node_ctx; /* global node context (low uuid) */
M0_EXPORTED(m0_addb_node_ctx);

static uint64_t addb_proc_ctx_fields[2]; /* container context fields */
struct m0_addb_ctx m0_addb_proc_ctx; /* global container context */
M0_EXPORTED(m0_addb_proc_ctx);

/**
   @addtogroup addb_pvt
   @{
 */

/**
   Context object invariant for application created contexts.
 */
static bool addb_ctx_invariant(const struct m0_addb_ctx *ctx)
{
	if (ctx->ac_magic != M0_ADDB_CTX_MAGIC || ctx->ac_depth == 0)
		return false;
	if (ctx->ac_type != NULL) { /* case A */
		if (!addb_ctx_type_invariant(ctx->ac_type) ||
		    ctx->ac_imp_id != NULL)
			return false;
		if (ctx->ac_depth > 1) { /* not root */
			if (ctx->ac_parent == NULL ||
			    ctx->ac_depth != ctx->ac_parent->ac_depth + 1)
				return false;
		} else /* root */
			if (ctx->ac_parent != NULL)
				return false;
	} else { /* case B */
		if (ctx->ac_imp_id == NULL || ctx->ac_parent != NULL)
			return false;
	}
	return true;
}

/**
   Unposted ADDB context objects records are tracked with this data structure.
 */
struct addb_ctx_def_cache {
	uint64_t            cdc_magic;
	struct m0_tlink     cdc_linkage;
	struct m0_addb_ctx *cdc_ctx;
	uint64_t            cdc_fields[]; /**< init fields */
};

M0_TL_DESCR_DEFINE(addb_cdc, "cdc list", static,
		   struct addb_ctx_def_cache, cdc_linkage, cdc_magic,
		   M0_ADDB_CDC_MAGIC, M0_ADDB_CDC_HEAD_MAGIC);
M0_TL_DEFINE(addb_cdc, static, struct addb_ctx_def_cache);

/** The cache of unposted ADDB context definition records */
static struct m0_tl addb_cdc;

/**
   Cache a context object and its init fields.

   This is done on a best effort basis.
 */
static void addb_cdc_cache(struct m0_addb_ctx *ctx, uint64_t fields[])
{
	struct addb_ctx_def_cache *ce;
	size_t fields_nr;
	size_t l;

	M0_PRE(addb_ctx_invariant(ctx));
	M0_PRE(ergo(ctx->ac_type->act_cf_nr > 0, fields != NULL));

	fields_nr = ctx->ac_type->act_cf_nr;
	l = sizeof *ce + fields_nr * sizeof(fields[0]);
	ce = m0_alloc(l);
	if (ce == NULL) {
		M0_LOG(M0_ERROR, "Unable to cache ctx def record len=%d",
		       (int)l);
		return;
	}
	addb_cdc_tlink_init(ce);
	ce->cdc_ctx = ctx;
	for (l = 0; l < fields_nr; ++l)
		ce->cdc_fields[l] = fields[l];
	addb_cdc_tlist_add_tail(&addb_cdc, ce);
}

/**
   Sub-module initialization.
   It initializes the global node and global process context objects
   in a user/kernel space specific manner.
 */
static void addb_ctx_init(void)
{
	addb_node_root_ctx.ac_type  = &m0_addb_ct_node_hi;
	addb_node_root_ctx.ac_id    = m0_node_uuid.u_hi;
	addb_node_root_ctx.ac_depth = 1;
	addb_node_root_ctx.ac_magic = M0_ADDB_CTX_MAGIC;
	M0_ASSERT(addb_ctx_invariant(&addb_node_root_ctx));

	m0_addb_node_ctx.ac_type    = &m0_addb_ct_node_lo;
	m0_addb_node_ctx.ac_id      = m0_node_uuid.u_lo;
	m0_addb_node_ctx.ac_parent  = &addb_node_root_ctx;
	++addb_node_root_ctx.ac_cntr;
	m0_addb_node_ctx.ac_depth   = addb_node_root_ctx.ac_depth + 1;
	m0_addb_node_ctx.ac_magic   = M0_ADDB_CTX_MAGIC;
	M0_ASSERT(addb_ctx_invariant(&m0_addb_node_ctx));

	addb_ctx_proc_ctx_create();
	M0_ASSERT(addb_ctx_invariant(&m0_addb_proc_ctx));

	/* init the context definition cache */
	addb_cdc_tlist_init(&addb_cdc);
}

/**
   Sub-module finalization.
   It releases resources associated with the global context objects and
   the context definition cache.
 */
static void addb_ctx_fini(void)
{
	struct addb_ctx_def_cache *ce;

	/* free the cache */
	m0_tl_for(addb_cdc, &addb_cdc, ce) {
		addb_cdc_tlist_del(ce);
		m0_free(ce);
	} m0_tl_endfor;
	addb_cdc_tlist_fini(&addb_cdc);
}

/**
   Post definition records for the global context objects through the
   global ADDB machine.
 */
static void addb_ctx_global_post(void)
{
	struct addb_ctx_def_cache *ce;

	M0_ASSERT(m0_addb_mc_is_fully_configured(&m0_addb_gmc));

	addb_ctx_rec_post(&m0_addb_gmc, &addb_node_root_ctx, NULL);
	addb_ctx_rec_post(&m0_addb_gmc, &m0_addb_node_ctx, NULL);
	addb_ctx_rec_post(&m0_addb_gmc, &m0_addb_proc_ctx,
			  addb_proc_ctx_fields);
	m0_mutex_lock(&addb_mutex);
	m0_tl_for(addb_cdc, &addb_cdc, ce) {
		addb_cdc_tlist_del(ce);
		addb_ctx_rec_post(&m0_addb_gmc, ce->cdc_ctx, ce->cdc_fields);
		m0_free(ce);
	} m0_tl_endfor;
	m0_mutex_unlock(&addb_mutex);
}

/**
   Compute the data size of a context id sequence.
   @param cv Context vector (a NULL terminated array)
 */
static size_t addb_ctxid_seq_data_size(struct m0_addb_ctx * const cv[])
{
	size_t len = 0;

	for ( ; *cv != NULL; ++cv)
		len += sizeof(struct m0_addb_uint64_seq) +
			sizeof(uint64_t) * (*cv)->ac_depth;
	return len;
}

/**
   Construct a context id sequence from a context vector.
   @param seq Pointer to the sequence.  The data field should point
   to sufficient data for the records. It is assumed that
   this has at least addb_ctxid_seq_data_size(cv) bytes available.
   @param cv Context vector.
   @retval size of the data portion of the sequence, in bytes.
 */
static size_t addb_ctxid_seq_build(struct m0_addb_ctxid_seq *seq,
				   struct m0_addb_ctx * const cv[])
{
	struct m0_addb_uint64_seq *u64s;
	const struct m0_addb_ctx  *cp;
	uint64_t                  *dp;
	int                        i;

	M0_PRE(seq != NULL && seq->acis_data != NULL);

	seq->acis_nr = 0;
	u64s = seq->acis_data;
	for (i = 0; cv[i] != NULL; ++i)
		; /* count the vector depth */
	dp = (uint64_t *)(u64s + i); /* start of the data fields */
	for ( ; *cv != NULL; ++cv, ++u64s) {
		++seq->acis_nr;
		u64s->au64s_nr = (*cv)->ac_depth;
		u64s->au64s_data = dp;
		for (i = (*cv)->ac_depth - 1, cp = *cv;
		     i >= 0; --i, cp = cp->ac_parent) {
			M0_ASSERT(cp != NULL);
			dp[i] = cp->ac_id;
		}
		M0_ASSERT(cp == NULL);
		dp += u64s->au64s_nr;
	}
	return (char *)dp - (char *)seq->acis_data;
}

/**
   Post a context definition record.  The record is assembled in
   contiguous memory allocated by the record sink, as follows:
   @verbatim
    +-----------------------+
    | m0_addb_rec           |
    |   ar_data.au64s_nr=#f |
    |   ar_data.au64s_data -+------\
    |   ar_ctxids.acis_nr=1 |      |
    |   ar_ctxids.acis_data +----\ |
    |   ...                 |    | |
    +-----------------------+    | |
    | m0_addb_uint64_seq    |<---/ |
    |   au64s_nr=depth      |      |
    |   au64s_data ---------+--\   |
    +-----------------------+  |   |
    | root ac_id            |<-/   |
    |   ...                 |      |
    | ctx->ac_id            |      |
    +-----------------------+      |
    | f0                    |<-----/
    | f1                    |
    | ...                   |
    +-----------------------+
@endverbatim

   If the record is being posted to the global ADDB machine, and the latter
   is not yet configured, then the subroutine caches the context definition
   record until such time as the global ADDB machine gets configured.

   @param mc Machine to use for posting
   @param ctx The context object
   @param fields Context data field array; length determined from context type.
   @pre addb_ctx_invariant(ctx)
   @pre m0_addb_mc_has_recsink(mc)
 */
static void addb_ctx_rec_post(struct m0_addb_mc *mc,
			      struct m0_addb_ctx *ctx,
			      uint64_t fields[])
{
	M0_PRE(addb_ctx_invariant(ctx));
	M0_PRE(ergo(ctx->ac_type->act_cf_nr > 0, fields != NULL));

	addb_rec_post(mc, m0_addb_rec_rid_make(M0_ADDB_BRT_CTXDEF,
					       ctx->ac_type->act_id),
		      M0_ADDB_CTX_VEC(ctx), fields, ctx->ac_type->act_cf_nr);
}

/** @} addb_pvt */

/*
 ******************************************************************************
 * Public interfaces
 ******************************************************************************
 */

M0_INTERNAL void m0__addb_ctx_init(struct m0_addb_mc *mc,
				   struct m0_addb_ctx *ctx,
				   const struct m0_addb_ctx_type *ct,
				   struct m0_addb_ctx *parent,
				   uint64_t fields[])
{
	M0_PRE(!m0_addb_ctx_is_imported(parent));
	M0_PRE(addb_ctx_type_invariant(ct));

	/* construct the context object */
	ctx->ac_type = ct;
	if (parent == &m0_addb_node_ctx || parent == &m0_addb_proc_ctx ||
	    parent == &addb_node_root_ctx) {
		m0_mutex_lock(&addb_mutex);
		ctx->ac_id = ++parent->ac_cntr;
		m0_mutex_unlock(&addb_mutex);
	} else
		ctx->ac_id = ++parent->ac_cntr; /* externally serialized */
	ctx->ac_parent = parent;
	ctx->ac_imp_id = NULL;
	ctx->ac_cntr = 0;
	ctx->ac_depth = parent->ac_depth + 1;
	ctx->ac_magic = M0_ADDB_CTX_MAGIC;
	M0_POST(!m0_addb_ctx_is_imported(ctx));

	if (mc == &m0_addb_gmc && !m0_addb_mc_is_fully_configured(mc)) {
		bool post_deferred = true;

		m0_mutex_lock(&addb_mutex);
		if (m0_addb_mc_is_fully_configured(mc))
			post_deferred = false;
		else
			addb_cdc_cache(ctx, fields);
		m0_mutex_unlock(&addb_mutex);
		if (post_deferred)
			return;
	}

	/* post the context definition record */
	addb_ctx_rec_post(mc, ctx, fields);
}

M0_INTERNAL int m0_addb_ctx_export(struct m0_addb_ctx *ctx,
				   struct m0_addb_uint64_seq *id)
{
	int i;

	M0_PRE(id->au64s_nr == 0 && id->au64s_data == NULL);
	M0_PRE(addb_ctx_invariant(ctx));

	M0_ALLOC_ARR(id->au64s_data, ctx->ac_depth);
	if (id->au64s_data == NULL) {
		M0_LOG(M0_ERROR, "m0_addb_ctx_export: unable to allocate %d",
		       (int)(ctx->ac_depth * sizeof(uint64_t)));
		return -ENOMEM;
	}
	id->au64s_nr = ctx->ac_depth;

	if (ctx->ac_imp_id == NULL) { /* navigate up the hierarchy */
		struct m0_addb_ctx *cp;
		for (i = ctx->ac_depth - 1, cp = ctx; i >= 0;
		     --i, cp = cp->ac_parent)
			id->au64s_data[i] = cp->ac_id;
		M0_ASSERT(cp == NULL);
	} else { /* exporting an imported value */
		for (i = 0; i < ctx->ac_depth; ++i)
			id->au64s_data[i] = ctx->ac_imp_id[i];
	}
	M0_POST(id->au64s_nr != 0 && id->au64s_data != NULL);
	return 0;
}

M0_INTERNAL void m0_addb_ctx_id_free(struct m0_addb_uint64_seq *id)
{
	M0_PRE(id->au64s_nr != 0 && id->au64s_data != NULL);
	m0_free(id->au64s_data);
	id->au64s_nr = 0;
	id->au64s_data = NULL;
	M0_POST(id->au64s_nr == 0 && id->au64s_data == NULL);
}

M0_INTERNAL int m0_addb_ctx_import(struct m0_addb_ctx *ctx,
				   const struct m0_addb_uint64_seq *id)
{
	M0_PRE(id->au64s_nr != 0 && id->au64s_data != NULL);

	ctx->ac_type = NULL;
	ctx->ac_id = 0;
	ctx->ac_parent = NULL;
	ctx->ac_depth = id->au64s_nr;
	ctx->ac_imp_id = id->au64s_data; /* pins the memory */
	ctx->ac_cntr = 0;
	ctx->ac_magic = M0_ADDB_CTX_MAGIC;
	M0_POST(m0_addb_ctx_is_imported(ctx));

	return 0;
}

M0_INTERNAL bool m0_addb_ctx_is_imported(const struct m0_addb_ctx *ctx)
{
	M0_PRE(addb_ctx_invariant(ctx));
	return ctx->ac_imp_id != NULL;
}

M0_INTERNAL void m0_addb_ctx_fini(struct m0_addb_ctx *ctx)
{
	/* unlink context if we have a cached reference */
	if (!m0_addb_mc_is_fully_configured(&m0_addb_gmc)) {
		struct addb_ctx_def_cache *ce;

		m0_mutex_lock(&addb_mutex);
		m0_tl_for(addb_cdc, &addb_cdc, ce) {
			if (ce->cdc_ctx != ctx)
				continue;
			addb_cdc_tlist_del(ce);
			m0_free(ce);
		} m0_tl_endfor;
		m0_mutex_unlock(&addb_mutex);
	}
	ctx->ac_magic  = 0;
	ctx->ac_type   = NULL;
	ctx->ac_parent = NULL;
	ctx->ac_imp_id = NULL;
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
