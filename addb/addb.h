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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 *                  Huang Hua <hua_huang@xyratex.com>
 * Original creation date: 06/19/2010
 * Rewritten by:    Carl Braganza <carl_braganza@xyratex.com>
 *                  Dave Cohrs <dave_cohrs@xyratex.com>
 * Rewrite date: 08/14/2012
 */

#pragma once

#ifndef __MERO_ADDB_ADDB_H__
#define __MERO_ADDB_ADDB_H__

#include "lib/types.h"
#include "lib/time.h"
#include "lib/tlist.h"
#include "lib/vec.h" /* m0_bufvec_cursor */
/** @todo REMOVE ME, once all fo_addb_fini() are impl. */
#include "mero/magic.h"

struct m0_addb_counter;
struct m0_addb_ctx;
struct m0_addb_ctx_type;
struct m0_addb_cursor;
struct m0_addb_mc;
struct m0_addb_mc_evmgr;
struct m0_addb_mc_recsink;
struct m0_addb_rec;
struct m0_addb_rec_seq;
struct m0_addb_rec_header;
struct m0_addb_uint64_seq;

struct m0_rpc_machine;
struct m0_stob;

#include "addb/addb_wire.h"
#include "addb/addb_macros.h"

/**
   @defgroup addb Analysis and Diagnostics Data-Base API

   ADDB records are posted by Mero software to record the occurrence of
   specific situations.  The records are conveyed by the ADDB subsystem to an
   appropriate server node and then saved persistently in repositories on disk.
   Specialized tools can be written to harvest these repositories from the
   various server nodes, and then read the contained records and perform
   post-mortem analysis.

   @see @ref ADDB-DLD-fspec "ADDB Functional Specification" for a categorization
   of the available interfaces and examples of use.
   @see @ref ADDB-DLD "ADDB Detailed Design"
   @see @ref addb_pvt "ADDB Internal Interfaces"

   @{
*/

/**
   ADDB module initializer.
 */
M0_INTERNAL int m0_addb_init(void);

/**
   ADDB module finalizer.
 */
M0_INTERNAL void m0_addb_fini(void);

/** @} end of addb group */

/**
   @defgroup addb_meta_data ADDB Meta-Data
   @ingroup addb
   @{
 */

/*
 ****************************************************************************
 * Record type
 ****************************************************************************
 */

/**
   Enumerator describing the ADDB base record types.
 */
enum m0_addb_base_rec_type {
	M0_ADDB_BRT_EX = 1,   /**< Exception record */
	M0_ADDB_BRT_DP,       /**< Data point record */
	M0_ADDB_BRT_CNTR,     /**< Counter record */
	M0_ADDB_BRT_SEQ,      /**< Sequence record */

	/* internal base record types */
	M0_ADDB_BRT_CTXDEF,   /**< Context definition record (internal) */

	M0_ADDB_BRT_NR
};

/**
   Record field descriptor.  This could either name a field in exception and
   data point records, or, in counter records only, describe the lower bound of
   a histogram bucket.
 */
union m0_addb_rf_u {
	const char *arfu_name;  /**< Field name (EX, DP) */
	uint64_t    arfu_lower; /**< Bucket inclusive lower bound (CNTR) */
};

/**
   An ADDB record type descriptor.
   Instances of this descriptor type must be registered before they can be used.
 */
struct m0_addb_rec_type {
	/* Private */
	uint64_t                   art_magic;
	struct m0_tlink            art_linkage;

	/** External base record type */
	enum m0_addb_base_rec_type art_base_type;
	/** Record type name */
	const char                *art_name;
	/**
	   Unique identifier assigned to the record type.
	   The uniqueness will be asserted during registration.
	   The ADDB subsystem reserves a subset of the record type
	   identifier range for internal use.
	 */
	uint32_t                   art_id;
	/**
	   The interpretation of this element depends on the base
	   record type:
	   - In exception and data point records, it specifies the
	   number of fields in the @a art_rf array.
	   - In counter records a value of 0 implies that no histogram
	   should be computed.  A non-zero value is the number of
	   bucket limit values specified in the @a art_rf array.
	   Note that this is one less than the number of buckets in the
	   histogram, as there is an implicit lower bound of 0 for the
	   first bucket. See M0_ADDB_RT_CNTR() for details.
	   - The field is ignored for other record types.
	 */
	size_t                     art_rf_nr;
	/**
	   This contains an in-line array of field descriptors.
	 */
	const union m0_addb_rf_u   art_rf[];
};

/* Record type macros expand differently based upon the presence of
 * the predicate M0_ADDB_RT_DEFINITION macro.
 */
#ifndef M0_ADDB_RT_CREATE_DEFINITION

/**
   This macro expands in two different ways based on the existence of the
   ::M0_ADDB_RT_CREATE_DEFINITION predicate macro.
   If the predicate macro is defined then an ADDB exception record type C
   structure gets defined.
   If the predicate macro is not defined, then an ADDB exception record type C
   structure gets declared as an extern.
   @param name Specify the abbreviated record type name.
               This is a globally visible identifier.
   @param id   Specify the unique record type identity.
   @param ...  Specify C strings with the names of the fields.
	       There is an implementation limit of at most 9 fields.
   @pre id > 0
   @post name.art_magic == 0
 */
#define M0_ADDB_RT_EX(name, id, ...) \
	extern struct m0_addb_rec_type name

/**
   This macro expands in two different ways based on the existence of the
   ::M0_ADDB_RT_CREATE_DEFINITION predicate macro.
   If the predicate macro is defined then an ADDB data point record type C
   structure gets defined.
   If the predicate macro is not defined, then an ADDB data point record type
   C structure gets declared as an extern.
   @param name Specify the abbreviated record type name.
               This is a globally visible identifier.
   @param id   Specify the unique record type identity.
   @param ...  Specify C strings with the names of the fields.
	       There is an implementation limit of at most 9 fields.
   @pre id > 0
   @post name.art_magic == 0
 */
#define M0_ADDB_RT_DP(name, id, ...) \
	extern struct m0_addb_rec_type name

