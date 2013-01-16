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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 09/09/2010
 */

#pragma once

#ifndef __MERO_FOL_FOL_H__
#define __MERO_FOL_FOL_H__

/**
   @defgroup fol File operations log

   File operations log (fol) is a per-node collection of records, describing
   updates to file system state carried out on the node. See HLD (url below) for
   the description of requirements, usage patterns and constraints on fol, as
   well as important terminology (update, operation, etc.).

   A fol is represented by an instance of struct m0_fol. A fol record has two
   data-types associated with it:

   @li m0_fol_rec_desc: a description of a new fol record to be added to a
   fol. This description is used to add a record via call to m0_fol_add();

   @li m0_fol_rec: a record fetched from a fol. m0_fol_rec remembers its
   location it the fol.

   A fol record belongs to a fol type (m0_fol_rec_type) that defines how record
   reacts to the relevant fol state changes.

   @see https://docs.google.com/a/horizontalscale.com/Doc?docid=0Aa9lcGbR4emcZGhxY2hqdmdfNjQ2ZHZocWJ4OWo

   @{
 */

/* export */
struct m0_fol;
struct m0_fol_rec_desc;
struct m0_fol_rec;
struct m0_fol_rec_type;

/* import */
#include "lib/adt.h"      /* m0_buf */
#include "lib/types.h"    /* uint64_t */
#include "lib/arith.h"    /* M0_IS_8ALIGNED */
#include "lib/mutex.h"
#include "lib/vec.h"
#include "fid/fid.h"
#include "dtm/dtm.h"      /* m0_update_id, m0_update_state */
#include "dtm/verno.h"    /* m0_verno */
#include "db/db.h"        /* m0_table, m0_db_cursor */
#include "fol/lsn.h"      /* m0_lsn_t */

struct m0_dbenv;
struct m0_db_tx;
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
	/** Table where fol records are stored. */
	struct m0_table f_table;
	/** Next lsn to use in the fol. */
	m0_lsn_t        f_lsn;
	/** Lock, serializing fol access. */
	struct m0_mutex f_lock;
};

/**
   Initialise in-memory fol structure, creating persistent structures, if
   necessary.

   @post ergo(result == 0, m0_lsn_is_valid(fol->f_lsn))
 */
M0_INTERNAL int m0_fol_init(struct m0_fol *fol, struct m0_dbenv *env);
M0_INTERNAL void m0_fol_fini(struct m0_fol *fol);

/**
   Constructs a in-db representation of a fol record in an allocated buffer.

   This function takes @desc as an input parameter, describing the record to be
   constructed. Representation size is estimated by calling
   m0_fol_rec_type_ops::rto_pack_size(). A buffer is allocated and the record is
   spilled into it. It's up to the caller to free the buffer when necessary.
 */
M0_INTERNAL int m0_fol_rec_pack(struct m0_fol_rec_desc *desc,
				struct m0_buf *buf);

/**
   Reserves and returns lsn.

   @post m0_lsn_is_valid(result);
 */
M0_INTERNAL m0_lsn_t m0_fol_lsn_allocate(struct m0_fol *fol);

/**
   Adds a record to the fol, in the transaction context.

   This function calls m0_fol_rec_pack() internally to pack the record.

   drec->rd_lsn is filled in by m0_fol_add() with lsn assigned to the record.

   drec->rd_refcounter is initial value of record's reference counter. This
   field must be filled by the caller.

   @pre m0_lsn_is_valid(drec->rd_lsn);
   @see m0_fol_add_buf()
 */
M0_INTERNAL int m0_fol_add(struct m0_fol *fol, struct m0_db_tx *tx,
			   struct m0_fol_rec_desc *drec);

/**
   Similar to m0_fol_add(), but with a record already packed into a buffer.

   @pre m0_lsn_is_valid(drec->rd_lsn);
 */
M0_INTERNAL int m0_fol_add_buf(struct m0_fol *fol, struct m0_db_tx *tx,
			       struct m0_fol_rec_desc *drec,
			       struct m0_buf *buf);

/**
   Forces the log.

   On successful return from this call it is guaranteed that all fol records
   with lsn not greater than "upto" are persistent.

   The implementation is free to make other records persistent as it sees fit.
 */
