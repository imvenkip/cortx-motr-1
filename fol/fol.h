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
 * Original creation date: 09/09/2010
 */

#pragma once

#ifndef __MERO_FOL_FOL_H__
#define __MERO_FOL_FOL_H__

/**
   @defgroup fol File operations log

   File operations log (fol) is a per-node collection of records, describing
   updates to file system state carried out on the node. See HLD for the
   description of requirements, usage patterns and constraints on fol,
   as well as important terminology (update, operation, etc.):
   https://docs.google.com/a/xyratex.com/document/d/1_5UGU0n7CATMiuG6V9eK3cMshiYFotPVnIy478MMnvM/comment .

   A fol is represented by an instance of struct m0_fol. A fol record has two
   data-types associated with it:

   @li m0_fol_rec_desc: a description of a new fol record to be added to a fol.

   @li m0_fol_rec: a record fetched from a fol. m0_fol_rec remembers its
   location in the fol.

   A fol record contains the list of fol record parts, belonging to fol record
   part types, added during updates. These fol record parts provide flexibility
   for modules to participate in a transaction without global knowledge.

   @see m0_fol_rec_part : FOL record part.
   @see m0_fol_rec_part_type : FOL record part type.

   m0_fol_rec_part_ops structure contains operations for undo and redo of
   FOL record parts.

   @see m0_fol_rec_part_init() : Initializes m0_fol_rec_part with
				 m0_fol_rec_part_type_ops.
   @see m0_fol_rec_part_fini() : Finalizes FOL record part.

   @see m0_fol_rec_part_type_register() : Registers FOL record part type.
   @see m0_fol_rec_part_type_deregister() : Deregisters FOL record part type.

   FOL record parts list is kept in m0_fol_rec::fr_parts which is
   initialized in m0_fol_rec_init().

   m0_fol_rec_add() is used to compose FOL record from FOL record descriptor
   and parts.
   fol_record_encode() encodes the FOL record parts in the list
   m0_fol_rec:fr_parts in a buffer, which then will be added into the db
   using m0_fol_add_buf().

   @see m0_fol_rec_add()
   @see m0_fol_rec_lookup()

   m0_fol_rec_part_type_init() and m0_fol_rec_part_type_fini() are added
   to initialize and finalize FOL part types.
   FOL record part types are registered in a global array of FOL record
   parts using m0_fol_rec_part_type::rpt_index.

   After successful execution of updates on server side, in FOM generic phase
   using m0_fom_fol_rec_add() FOL record parts in the list are combined in a
   FOL record and is made persistent. Before this phase all FOL record parts
   need to be added in the list after completing their updates.

   After retrieving FOL record from the storage, FOL record parts are decoded
   based on part type using index and are used in undo or redo operations.
   <hr>
   @section IOFOLDLD-conformance Conformance

   @{
 */

#define XXX_USE_DB5 0

/* export */
struct m0_fol;
struct m0_fol_rec_desc;
struct m0_fol_rec;

/* import */
#include "lib/types.h"      /* uint64_t */
#include "lib/arith.h"      /* M0_IS_8ALIGNED */
#include "lib/mutex.h"
#include "fid/fid.h"
#include "dtm/dtm_update.h" /* m0_update_id, m0_epoch_id */
#include "dtm/verno.h"      /* m0_verno */
#if XXX_USE_DB5
#  include "db/db.h"        /* m0_table, m0_db_cursor */
#else
#  include "be/btree.h"
#endif
#include "fol/lsn.h"        /* m0_lsn_t */
#include "fid/fid_xc.h"
#include "dtm/verno_xc.h"
#include "dtm/dtm_update_xc.h"

#if XXX_USE_DB5
struct m0_dbenv;
struct m0_db_tx;
#endif
struct m0_be_tx;
struct m0_epoch_id;

/**
   In-memory representation of a fol.

   <b>Liveness rules and concurrency control.</b>

   m0_fol liveness is managed by the user (the structure is not reference
   counted) which is responsible for guaranteeing that m0_fol_fini() is the last
   call against a given instance.

   FOL code manages concurrency internally: multiple threads can call m0_fol_*()
   functions against the same fol (except for m0_fol_init() and m0_fol_fini()).
 */
struct m0_fol {
#if XXX_USE_DB5
	/** Table where fol records are stored. */
	struct m0_table    f_table;
#else
	/** KV storage of fol records. */
	struct m0_be_btree f_store;
#endif
	/** Next lsn to use in the fol. */
	m0_lsn_t           f_lsn;
	/** Lock, serializing fol access. */
	struct m0_mutex    f_lock;
	struct m0_reqh    *f_reqh;
};

/**
   Initialise in-memory fol structure, creating persistent structures, if
   necessary.

   @post ergo(result == 0, m0_lsn_is_valid(fol->f_lsn))
 */