/**
   This macro expands in two different ways based on the existence of the
   ::M0_ADDB_RT_CREATE_DEFINITION predicate macro.
   If the predicate macro is defined then an ADDB counter record type C
   structure gets defined.
   If the predicate macro is not defined, then an ADDB counter record type
   C structure gets declared as an extern.
   @param name   Specify the abbreviated record type name.
                 This is a globally visible identifier.
   @param id     Specify the unique record type identity.
   @param ...    Specify the inclusive lower bound of the second histogram
                 bucket, followed by the third bucket, and so on.  The first
		 (0th) bucket has an implicit lower bound of 0.  There is an
                 implementation limit of at most 9 such bounds, for a maximum of
                 10 buckets in all.  Not specifying any bounds disables use of
                 the histogram feature.
   @pre id > 0
   @post name.art_magic == 0
 */
#define M0_ADDB_RT_CNTR(name, id, ...) \
	extern struct m0_addb_rec_type name

/**
   This macro expands in two different ways based on the existence of the
   ::M0_ADDB_RT_CREATE_DEFINITION predicate macro.
   If the predicate macro is defined then an ADDB sequence record type C
   structure gets defined.
   If the predicate macro is not defined, then an ADDB sequence record type
   C structure gets declared as an extern.
   @param name Specify the abbreviated record type name.
               This is a globally visible identifier.
   @param id   Specify the unique record type identity.
   @pre id > 0
   @post name.art_magic == 0
 */
#define M0_ADDB_RT_SEQ(name, id) \
	extern struct m0_addb_rec_type name

#else /* M0_ADDB_RT_CREATE_DEFINITION */