M0_INTERNAL int m0_fol_force(struct m0_fol *fol, m0_lsn_t upto);

/**
   Reference to a file system object from a fol record.

   This reference points to a specific fol record which modified the object in
   question. FOL object references are used in two ways:

   @li they are embedded into a fol record to point to the previous update of
   the same object. Multiple reference might be embedded into a given record
   (depending on the file system operation type), because an operation might
   affect multiple objects;

   @li a reference is embedded into an object index record to point to the
   latest operation applied to the object.

   Together these references specify the complete history of updates of a given
   object. To navigate through this history, one obtains from the object index a
   reference to the latest operation against the object and then follows the
   chain of prevlsn-s.

   An invariant of this system of references is that object versions decrease
   one by one as prevlsn-s for the operations against the object are followed
   and their lsn-s also decrease.
 */
struct m0_fol_obj_ref {
	/** file identifier */
	struct m0_fid   or_fid;
	/** version that the object had before operation has been applied,
	    or {M0_LSN_NONE, 0} if this is the first operation */
	struct m0_verno or_before_ver;
};

/**
   Reference to a sibling update of the same operation.

   @todo More detailed description is to be supplied as part of DTM design.
 */
struct m0_fol_update_ref {
	/* taken from enum m0_update_state  */
	uint32_t             ur_state;
	struct m0_update_id  ur_id;
};

/**
   Fixed part of a fol record.

   @see m0_fol_rec_desc
 */
struct m0_fol_rec_header {
	/** number of outstanding references to the record */
	uint64_t            rh_refcount;
	/** operation code */
	uint32_t            rh_opcode;
	/** number of objects modified by this update */
	uint32_t            rh_obj_nr;
	/** number of sibling updates in the same operation */
	uint32_t            rh_sibling_nr;
	/** number of record parts added to the record */
	uint32_t            rh_parts_nr;
	/** length of the remaining operation type specific data in bytes */
	uint32_t            rh_data_len;
	/**
	    Identifier of this update.

	    @note that the update might be for a different node.
	 */
	struct m0_update_id rh_self;
};

M0_BASSERT(M0_IS_8ALIGNED(sizeof(struct m0_fol_rec_header)));
M0_BASSERT(M0_IS_8ALIGNED(sizeof(struct m0_fol_obj_ref)));
M0_BASSERT(M0_IS_8ALIGNED(sizeof(struct m0_fol_update_ref)));

/**
   In-memory representation of a fol record.

   m0_fol_rec_desc is used in two ways:

   @li as an argument to m0_fol_add(). In this case m0_fol_rec_desc describes a
   new record to be added. All fields, except for rd_lsn, are filled in by a
   user and the user is responsible for the concurrency control and the liveness
   of the structure;

   @li as part of m0_fol_rec returned by m0_fol_rec_lookup() or
   m0_fol_batch(). In this case, m0_fol_rec_desc is filled by the fol code. The
   user has read-only access to it and has to call m0_fol_rec_fini() once it is
   done with inspecting the record.
 */
struct m0_fol_rec_desc {
	/** record log sequence number */
	m0_lsn_t                      rd_lsn;
	struct m0_fol_rec_header      rd_header;
	const struct m0_fol_rec_type *rd_type;
	/** pointer for use by fol record type. */
	void                         *rd_type_private;
	/** references to the objects modified by this update. */
	struct m0_fol_obj_ref        *rd_ref;
	/** a DTM epoch this update is a part of. */
	struct m0_epoch_id           *rd_epoch;
	/** identifiers of sibling updates. */
	struct m0_fol_update_ref     *rd_sibling;
	/** pointer to the remaining operation type specific data. */
	void                         *rd_data;
};

/**
   Record fetched from a fol.

   This structure is returned by m0_fol_rec_lookup() and
   m0_fol_batch(). m0_fol_rec is bound to a particular fol and remembers its
   location in the log.

   The user must call m0_fol_rec_fini() once it has finished dealing with the
   record.

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
   valid until m0_fol_rec_fini() is called. Multiple threads can access the
   record with a given lsn. It's up to them to synchronize access to mutable
   fields (reference counter and sibling updates state).
 */