#if XXX_USE_DB5
M0_INTERNAL int m0_fol_init(struct m0_fol *fol, struct m0_dbenv *env);
#else
M0_INTERNAL void m0_fol_init(struct m0_fol *fol, struct m0_be_seg *seg);

M0_INTERNAL int m0_fol_create(struct m0_fol *fol, struct m0_be_tx *tx,
			      struct m0_be_op *op);

M0_INTERNAL void m0_fol_destroy(struct m0_fol *fol, struct m0_be_tx *tx,
			        struct m0_be_op *op);
#endif
M0_INTERNAL void m0_fol_fini(struct m0_fol *fol);

#if !XXX_USE_DB5
/** Fol operations that modify back-end segment. */
enum m0_fol_op {
	M0_FO_CREATE,    /**< m0_fol_create() */
	M0_FO_DESTROY,   /**< m0_fol_destroy() */
	M0_FO_REC_ADD  /**< m0_fol_rec_add() */
};

/**
 * Calculates the credit needed to perform `nr' fol operations of type
 * `optype' and adds this credit to `accum'.
 */
M0_INTERNAL void m0_fol_credit(const struct m0_fol *fol, enum m0_fol_op optype,
			       m0_bcount_t nr, struct m0_be_tx_credit *accum);
#endif

/**
   Adds a record to the fol, in the transaction context.

   rec->fr_desc->rd_lsn must be filled by the caller using
   m0_fol_lsn_allocate(fol)

   rec->fr_desc->rd_refcounter is initial value of record's reference counter.
   This field must be filled by the caller.

   @pre m0_lsn_is_valid(drec->rd_lsn);
   @see m0_fol_add_buf()
 */
#if XXX_USE_DB5
M0_INTERNAL int m0_fol_rec_add(struct m0_fol *fol,
			       struct m0_db_tx *tx,
			       struct m0_fol_rec *rec);
#else
M0_INTERNAL int m0_fol_rec_add(struct m0_fol *fol,
			       struct m0_fol_rec *rec,
			       struct m0_be_tx *tx,
			       struct m0_be_op *op);
#endif

/**
   Similar to m0_fol_rec_add(), but with a record already packed into a buffer.

   @pre m0_lsn_is_valid(drec->rd_lsn);
 */
#if XXX_USE_DB5
M0_INTERNAL int m0_fol_add_buf(struct m0_fol *fol,
			       struct m0_db_tx *tx,
			       struct m0_fol_rec_desc *drec,
			       struct m0_buf *buf);
#else
M0_INTERNAL int m0_fol_add_buf(struct m0_fol *fol,
			       struct m0_fol_rec_desc *drec,
			       struct m0_buf *buf,
			       struct m0_be_tx *tx,
			       struct m0_be_op *op);
#endif

/**
   Reserves and returns lsn.

   @post m0_lsn_is_valid(result);
 */
M0_INTERNAL m0_lsn_t m0_fol_lsn_allocate(struct m0_fol *fol);

/**
   Forces the log.

   On successful return from this call it is guaranteed that all fol records
   with lsn not greater than "upto" are persistent.

   The implementation is free to make other records persistent as it sees fit.
 */
M0_INTERNAL int m0_fol_force(struct m0_fol *fol, m0_lsn_t upto);

/**
   Fixed part of a fol record.

   @see m0_fol_rec_desc
 */
struct m0_fol_rec_header {
	/** Number of outstanding references to the record. */
	uint64_t            rh_refcount;
	/** Number of record parts added to the record. */
	uint32_t            rh_parts_nr;
	/**
	 * Length of the remaining operation type specific data in bytes.
	 *
	 * @note XXX Currently this is the length of encoded record.
	 */
	uint32_t            rh_data_len;
	/**
	 * Identifier of this update.
	 *
	 * @note The update might be for a different node.
	 */
	struct m0_update_id rh_self;
	uint64_t            rh_magic;
} M0_XCA_RECORD;

M0_BASSERT(M0_IS_8ALIGNED(sizeof(struct m0_fol_rec_header)));

/**
   In-memory representation of a fol record.

   m0_fol_rec_desc is used in two ways:

   @li as an argument to m0_fol_add(). In this case m0_fol_rec_desc describes a
   new record to be added. All fields, except for rd_lsn, are filled in by a
   user and the user is responsible for the concurrency control and the liveness
   of the structure;

   @li as part of m0_fol_rec returned by m0_fol_rec_lookup() or
   m0_fol_batch(). In this case, m0_fol_rec_desc is filled by the fol code. The
   user has read-only access to it and has to call m0_fol_lookup_rec_fini() once
   it is done with inspecting the record.
 */