#define M0_ADDB_RT_EX(name, id, ...)				\
M0_CAT(M0__ADDB_RT_N, M0_COUNT_PARAMS(id, ## __VA_ARGS__))	\
 (name, EX, id, ## __VA_ARGS__)

#define M0_ADDB_RT_DP(name, id, ...)				\
M0_CAT(M0__ADDB_RT_N, M0_COUNT_PARAMS(id, ## __VA_ARGS__))	\
 (name, DP, id, ## __VA_ARGS__)

#define M0_ADDB_RT_CNTR(name, id, ...)				\
M0_CAT(M0__ADDB_RT_L, M0_COUNT_PARAMS(id, ## __VA_ARGS__))	\
 (name, id, ## __VA_ARGS__)

#define M0_ADDB_RT_SEQ(name, id)		\
M0_BASSERT(M0_HAS_TYPE(id, int) && (id) > 0);	\
struct m0_addb_rec_type name = {		\
	.art_base_type = M0_ADDB_BRT_SEQ,	\
	.art_name      = #name,			\
	.art_id        = (id),			\
	.art_rf_nr     = 0,			\
        .art_rf        = { }			\
}

#endif /* M0_ADDB_RT_CREATE_DEFINITION */

/**
   Register a record type.  The subroutine will assert that the record
   type identifier is unique among the record types registered so far,
   and that the data in the record type is valid.
   @param rt Pointer to the record type structure.
   @pre m0_addb_rec_type_lookup(rt->art_id) == NULL
   @post m0_addb_rec_type_lookup(rt->art_id) == rt
   @see M0_ADDB_RT_EX(), M0_ADDB_RT_DP(), M0_ADDB_RT_CNTR(), M0_ADDB_RT_SEQ()
 */
M0_INTERNAL void m0_addb_rec_type_register(struct m0_addb_rec_type *rt);

/**
   Look up a record type by record type identifier.
   @param id Specify the record type identifier.
   @return NULL Not found
   @return Pointer if found
   @see m0_addb_cursor_next()
 */
M0_INTERNAL const struct m0_addb_rec_type *m0_addb_rec_type_lookup(uint32_t id);

enum {
	/** Record type identifier for context records */
	M0_ADDB_RECID_CTXDEF    = 1,
	/** Function failure record type identifier */
	M0_ADDB_RECID_FUNC_FAIL = 2,
	/** Out-of-memory record type identifier */
	M0_ADDB_RECID_OOM       = 3,

	M0_ADDB_RECID_RESV_FIRST,
	M0_ADDB_RECID_RESV4     = M0_ADDB_RECID_RESV_FIRST,
	M0_ADDB_RECID_RESV5,
	M0_ADDB_RECID_RESV6,
	M0_ADDB_RECID_RESV7,
	M0_ADDB_RECID_RESV8,
	M0_ADDB_RECID_RESV9,
	M0_ADDB_RECID_RESV10,

	/** Start of unreserved range */
	M0_ADDB_RECID_RESV_NR
};

/**
   Function failure exception record type, created with the equivalent of
@code
M0_ADDB_RT_EX(m0_addb_rt_func_fail,  M0_ADDB_RECID_FUNC_FAIL, "loc", "rc");
@endcode
   @param loc A context relative location code.
   @param rc  The function return code.
 */
extern struct m0_addb_rec_type m0_addb_rt_func_fail;

/**
   Out-of-memory exception record type, created with the equivalent of
@code
M0_ADDB_RT_EX(m0_addb_rt_oom,  M0_ADDB_RECID_OOM, "loc");
@endcode
   @param loc A context relative location code.
 */
extern struct m0_addb_rec_type m0_addb_rt_oom;

/*
 ****************************************************************************
 * Context type
 ****************************************************************************
 */

/**
   ADDB context type descriptor.
   The descriptor must be registered before it can be used in context
   initialization.
 */
struct m0_addb_ctx_type {
	/* Private */
	uint64_t        act_magic;
	struct m0_tlink act_linkage;

	/** Name of the context type */
	const char     *act_name;
	/**
	   Unique identifier assigned to the context type.
	   The uniqueness will be asserted during registration.
	 */
	uint32_t        act_id;
	/**
	   Size of the @a act_cf array.
	   @pre act_cf_nr > 0
	 */
	size_t          act_cf_nr;
	/**
	   This contains an in-line array of strings which define the
	   names of the context fields in the order they will be specified.
	 */
	const char    *act_cf[];

};

/* Context type macros expand differently based upon the presence of
 * the predicate ::M0_ADDB_CT_DEFINITION macro.
 */
#ifndef M0_ADDB_CT_CREATE_DEFINITION

/**
   This macro expands in two different ways based on the existence of the
   ::M0_ADDB_CT_CREATE_DEFINITION predicate macro.
   If the predicate macro is defined then an ADDB context type C structure
   gets defined.
   If the predicate macro is not defined then an ADDB context type C
   structure gets declared as an extern.
   Context types must be registered before use.
   @param name  Specify the relative name of the context type identifier.
                This is a globally visible identifier.
   @param id    [int32] Specify the unique context type identifier.
   @param ...   Specify the names of the context attributes - the values
                should be C strings.  Contexts may have attributes, each of
		which is a 64 bit unsigned value.
		There is an implementation limit of at most 9 attributes.
   @pre id > 0
   @post name.act_magic == 0
 */
#define M0_ADDB_CT(name, id, ...) \
	extern struct m0_addb_ctx_type name

#else /* M0_ADDB_CT_CREATE_DEFINITION */

#define M0_ADDB_CT(name, id, ...)				\
M0_BASSERT(M0_HAS_TYPE(id, int) && (id) > 0);			\
struct m0_addb_ctx_type name = {				\
	.act_magic = 0,						\
	.act_name = # name,					\
	.act_id = (id),						\
	.act_cf_nr = M0_COUNT_PARAMS(id, ## __VA_ARGS__),	\
	.act_cf = { __VA_ARGS__ }				\
}

#endif /* M0_ADDB_CT_CREATE_DEFINITION */

/**
   Register a context type.  The subroutine will assert that the context
   type identifier is unique among the context types registered so far,
   and that the data in the context type is valid.
   @param ct Pointer to the context type structure.
   @pre ct->act_magic == 0
   @pre ct->act_id > 0
   @pre m0_addb_ctx_type_lookup(ct->act_id) == NULL
   @post m0_addb_ctx_type_lookup(ct->act_id) == ct
   @post ct->act_magic == M0_ADDB_CT_MAGIC
   @see M0_ADDB_CT()
 */
M0_INTERNAL void m0_addb_ctx_type_register(struct m0_addb_ctx_type *ct);

/**
   Look up a context type by context type identifier.
   @param id Specify the context type identifier.
   @return NULL Not found
   @return Pointer if found
   @see m0_addb_cursor_next()
 */
M0_INTERNAL const struct m0_addb_ctx_type *m0_addb_ctx_type_lookup(uint32_t id);

enum {
	/** Context type identifier for a node (upper half of UUID) */
	M0_ADDB_CTXID_NODE_HI = 1,
	/** Context type identifier for a node (lower half of UUID) */
	M0_ADDB_CTXID_NODE_LO = 2,
	/** Context type identifier for the kernel module */
	M0_ADDB_CTXID_KMOD    = 3,
	/** Context type identifier for a process */
	M0_ADDB_CTXID_PROCESS = 4,
	/** Context type identifier for a common for every modules UT ex. ADDB*/
	M0_ADDB_CTXID_UT_SERVICE = 5,
	/** Context type identifier for the ADDB service */
	M0_ADDB_CTXID_ADDB_SERVICE = 6,
	/** Context type identifier for the ADDB statistics posting FOM */
	M0_ADDB_CTXID_ADDB_PFOM = 7,
};

/**
   Context type representing the upper half of a node UUID.
   The context type is used internally by the ADDB subsystem to
   create the parent object of ::m0_addb_node_ctx.
@code
M0_ADDB_CT(m0_addb_ct_node_hi, M0_ADDB_CTXID_NODE_HI);
@endcode
   @param uuid_hi The upper half of the node UUID.
   @see ::m0_addb_node_ctx, M0_ADDB_CT(), ::M0_ADDB_CTXID_NODE_HI
 */
extern struct m0_addb_ctx_type m0_addb_ct_node_hi;

/**
   Context type representing the lower half of a node UUID.
   It is the context type of the ::m0_addb_node_ctx context object.
@code
M0_ADDB_CT(m0_addb_ct_node_lo, M0_ADDB_CTXID_NODE_LO);
@endcode
   @param uuid_lo The lower half of the node UUID.
   @see ::m0_addb_node_ctx, M0_ADDB_CT(), ::M0_ADDB_CTXID_NODE_LO
 */
extern struct m0_addb_ctx_type m0_addb_ct_node_lo;

/**
   Context type for the kernel module.  It is the context type of the
   ::m0_addb_proc_ctx context object in the kernel.
@code
M0_ADDB_CT(m0_addb_ct_kmod, M0_ADDB_CTXID_KMOD, "ts");
@endcode
   @param ts Creation timestamp.
   @see ::m0_addb_proc_ctx, M0_ADDB_CT(), ::M0_ADDB_CTXID_KMOD
 */
extern struct m0_addb_ctx_type m0_addb_ct_kmod;

/**
   Context type for a user space process.  It is the context type of the
   ::m0_addb_proc_ctx context object in user space.
@code
M0_ADDB_CT(m0_addb_ct_process, M0_ADDB_CTXID_PROCESS, "ts", "procid");
@endcode
   @param ts Creation timestamp.
   @param procid Process id
   @see ::m0_addb_proc_ctx, M0_ADDB_CT(), ::M0_ADDB_CTXID_PROCESS
 */
extern struct m0_addb_ctx_type m0_addb_ct_process;

/**
   Context type for UT services.  It is the context type of the
   ::m0_addb_ct_ut_service context object.
@code
M0_ADDB_CT(m0_addb_ct_ut_service, M0_ADDB_CTXID_UT_SERVICE, "hi", "low");
@endcode
   @param Both hi & low just needed to pass no of arguments check
   @see ::m0_addb_ct_ut_service, M0_ADDB_CT(), ::M0_ADDB_CTXID_UT_SERVICE
 */
extern struct m0_addb_ctx_type m0_addb_ct_ut_service;

/**
   Context type for the ADDB service.  It is the context type of the
   ::m0_addb_ct_addb_service context object.
@code
M0_ADDB_CT(m0_addb_ct_addb_service, M0_ADDB_CTXID_ADDB_SERVICE, "hi", "low");
@endcode
   @param Both hi & low just needed to pass no of arguments check
   @see M0_ADDB_CT(), ::M0_ADDB_CTXID_ADDB_SERVICE
 */
extern struct m0_addb_ctx_type m0_addb_ct_addb_service;

/**
   Context type for the ADDB statistics posting FOM.
   It is the context type of the ::m0_addb_ct_addb_pfom context object.
@code
M0_ADDB_CT(m0_addb_ct_addb_pfom, M0_ADDB_CTXID_ADDB_PFOM);
@endcode
   @see M0_ADDB_CT(), ::m0_addb_ct_pfom
 */
extern struct m0_addb_ctx_type m0_addb_ct_addb_pfom;

/** @} end group addb_meta_data */

/*
 ****************************************************************************
 * Counters
 ****************************************************************************
 */

/**
   @defgroup addb_counter ADDB Counters
   @ingroup addb
   @{
 */

/**
 * Counter data.
 */
struct m0_addb_counter_data {
	/** counter sequence number, incremented on post */
	uint64_t                       acd_seq;
	/** number of samples */
	uint64_t                       acd_nr;
	/** sample total */
	uint64_t                       acd_total;
	/** sample minimum */
	uint64_t                       acd_min;
	/** sample maximum */
	uint64_t                       acd_max;
	/** sum of squares */
	uint64_t                       acd_sum_sq;
	/**
	   Histogram, if acn_rt->art_rf_nr != 0.
	   There are (acn_rt->art_rf_nr + 1) buckets in the histogram.
	 */
	uint64_t                       acd_hist[];
};

/**
 * Counter data object
 *
 * @see m0_addb_counter_init()
 */
struct m0_addb_counter {
	uint64_t                       acn_magic;
	/** the counter's record type */
	const struct m0_addb_rec_type *acn_rt;
	struct m0_addb_counter_data   *acn_data;
};

/**
   Initialize a counter.

   @param c  Specify the counter to initialize.
   @param rt Specify the counter record type.

   @pre rt->art_base_type == M0_ADDB_BRT_CNTR
   @see M0_ADDB_RT_CNTR()
   @see m0_addb_counter_update() m0_addb_counter_fini()
 */
M0_INTERNAL int m0_addb_counter_init(struct m0_addb_counter        *c,
				     const struct m0_addb_rec_type *rt);

/**
   Finalize the counter.
 */
M0_INTERNAL void m0_addb_counter_fini(struct m0_addb_counter *c);

/**
   Update the counter with a new sample.  The statistical fields are updated
   on success.

   @pre addb_counter_invariant(c) && datum <= UINT_MAX
   @return -EOVERFLOW The new datum would cause the counter to overflow.  The
   datum is not applied to the counter.  In this situation, the caller should
   post the counter and re-attempt the update.

   @see M0_ADDB_POST_CNTR()
 */
M0_INTERNAL int m0_addb_counter_update(struct m0_addb_counter *c,
				       uint64_t datum);

/**
   Returns the number of samples in the counter.
 */
M0_INTERNAL uint64_t m0_addb_counter_nr(const struct m0_addb_counter *c);

/**
   Low level subroutine used to reset the counter after it has been posted.
   The statistical fields are set to 0.

   @see M0_ADDB_POST_CNTR()
 */
M0_INTERNAL void m0__addb_counter_reset(struct m0_addb_counter *c);

/** @} end group addb_counter */

/*
 ****************************************************************************
 * ADDB machine
 ****************************************************************************
 */

/**
   @defgroup addb_mc ADDB Machine
   @ingroup addb

   Use the global ADDB machine, ::m0_addb_gmc, in general.  Some environments
   may define private ADDB machines, such as ::m0_reqh.

   @{
 */

/** ADDB machine object. */
struct m0_addb_mc {
	uint64_t                   am_magic;
	struct m0_addb_mc_evmgr   *am_evmgr;  /* required */
	struct m0_addb_mc_recsink *am_sink;   /* optional */
};

/**
   Global ADDB machine.
   Should be used by default unless a special ADDB machine is defined
   for the execution environment.

   The global machine is initialized by m0_addb_init() but must be configured
   explicitly based upon the execution context.  It is finalized by
   m0_addb_fini() if not already finalized by the application.
 */
extern struct m0_addb_mc m0_addb_gmc;

/**
   Initialize an ADDB machine.
 */
M0_INTERNAL void m0_addb_mc_init(struct m0_addb_mc *mc);

/**
   Predicate to determine if an ADDB machine has been initialized.
 */
M0_INTERNAL bool m0_addb_mc_is_initialized(const struct m0_addb_mc *mc);

/**
   Finalize an ADDB machine.
   It will release all the referenced components of the machine and finalize
   them if their reference counts go to zero.
   @pre m0_addb_mc_is_initialized(mc)
   @post !m0_addb_mc_is_initialized(mc)
 */
M0_INTERNAL void m0_addb_mc_fini(struct m0_addb_mc *mc);

/**
   Predicate to determine if an ADDB machine has an event manager.
   It may also check that the configured component pointers are valid.
   @pre m0_addb_mc_is_initialized(mc)
   @return mc->am_evmgr != NULL
 */
M0_INTERNAL bool m0_addb_mc_has_evmgr(const struct m0_addb_mc *mc);

/**
   Predicate to determine if an ADDB machine has a record sink.
   It may also check that the configured component pointers are valid.
   @pre m0_addb_mc_is_initialized(mc)
   @return mc->am_sink != NULL
 */
M0_INTERNAL bool m0_addb_mc_has_recsink(const struct m0_addb_mc *mc);

/**
   Predicate to determine if an ADDB machine is configured with at least
   one component.
   It may also check that the configured component pointers are valid.
   @return m0_addb_mc_has_evmgr(mc) || m0_addb_mc_has_recsink(mc)
 */
M0_INTERNAL bool m0_addb_mc_is_configured(const struct m0_addb_mc *mc);

/**
   Predicate to determine if an ADDB machine is fully configured.
   It may also check that the configured component pointers are valid.
   @return m0_addb_mc_has_evmgr(mc) && m0_addb_mc_has_recsink(mc)
 */
M0_INTERNAL bool m0_addb_mc_is_fully_configured(const struct m0_addb_mc *mc);

/**
   Predicate to determine if an ADDB machine can post in awkward contexts.
   @pre m0_addb_mc_has_evmgr(mc)
 */
M0_INTERNAL bool m0_addb_mc_can_post_awkward(const struct m0_addb_mc *mc);

/**
   Configure a passthrough event manager.
   A record sink must be configured.
   @pre m0_addb_mc_is_initialized(mc)
   @pre !m0_addb_mc_has_evmgr(mc)
   @pre m0_addb_mc_has_recsink(mc)
   @post !m0_addb_mc_can_post_awkward(mc)
 */
M0_INTERNAL void m0_addb_mc_configure_pt_evmgr(struct m0_addb_mc *mc);

/**
   Configure a caching event manager.
   @param mc   The ADDB machine.
   @param size The size of the cache in 64 bit words.
   @pre m0_addb_mc_is_initialized(mc)
   @pre !m0_addb_mc_has_evmgr(mc)
   @post m0_addb_mc_can_post_awkward(mc)
 */
M0_INTERNAL void m0_addb_mc_configure_cache_evmgr(struct m0_addb_mc *mc,
						  size_t size);

/**
   Flush cached records saved in a caching event manager to an ADDB machine with
   a record sink.  This should be done in an execution context supported by both
   the machines.
   @param cache_mc The ADDB machine with a cached event manager.
   @param sink_mc  The ADDB machine with a record sink.
   @pre m0_addb_can_post_awkward(cache_mc)
   @pre m0_addb_mc_has_recsink(sink_mc)
 */
M0_INTERNAL void m0_addb_mc_cache_evmgr_flush(struct m0_addb_mc *cache_mc,
					      struct m0_addb_mc *sink_mc);

/**
   Configure an RPC sink.
   Configuring a record sink for the global ADDB machine will trigger posting
   of the global context definition records.
   @pre m0_addb_mc_is_initialized(mc)
   @pre !m0_addb_mc_has_recsink(mc)
 */
M0_INTERNAL int m0_addb_mc_configure_rpc_sink(struct m0_addb_mc *mc,
					      struct m0_rpc_machine
					      *rpc_machine);

#ifndef __KERNEL__
/**
   Configure a stob sink.  The reference count of the stob is incremented.
   Configuring a record sink for the global ADDB machine will trigger posting
   of the global context definition records.
   The rel_timeout is used to cause buffered records to be flushed if the time
   since the last flush exceeds the timeout.
   @pre m0_addb_mc_is_initialized(mc)
   @pre !m0_addb_mc_has_recsink(mc)
   @pre segment_size > 0 && max_stob_size >= segment_size && rel_timeout > 0
 */
M0_INTERNAL int m0_addb_mc_configure_stob_sink(struct m0_addb_mc *mc,
					       struct m0_stob    *stob,
					       m0_bcount_t        segment_size,
					       m0_bcount_t        max_stob_size,
					       m0_time_t          rel_timeout);
#endif

/**
   Duplicate the configuration of an ADDB machine. The reference counts of
   the configured components are incremented.
   Configuring a record sink for the global ADDB machine will trigger posting
   of the global context definition records.
   @param src_mc The machine whose configuration should be copied.
   @param tgt_mc   The machine to be configured.
   @pre m0_addb_mc_is_configured(src_mc)
   @pre !m0_addb_can_post_awkward(src_mc)
   @pre !m0_addb_mc_is_configured(tgt_mc)
 */
M0_INTERNAL void m0_addb_mc_dup(const struct m0_addb_mc *src_mc,
				struct m0_addb_mc *tgt_mc);

/** @} end group addb_mc */

/**
   @defgroup addb_post ADDB Contexts and Posting
   @ingroup addb
   @{
 */

/*
 ****************************************************************************
 * Contexts
 ****************************************************************************
 */

/**
   Represents a context in which an ADDB event is posted.
   There are two ways to initialize this object:
   - With the M0_ADDB_CTX_INIT() macro (A)
   - With the m0_addb_ctx_import() subroutine (B)

   Some structure fields are not used in every case, as indicated in their
   description.  If not used a field value is set to 0.

   The context identifier of a context object initialized in case A is
   constructed when needed by concatenating the @a ac_id field to its parent
   context object's identifier.  This is a recursive definition, involving
   traversal of the @a ac_parent links up to the root of the context object
   tree.

   The context identifier of a context object initialized in case B is pointed
   to by its @a ac_imp_id field.  The memory should not be altered until the
   context object is finalized.
 */
struct m0_addb_ctx {
	uint64_t                       ac_magic;
	/** Identifies the context type. (A) */
	const struct m0_addb_ctx_type *ac_type;
	/** The relative context identifier. (A) */
	uint64_t                       ac_id;
	/** Pointer to the parent context. (A) */
	struct m0_addb_ctx            *ac_parent;
	/** Points to the imported context id. (B) */
	const uint64_t                *ac_imp_id;
	/** Counter to aid in leaf context identifier numbering. */
	uint64_t                       ac_cntr;
	/**
	   Depth of the object from the root. Equal to the length of the context
	   identifier, or equivalently, the length of the context path from the
	   root.
	 */
	uint32_t                       ac_depth;
};

/**
   Global node context.
   This can be used as a parent for other contexts.
   The @a ac_cntr value is primed with its creation time, providing a
   relatively unique starting number for child contexts.

   Context object identifiers are restricted to 64 bits only.  This object's
   identifier reflects the lower half of the node's 128 bit UUID; the upper half
   can be referenced via the @a ac_parent pointer.

   This context is initialized by m0_addb_init() and finalized by
   m0_addb_fini().

   @see @ref m0_addb_ct_node_lo, M0_ADDB_CTXID_NODE_LO
   @see @ref m0_addb_ct_node_hi, M0_ADDB_CTXID_NODE_HI
   @see m0_node_uuid
 */
extern struct m0_addb_ctx m0_addb_node_ctx;

/**
   Global container context identifying the process or kernel module on
   the node.  The object is created with the ::m0_addb_node_ctx object as
   its parent. Its @a ac_id value has a time component to make it unique
   in time.  Its @a ac_cntr field is initialized to zero.
   This object can be used as a parent for other contexts.

   This context is initialized by m0_addb_init() and finalized by
   m0_addb_fini().
   @see @ref m0_addb_ct_kmod, M0_ADDB_CTXID_KMOD
   @see @ref m0_addb_ct_process, M0_ADDB_CTXID_PROCESS
 */
extern struct m0_addb_ctx m0_addb_proc_ctx;

/**
   Macro to initialize a context object.  The macro validates the
   number of fields specified.

   The parent object's @a ac_cntr value is used to assign a value to the @a
   ac_id field of the context object being initialized.  The parent counter
   value is subsequently incremented.  When an ADDB subsystem provided global
   context object is used as the parent, the manipulation of its counter is
   implicitly serialized by this macro; the use of any other context object as
   the parent must be adequately serialized by the invoker.

   @param mc Specify an ADDB machine. The context object can be used
   to post with any ADDB machine after initialization.
   @param ctx Specify the context object to initialize.
   @param ct Specify the context type.
   @param parent Specify the parent context object. This must not be a context
   object initialized by import.
   @param ... Specify the context fields.
   @pre m0_addb_mc_has_recsink(mc)
   @pre !m0_addb_ctx_is_imported(parent)
   @post ctx->ac_id == parent->ac_cntr - 1 (parent counter post-incremented)
   @post ctx->ac_cntr == 0
   @post ctx->ac_imp_id == NULL
   @see m0_addb_ctx_fini()
   @see ::m0_addb_node_ctx, ::m0_addb_proc_ctx
 */
#define M0_ADDB_CTX_INIT(mc, ctx, ct, parent, ...)			\
M0_CAT(M0__ADDB_CTX_INIT, M0_COUNT_PARAMS(parent, ## __VA_ARGS__))	\
(mc, ctx, ct, parent, ## __VA_ARGS__)

/**
   Copy a context identifier (the path from root to leaf object) with the
   intention of conveying it to some other process, possibly remote, for
   reference.
   @param ctx The context object whose path should be copied.
   @param id  The returned context identifier. The allocated memory should
   be freed with m0_addb_ctx_id_free().
   @pre id->au64s_nr == 0 && id->au64s_data == NULL
   @post id->au64s_nr != 0 && id->au64s_data != NULL
   @see m0_addb_ctx_import(), m0_addb_ctx_id_free()
 */
M0_INTERNAL int m0_addb_ctx_export(struct m0_addb_ctx *ctx,
				   struct m0_addb_uint64_seq *id);

/**
   Release the memory allocated by m0_addb_ctx_export().
   @pre id->au64s_nr != 0 && id->au64s_data != NULL
   @post id->au64s_nr == 0 && id->au64s_data == NULL
 */
M0_INTERNAL void m0_addb_ctx_id_free(struct m0_addb_uint64_seq *id);

/**
   Initialize a context object from a context identifier (import operation).
   The memory pointed to by the context identifier should not be released until
   the context object is finalized.

   The context object can subsequently be used in posts.  However, an imported
   context may not be used as the parent of a new context object.
   @param ctx The context object to be initialized.
   @param id  The context identifier being imported.  The identifier should
   have been obtained originally with the m0_addb_ctx_export() subroutine.
   @post ctx->ac_imp_id != NULL
   @post ctx->ac_type == NULL && ctx->ac_parent == NULL && ctx->ac_id == 0
   @see m0_addb_ctx_fini() m0_addb_ctx_export()
 */
M0_INTERNAL int m0_addb_ctx_import(struct m0_addb_ctx *ctx,
				   const struct m0_addb_uint64_seq *id);

/**
   Predicate to determine if a context object has been initialized
   by import.
   @pre The context object has been initialized.
 */
M0_INTERNAL bool m0_addb_ctx_is_imported(const struct m0_addb_ctx *ctx);

/**
   Low level subroutine used by M0_ADDB_CTX_INIT().
   @pre m0_addb_mc_has_ctxmgr(mc)
   @see M0_ADDB_CTX_INIT()
   @see m0_addb_ctx_fini()
 */
M0_INTERNAL void m0__addb_ctx_init(struct m0_addb_mc *mc,
				   struct m0_addb_ctx *ctx,
				   const struct m0_addb_ctx_type *ct,
				   struct m0_addb_ctx *parent,
				   uint64_t *fields);

/**
   Finalize the context object.
   Any memory referenced by the context object may now be released.
   This includes parent object references, context type references and
   references to imported context identifiers.
   @see M0_ADDB_CTX_INIT()
 */
M0_INTERNAL void m0_addb_ctx_fini(struct m0_addb_ctx *ctx);

/**
   Predicate to determine if a context object has been initialized.
 */
M0_INTERNAL bool m0_addb_ctx_is_initialized(const struct m0_addb_ctx *ctx);

/*
 ****************************************************************************
 * Posting
 ****************************************************************************
 */

/**
   A context vector helper macro.
   @param ... Specify one or more context object pointers.
 */
#define M0_ADDB_CTX_VEC(...) (struct m0_addb_ctx *[]){__VA_ARGS__, NULL}

/**
   Internal structure used during posting. It is filled in by
   the M0_ADDB_POST() macro.
 */
struct m0_addb_post_data {
	/** record type */
	const struct m0_addb_rec_type      *apd_rt;
	/** context vector */
	struct m0_addb_ctx                **apd_cv;
	union {
		/** counter */
		struct m0_addb_counter     *apd_cntr;
		/** sequence */
		struct m0_addb_uint64_seq  *apd_seq;
		/** argument array (count via apd_rt) */
		uint64_t                   *apd_args;
	} u;
};

/**
   Macro to post ADDB exceptions and data point records.
   The macro validates that the count of the arguments is as described by the
   record type, and that the argument data type is as expected.
   @param mc  Specify the ADDB machine.
   @param rt  Specify the pointer to the ADDB record type.
   @param cv  Specify the context vector, a null terminated array of one or
   more context object pointers.
   @param ... Specify the arguments; the argument data type is uint64_t.
   @pre m0_addb_mc_has_evmgr(mc)
   @see M0_ADDB_CTX_VEC(), M0_ADDB_POST_CNTR(), M0_ADDB_POST_SEQ()
 */
#define M0_ADDB_POST(mc, rt, cv, ...)					\
do {									\
	struct m0_addb_post_data pd;					\
	pd.apd_rt = rt;							\
	pd.apd_cv = cv;							\
	M0_ASSERT(pd.apd_rt->art_base_type == M0_ADDB_BRT_DP ||		\
		  pd.apd_rt->art_base_type == M0_ADDB_BRT_EX);		\
	M0_CAT(M0__ADDB_POST, M0_COUNT_PARAMS(X, ## __VA_ARGS__))	\
		(pd.apd_rt, ## __VA_ARGS__);				\
	m0__addb_post(mc, &pd);						\
} while (0)

/**
   Macro to post an ADDB counter record.
   @param mc   Specify the ADDB machine.
   @param cv   Specify the context vector, a null terminated array of one or
   more context object pointers.
   @param cntr Specify the pointer to the counter.  A side effect of posting
   the counter is that it is reset and its sequence number incremented.
   @pre m0_addb_mc_has_evmgr(mc)
   @see M0_ADDB_CTX_VEC(), M0_ADDB_POST(), M0_ADDB_POST_SEQ()
 */
#define M0_ADDB_POST_CNTR(mc, cv, cntr)					\
do {									\
	struct m0_addb_post_data pd;					\
	pd.u.apd_cntr = cntr;						\
	M0_ASSERT(pd.u.apd_cntr != NULL);				\
	pd.apd_rt     = pd.u.apd_cntr->acn_rt;				\
	pd.apd_cv     = cv;						\
	M0_ASSERT(pd.apd_rt->art_base_type == M0_ADDB_BRT_CNTR);	\
	m0__addb_post(mc, &pd);						\
	m0__addb_counter_reset(pd.u.apd_cntr);				\
} while (0)

/**
   Macro to post an ADDB sequence record.
   @param mc   Specify the ADDB machine.
   @param rt   Specify the pointer to the ADDB record type.
   @param cv   Specify the context vector, a null terminated array of one or
   more context object pointers.
   @param seq  Specify the pointer to the sequence.
   @pre m0_addb_mc_has_evmgr(mc)
   @see M0_ADDB_CTX_VEC(), M0_ADDB_POST(), M0_ADDB_POST_CNTR()
 */
#define M0_ADDB_POST_SEQ(mc, rt, cv, seq)			\
do {								\
	struct m0_addb_post_data pd;				\
	pd.apd_rt    = rt;					\
	pd.apd_cv    = cv;					\
	pd.u.apd_seq = seq;					\
	M0_ASSERT(pd.apd_rt->art_base_type == M0_ADDB_BRT_SEQ);	\
	m0__addb_post(mc, &pd);					\
} while (0)

/**
   Low level subroutine used by the posting macros.
   It invokes the ADDB machine's event manager post interface internally.
   It knows how to handle counter and sequence base record types.
   @param mc The ADDB machine.
   @param pd The posted record datum.
   @pre m0_addb_mc_has_evmgr(mc)
   @see M0_ADDB_POST(), M0_ADDB_POST_CNTR(), M0_ADDB_POST_SEQ()
 */
M0_INTERNAL void m0__addb_post(struct m0_addb_mc *mc,
			       struct m0_addb_post_data *pd);

/**
   Convenience macro to post a function failure exception record.
   @param mc  The ADDB machine pointer.
   @param loc A context specific location identifier.
   @param rc  The error code.
   @param ... One or more context pointers.
 */
#define M0_ADDB_FUNC_FAIL(mc, loc, rc, ...)			\
do {								\
	int _rc = (rc);						\
	M0_ASSERT(_rc < 0);					\
	M0_ADDB_POST((mc), &m0_addb_rt_func_fail,		\
		     M0_ADDB_CTX_VEC(__VA_ARGS__), (loc), _rc);	\
} while (0)

/**
   Convenience macro to post an out-of-memory exception record.
   @param mc  The ADDB machine pointer.
   @param loc A context specific location identifier.
   @param ... One or more context pointers.
 */
#define M0_ADDB_OOM(mc, loc, ...)				\
do {								\
	M0_ADDB_POST((mc), &m0_addb_rt_oom,			\
		     M0_ADDB_CTX_VEC(__VA_ARGS__), (loc));	\
} while (0)

/** @} end group addb_post */

/*
 ****************************************************************************
 * Retrieval
 ****************************************************************************
 */

/**
   @defgroup addb_retrieval ADDB Record Retrieval
   @ingroup addb
   @{
 */

#ifndef __KERNEL__
/**
   Abstract ADDB repository segment iterator.
 */
struct m0_addb_segment_iter {
	/**
	   Get a cursor into the next segment of the ADDB repository.
	   Only complete segments are returned.
	   @param iter Segment Iterator object.
	   @param cur On success, set to the start of the record data in the
	   segment.  The cursor may be used until the next call to asi_next(),
	   asi_nextbuf() or asi_seq_set(), or until the iterator is freed,
	   whichever occurs first.
	   @return A positive number denotes the number of records in the
	   segment.  Zero denotes that more segments are available (EOF).
	   A negative number denotes that an error occurred.
	 */
	int (*asi_next)(struct m0_addb_segment_iter *iter,
			struct m0_bufvec_cursor     *cur);
	/**
	   Get a buffer containing the next segment of the ADDB repository.
	   Only complete segments are returned.
	   @param iter Segment Iterator object.
	   @param bv On success, set to a bufvec containing the data of the
	   segment.  The buffer may be used until the next call to asi_next(),
	   asi_nextbuf() or asi_seq_set(), or until the iterator is freed,
	   whichever occurs first.
	 */
	int (*asi_nextbuf)(struct m0_addb_segment_iter *iter,
			   const struct m0_bufvec     **bv);
	/**
	   Gets the sequence number of the current segment.
	   @return a positive sequence number is returned after a call to
	   asi_next() or asi_nextbuf() has been made and has successfully read
	   in a segment.  Otherwise, 0 is returned (i.e. before a segment was
	   read or after EOF).
	 */
	uint64_t (*asi_seq_get)(struct m0_addb_segment_iter *iter);
	/**
	   Set the minimum desired sequence number of subsequent segments.
	   This has the effect of causing the iterator to skip over any segments
	   whose sequence number is less than the desired value.  Whether the
	   iterator performs some sort of seek internally is left to the
	   implementation.  asi_next() or asi_nextbuf() must be called to access
	   the resulting next segment.
	 */
	void (*asi_seq_set)(struct m0_addb_segment_iter *iter, uint64_t seq_nr);
	/**
	   Free the segment iterator.
	 */
	void (*asi_free)(struct m0_addb_segment_iter *iter);
};

/**
   Configure an ADDB repository segment iterator reading directly from
   a stob.
   @param iter On success, initialized iterator object is returned.
   @param stob The stob from which to read segments, on success, an additional
   reference is taken on the stob.
 */
M0_INTERNAL int m0_addb_stob_iter_alloc(struct m0_addb_segment_iter **iter,
					struct m0_stob *stob);
/**
   Configure an ADDB repository segment iterator reading from a binary file.
   @param iter On success, initialized iterator object is returned.
   @param path The path to the binary file containing ADDB data previously
   extracted from an ADDB stob.
 */
M0_INTERNAL int m0_addb_file_iter_alloc(struct m0_addb_segment_iter **iter,
					const char *path);

M0_INTERNAL void m0_addb_segment_iter_free(struct m0_addb_segment_iter *iter);

/**
   Cursor operational flags.
 */
enum {
	/** Return event records through the cursor */
	M0_ADDB_CURSOR_REC_EVENT = 1 << 0,
	/** Return context records through the cursor */
	M0_ADDB_CURSOR_REC_CTX   = 1 << 1,
	/** Return any type of record through the cursor */
	M0_ADDB_CURSOR_REC_ANY   = (M0_ADDB_CURSOR_REC_EVENT |
				    M0_ADDB_CURSOR_REC_CTX),
};

/**
  A cursor to read from an ADDB repository.
  All fields are to be used only by the implementation.
 */
struct m0_addb_cursor {
	struct m0_addb_segment_iter *ac_iter;
	/** Cursor into segment buffer */
	struct m0_bufvec_cursor      ac_cur;
	uint32_t                     ac_flags;
	/** remaining records in current segment */
	uint32_t                     ac_rec_nr;
	/** Record data for most recent record */
	struct m0_addb_rec          *ac_rec;
};

/**
   Initialize a cursor to read a repository.
   @param cur   The cursor object.
   @param iter  The repository segment iterator.
   @param flags Operational flags.  If 0, all records are retrieved.
 */
M0_INTERNAL int m0_addb_cursor_init(struct m0_addb_cursor *cur,
				    struct m0_addb_segment_iter *iter,
				    uint32_t flags);

/**
   Fetch the next record.
   @param cur The cursor object.
   @param rec The next record.  All referenced memory is managed by the cursor
   and is valid only until the next cursor subroutine invocation.
   @retval 0  Success
   @retval -ENODATA No more records available.
   @see ::m0_addb_rec
   @see m0_addb_rec_is_event()
   @see m0_addb_rec_is_ctx()
 */
M0_INTERNAL int m0_addb_cursor_next(struct m0_addb_cursor *cur,
				    struct m0_addb_rec **rec);

/**
   Close the cursor.
 */
M0_INTERNAL void m0_addb_cursor_fini(struct m0_addb_cursor *cur);
#endif /* __KERNEL__ */

/**
   Predicate to determine if a record is an event record.

   The record type of event records is recovered with
@code
   const struct m0_addb_rec_type *rt =
            m0_addb_rec_type_lookup(m0_addb_rec_rid_to_id(rec->ar_rid));
@endcode
   and the record fields are found in the @a rec->ar_data sequence.

   @return m0_addb_rec_rid_to_brt(rec->ar_rid) <= M0_ADDB_BRT_SEQ
   @see m0_addb_rec_type_lookup()
 */
M0_INTERNAL bool m0_addb_rec_is_event(const struct m0_addb_rec *rec);

/**
   Predicate to determine if a record is a context record.

   The context type of context records is recovered with
@code
   const struct m0_addb_ctx_type *rt =
            m0_addb_ctx_type_lookup(m0_addb_rec_rid_to_id(rec->ar_rid));
@endcode
   and the context fields are found in the @a rec->ar_data sequence.
   There will be only one entry in the @a rec->ar_ctxids sequence, and
   that will be the context identifier of the context described by the
   record.

   @return m0_addb_rec_rid_to_brt(rec->ar_rid) == M0_ADDB_BRT_CTXDEF
   @see m0_addb_ctx_type_lookup()
 */
M0_INTERNAL bool m0_addb_rec_is_ctx(const struct m0_addb_rec *rec);

/**
   Subroutine to construct the ADDB record identifier field.
   @param brt Base record type
   @param id  Identifier relative to @a brt.
   @see m0_addb_rec
 */
M0_INTERNAL uint64_t m0_addb_rec_rid_make(enum m0_addb_base_rec_type brt,
					  uint32_t id);

/**
   Subroutine to recover the base record type from the ADDB record identifier.
   @see m0_addb_rec
 */
M0_INTERNAL enum m0_addb_base_rec_type m0_addb_rec_rid_to_brt(uint64_t rid);

/**
   Subroutine to recover the (relative) identifier field from the ADDB record
   identifier.  Note that the interpretation of this field depends on the base
   record type.
   @see m0_addb_rec
 */
M0_INTERNAL uint32_t m0_addb_rec_rid_to_id(uint64_t rid);

/** @} end of addb_retrieval group */

/*
 ****************************************************************************
 * ADDB service module
 ****************************************************************************
 */

/**
   @defgroup addb_svc ADDB Service
   @ingroup addb

   The ADDB service exists in user space only. It is responsible for:
   - Receiving ADDB data from non-service clients.
   - Periodic ADDB operations within the server, such as the posting of
   statistical data, and flushing the stob record sink.  These periodic
   operations are executed by a long lived @ref fom "FOM".

   The service is called "addb", and one service instance must be created for
   each @ref reqh "Request Handler".
   @{
 */

#ifndef __KERNEL__
/**
   ADDB service module initializer
 */
M0_INTERNAL int m0_addb_svc_mod_init();

/**
   ADDB service module finalizer
 */
M0_INTERNAL void m0_addb_svc_mod_fini();

#endif /* __KERNEL__ */

/** @} end of addb_svc group */

/**
   @addtogroup uuid
   @{
 */

/**
   The UUID of a Mero node.  Defined and initialized by the ADDB subsystem.
 */
extern struct m0_uint128 m0_node_uuid;

/** @} end of uuid group */

/* __MERO_ADDB_ADDB_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