struct m0_fol_rec {
	struct m0_fol               *fr_fol;
	struct m0_fol_rec_desc       fr_desc;
	/**
	   A list of all FOL record parts in a record.
	   Record parts are linked through m0_fol_rec_part:rp_link to this list.
	 */
	struct m0_tl		     fr_fol_rec_parts;
	/** cursor in the underlying data-base, pointing to the record location
	    in the fol. */
	struct m0_db_cursor          fr_ptr;
	struct m0_db_pair            fr_pair;
};

/**
   Finds and returns a fol record with a given lsn.

   @pre lsn refers to a record with elevated reference counter.

   @post ergo(result == 0, out->fr_d.rd_lsn == lsn)
   @post ergo(result == 0, out->fr_d.rd_refcount > 0)
   @post ergo(result == 0, m0_fol_rec_invariant(&out->fr_d))
 */
M0_INTERNAL int m0_fol_rec_lookup(struct m0_fol *fol, struct m0_db_tx *tx,
				  m0_lsn_t lsn, struct m0_fol_rec *out);
/**
   Finalizes the record, returned by the m0_fol_rec_lookup() or m0_fol_batch()
   and releases all associated resources.
 */
M0_INTERNAL void m0_fol_rec_fini(struct m0_fol_rec *rec);

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

struct m0_fol_rec_type_ops;

/**
   Fol record type.

   There is an instance of struct m0_fol_rec_type for MKDIR, another for WRITE,
   yet another for OPEN, etc.

   Liveness.

   The user is responsible for guaranteeing that a fol record type is not
   unregistered while fol activity is still possible on the node.
 */
struct m0_fol_rec_type {
	/** symbolic type name */
	const char                       *rt_name;
	/** opcode for records of this type */
	uint32_t                          rt_opcode;
	const struct m0_fol_rec_type_ops *rt_ops;
};

/**
   Register a new fol record type.

   @pre m0_fol_rec_type_lookup(rtype->rt_opcode) == NULL
   @post ergo(result == 0, m0_fol_rec_type_lookup(rtype->rt_opcode) == rtype)

   @see m0_fol_rec_type_unregister()
 */
M0_INTERNAL int m0_fol_rec_type_register(const struct m0_fol_rec_type *rtype);

/**
   Dual to m0_fol_rec_type_register().

   @pre m0_fol_rec_type_lookup(rtype->rt_opcode) == rtype
   @post m0_fol_rec_type_lookup(rtype->rt_opcode) == NULL
 */
M0_INTERNAL void m0_fol_rec_type_unregister(const struct m0_fol_rec_type
					    *rtype);

/**
   Finds a record type with a given opcode.

   @post ergo(result != NULL, result->rt_opcode == opcode)
 */
M0_INTERNAL const struct m0_fol_rec_type *m0_fol_rec_type_lookup(uint32_t
								 opcode);

struct m0_fol_rec_type_ops {
	/**
	   Invoked when a transaction containing a record of the type is
	   committed.
	 */
	void (*rto_commit)    (const struct m0_fol_rec_type *type,
			       struct m0_fol *fol, m0_lsn_t lsn);
	/**
	   Invoked when a transaction containing a record of the type is
	   aborted.
	 */
	void (*rto_abort)     (const struct m0_fol_rec_type *type,
			       struct m0_fol *fol, m0_lsn_t lsn);
	/**
	   Invoked when a record of the type becomes persistent.
	 */
	void (*rto_persistent)(const struct m0_fol_rec_type *type,
			       struct m0_fol *fol, m0_lsn_t lsn);
	/**
	   Invoked when a record of the type is culled from a fol.
	 */
	void (*rto_cull)      (const struct m0_fol_rec_type *type,
			       struct m0_db_tx *tx, struct m0_fol_rec *rec);
	/**
	   Parse operation type specific data in desc->rd_data.
	 */
	int  (*rto_open)      (const struct m0_fol_rec_type *type,
			       struct m0_fol_rec_desc *desc);
	/**
	   Release resources associated with a record being finalised.
	 */
	void (*rto_fini)      (struct m0_fol_rec_desc *desc);
	/**
	   Returns number of bytes necessary to store type specific record data.
	 */
	size_t (*rto_pack_size)(struct m0_fol_rec_desc *desc);
	/**
	   Packs type specific record data into a buffer of size returned by
	   ->rto_pack_size().
	 */
	void (*rto_pack)(struct m0_fol_rec_desc *desc, void *buf);
};