struct m0_fol_rec_desc {
	/** Record log sequence number. */
	m0_lsn_t                  rd_lsn;
	struct m0_fol_rec_header  rd_header;
	/** A DTM epoch this update is a part of. */
	struct m0_epoch_id       *rd_epoch;
	/** Identifiers of sibling updates. */
	struct m0_fol_update_ref *rd_sibling;
};

/**
   Record fetched from a fol.

   This structure is returned by m0_fol_rec_lookup() and
   m0_fol_batch(). m0_fol_rec is bound to a particular fol and remembers its
   location in the log.

   The user must call m0_fol_lookup_rec_fini() once it has finished dealing with
   the record.

   There are two liveness and concurrency scopes for a m0_fol_rec, fetched from
   a fol:

   @li long-term: fol code guarantees that a record remains in the fol while its
   reference counter is greater than 0. A record's reference counter is
   manipulated by calls to m0_fol_get() and m0_fol_put(). Initial counter value
   is taken from m0_fol_rec_desc::rd_refcount at the moment of m0_fol_add()
   call. (Note that this means that by the time m0_fol_add() call for a record
   with initially zero reference counter returns, the record might be already
   culled.) Between record addition to the log and its culling the only record
   fields that could change are its reference counter and sibling updates state.

   @li short-term: data copied from the fol and pointed to from the record
   (object references, sibling updates and operation type specific data) are
   valid until m0_fol_lookup_rec_fini() is called. Multiple threads can access
   the record with a given lsn. It's up to them to synchronize access to mutable
   fields (reference counter and sibling updates state).
 */
struct m0_fol_rec {
	struct m0_fol            *fr_fol;
	struct m0_fol_rec_desc    fr_desc;
	/**
	   A list of all FOL record parts in a record.
	   Record parts are linked through m0_fol_rec_part:rp_link to this list.
	 */
	struct m0_tl              fr_parts;
	/** cursor in the underlying data-base, pointing to the record location
	    in the fol. */
#if XXX_USE_DB5
	struct m0_db_cursor       fr_ptr;
	struct m0_db_pair         fr_pair;
#else
	struct m0_be_btree_cursor fr_ptr;
	struct m0_buf             fr_key;
	struct m0_buf             fr_val;
#endif
};

/** Initializes fol record parts list. */
M0_INTERNAL void m0_fol_rec_init(struct m0_fol_rec *rec);

/** Finalizes fol record parts list. */
M0_INTERNAL void m0_fol_rec_fini(struct m0_fol_rec *rec);

/**
   Finds and returns a fol record with a given lsn.

   @pre lsn refers to a record with elevated reference counter.

   @post ergo(result == 0, out->fr_d.rd_lsn == lsn)
   @post ergo(result == 0, out->fr_d.rd_refcount > 0)
   @post ergo(result == 0, m0_fol_rec_invariant(&out->fr_d))
 */
M0_INTERNAL int m0_fol_rec_lookup(struct m0_fol *fol,
#if XXX_USE_DB5
				  struct m0_db_tx *tx,
#endif
				  m0_lsn_t lsn, struct m0_fol_rec *out);

/**
   Finalizes the record, returned by the m0_fol_rec_lookup() or m0_fol_batch()
   and releases all associated resources.
 */
M0_INTERNAL void m0_fol_lookup_rec_fini(struct m0_fol_rec *rec);

M0_INTERNAL bool m0_fol_rec_invariant(const struct m0_fol_rec_desc *drec);

/**
   Adds a reference to a record. The record cannot be culled until its reference
   counter drops to 0. This operation updates the record in the fol.

   @see m0_fol_rec_put()
 */
M0_INTERNAL void m0_fol_rec_get(struct m0_fol_rec *rec);

/**
   Removes a reference to a record.

   When the last reference is removed, the record becomes eligible for
   culling. This operation updates the record in the fol.

   @pre rec->fr_d.rd_refcount > 0

   @see m0_fol_rec_get()
 */
M0_INTERNAL void m0_fol_rec_put(struct m0_fol_rec *rec);

/**
   Returns a batch of no more than nr records, starting with the given
   lsn. Records are placed in "out" array.

   This function returns the number of records fetched. This number is less than
   "nr" if the end of the fol has been reached.

   @pre  @out array contains at least @nr elements.
   @post result <= nr
   @post \forall i >= 0 && i < result, (out[i]->fr_d.rd_lsn >= lsn &&
                                        out[i]->fr_d.rd_refcount > 0)
 */
M0_INTERNAL int m0_fol_batch(struct m0_fol *fol, m0_lsn_t lsn, uint32_t nr,
			     struct m0_fol_rec *out);

M0_INTERNAL int m0_fols_init(void);
M0_INTERNAL void m0_fols_fini(void);

/** Represents a part of FOL record. */
struct m0_fol_rec_part {
	const struct m0_fol_rec_part_ops  *rp_ops;
	/**
	    Pointer to the data where FOL record part is serialised or
	    will be de-serialised.
	 */
	void				  *rp_data;
	/** Linkage into a fol record parts. */
	struct m0_tlink			   rp_link;
	/** Magic for fol record part list. */
	uint64_t			   rp_magic;
	/**
	 * As rp_data points to the in-memory record part during encoding,
	 * rp_data is freed only when rp_flag is equals to M0_XCODE_DECODE.
	 */
	enum m0_xcode_what 		   rp_flag;
};

struct m0_fol_rec_part_type {
	uint32_t                               rpt_index;
	const char                            *rpt_name;
	/**
	    Xcode type representing the FOL record part.
	    Used to encode, decode or calculate the length of
	    FOL record parts using xcode operations.
	 */
	const struct m0_xcode_type	      *rpt_xt;
	const struct m0_fol_rec_part_type_ops *rpt_ops;
};

struct m0_fol_rec_part_type_ops {
	/**  Sets the record part operations vector. */
	void (*rpto_rec_part_init)(struct m0_fol_rec_part *part);
};

/**
    FOL record parts are decoded from FOL record and then undo or
    redo operations are performed on these parts.
 */
struct m0_fol_rec_part_ops {
	const struct m0_fol_rec_part_type *rpo_type;
#if XXX_USE_DB5
	int (*rpo_undo)(struct m0_fol_rec_part *part, struct m0_db_tx *tx);
	int (*rpo_redo)(struct m0_fol_rec_part *part, struct m0_db_tx *tx);
#else
	int (*rpo_undo)(struct m0_fol_rec_part *part, struct m0_be_tx *tx);
	int (*rpo_redo)(struct m0_fol_rec_part *part, struct m0_be_tx *tx);
	void (*rpo_undo_credit)(const struct m0_fol_rec_part *part,
				struct m0_be_tx_credit *accum);
	void (*rpo_redo_credit)(const struct m0_fol_rec_part *part,
				struct m0_be_tx_credit *accum);
#endif
};

struct m0_fol_rec_part_header {
	uint32_t rph_index;
	uint64_t rph_magic;
} M0_XCA_RECORD;

/**
   During encoding of FOL record data points to the in-memory FOL record
   part object allocated by the calling function.
   In case if decoding data should be NULL, as it is allocated by xcode.
   @pre part != NULL
   @pre type != NULL
 */
M0_INTERNAL void
m0_fol_rec_part_init(struct m0_fol_rec_part *part, void *data,
		     const struct m0_fol_rec_part_type *type);

M0_INTERNAL void m0_fol_rec_part_fini(struct m0_fol_rec_part *part);

/** Register a new fol record part type. */
M0_INTERNAL int
m0_fol_rec_part_type_register(struct m0_fol_rec_part_type *type);

M0_INTERNAL void
m0_fol_rec_part_type_deregister(struct m0_fol_rec_part_type *type);

/** Descriptor for the tlist of fol record parts. */
M0_TL_DESCR_DECLARE(m0_rec_part, M0_EXTERN);
M0_TL_DECLARE(m0_rec_part, M0_INTERNAL, struct m0_fol_rec_part);

M0_INTERNAL void m0_fol_rec_part_add(struct m0_fol_rec *rec,
				     struct m0_fol_rec_part *part);

#define M0_FOL_REC_PART_TYPE_DECLARE(part, scope, undo, redo,	   \
				     undo_cred, redo_cred)	   \
scope struct m0_fol_rec_part_type part ## _type;		   \
static const struct m0_fol_rec_part_ops part ## _ops = {           \
	.rpo_type = &part ## _type,		                   \
	.rpo_undo = undo,			                   \
	.rpo_redo = redo,			                   \
	.rpo_undo_credit = undo_cred,			           \
	.rpo_redo_credit = redo_cred,			           \
};						                   \
static void part ## _ops_init(struct m0_fol_rec_part *part)        \
{							           \
	part->rp_ops = &part ## _ops;			           \
}							           \
static const struct m0_fol_rec_part_type_ops part ## _type_ops = { \
	.rpto_rec_part_init = part ##_ops_init			   \
};

#define M0_FOL_REC_PART_TYPE_XC_OPS(name, part_xc, part_type_ops) \
(struct m0_fol_rec_part_type) {					  \
	.rpt_name = name,					  \
	.rpt_xt   = (part_xc),					  \
	.rpt_ops  = (part_type_ops)				  \
};

#define M0_FOL_REC_PART_TYPE_INIT(part, name)		        \
part ## _type = M0_FOL_REC_PART_TYPE_XC_OPS(name, part ## _xc,  \
				            &part ## _type_ops)

/** @} end of fol group */
#endif /* __MERO_FOL_FOL_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