M0_INTERNAL int m0_fols_init(void);
M0_INTERNAL void m0_fols_fini(void);

/**
 *   Composes the FOL record by iterating through FOL parts from the list in m0_dtx
 *   added during updates on server and adds it in the database.
 */
M0_INTERNAL int m0_fol_record_add(struct m0_fol *fol, struct m0_dtx *dtx);

M0_INTERNAL int m0_fol_record_lookup(struct m0_fol *fol, struct m0_db_tx *tx,
				     m0_lsn_t lsn, struct m0_fol_rec *out);

M0_INTERNAL struct m0_fol_rec *m0_fol_record_init(void);
M0_INTERNAL void m0_fol_record_fini(struct m0_fol_rec *rec);

/** It represents updates made as part of executing FOM on server. */
struct m0_fol_rec_part {
	const struct m0_fol_rec_part_ops  *rp_ops;
	/** Pointer to the data where FOL record part is serialised or
	    will be de-serialised.
	 */
	void				  *rp_data;
	/** Linkage into a fol record parts. */
	struct m0_tlink			   rp_link;
	/** Magic for fol record part list. */
	uint64_t			   rp_magic;
};

/** FOL record part types are registered in a global array of FOL record
    part types using m0_fol_rec_part_type::rpt_index.

   m0_fol_rec_part_type::rpt_index should also be encoded for each FOL
   record part, so that decoding of FOL record parts from FOL record
   can be done using it.
 */

struct m0_fol_rec_part_type {
	uint32_t                               rpt_index;
	const char                            *rpt_name;
	/** Xcode type representing the FOL record part.
	    Used to encode, decode or calculate the length of
	    FOL record parts using xcode operations.
	 */
	const struct m0_xcode_type	      *rpt_xt;
	const struct m0_fol_rec_part_type_ops *rpt_ops;
};

struct m0_fol_rec_part_type_ops {
	/**  Sets the record part opeartions vector. */
	void (*rpto_rec_part_init)(struct m0_fol_rec_part *part);
};

/** FOL part records are decoded from FOL record and undo or
    redo operations are done using them.
 */
struct m0_fol_rec_part_ops {
	const struct m0_fol_rec_part_type *rpo_type;
	int (*rpo_undo)(struct m0_fol_rec_part *part);
	int (*rpo_redo)(struct m0_fol_rec_part *part);
};

/**
 * FOL record part is allocated and ops is assigned to it.
 * FOL record part data rp_data is allocated based on part type.
 * @pre type != NULL
 */
M0_INTERNAL struct m0_fol_rec_part *m0_fol_rec_part_init(
		const struct m0_fol_rec_part_type *type);

M0_INTERNAL void m0_fol_rec_part_fini(struct m0_fol_rec_part *part);

/** FOL record part type is initialized with the given xcode type xt. */
M0_INTERNAL int m0_fol_rec_part_type_init(struct m0_fol_rec_part_type *type,
					  const char *name,
					  const struct m0_xcode_type *xt,
					  const struct m0_fol_rec_part_type_ops *ops);

M0_INTERNAL void m0_fol_rec_part_type_fini(struct m0_fol_rec_part_type *type);

M0_INTERNAL m0_bcount_t m0_fol_rec_part_data_size(struct m0_fol_rec_part *part);

/** Based on xcode type of the FOL record part, encoding or decoding is done. */
M0_INTERNAL int m0_fol_rec_part_encdec(struct m0_fol_rec_part  *part,
			               struct m0_bufvec_cursor *cur,
			               enum m0_bufvec_what      what);

/** Descriptor for the tlist of fol record parts. */
M0_TL_DESCR_DECLARE(m0_rec_part, M0_EXTERN);
M0_TL_DECLARE(m0_rec_part, M0_INTERNAL, struct m0_fol_rec_part);

#define M0_FOL_REC_PART_XCODE_OBJ(r) (struct m0_xcode_obj) {	\
		.xo_type = r->rp_ops->rpo_type->rpt_xt,		\
		.xo_ptr  = r->rp_data,		                \
}

/** @} end of fol group */

/* __MERO_FOL_FOL_H__ */
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
