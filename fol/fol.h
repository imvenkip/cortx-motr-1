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

#ifndef __COLIBRI_FOL_FOL_H__
#define __COLIBRI_FOL_FOL_H__

/**
   @defgroup fol File operations log

   File operations log (fol) is a per-node collection of records, describing
   updates to file system state carried out on the node. See HLD (url below) for
   the description of requirements, usage patterns and constraints on fol, as
   well as important terminology (update, operation, etc.).

   A fol is represented by an instance of struct c2_fol. A fol record has two
   data-types associated with it:

   @li c2_fol_rec_desc: a description of a new fol record to be added to a
   fol. This description is used to add a record via call to c2_fol_add();

   @li c2_fol_rec: a record fetched from a fol. c2_fol_rec remembers its
   location it the fol.

   A fol record belongs to a fol type (c2_fol_rec_type) that defines how record
   reacts to the relevant fol state changes.

   @see https://docs.google.com/a/horizontalscale.com/Doc?docid=0Aa9lcGbR4emcZGhxY2hqdmdfNjQ2ZHZocWJ4OWo

   @{
 */

/* export */
struct c2_fol;
struct c2_fol_rec_desc;
struct c2_fol_rec;
struct c2_fol_rec_type;

/* import */
#include "lib/adt.h"      /* c2_buf */
#include "lib/types.h"    /* uint64_t */
#include "lib/arith.h"    /* C2_IS_8ALIGNED */
#include "lib/mutex.h"
#include "fid/fid.h"
#include "dtm/dtm.h"      /* c2_update_id, c2_update_state */
#include "dtm/verno.h"    /* c2_verno */
#include "db/db.h"        /* c2_table, c2_db_cursor */
#include "fol/lsn.h"      /* c2_lsn_t */

struct c2_dbenv;
struct c2_db_tx;
struct c2_epoch_id;

/**
   In-memory representation of a fol.

   <b>Liveness rules and concurrency control.</b>

   c2_fol liveness is managed by the user (the structure is not reference
   counted) which is responsible for guaranteeing that c2_fol_fini() is the last
   call against a given instance.

   FOL code manages concurrency internally: multiple threads can call c2_fol_*()
   functions against the same fol (except for c2_fol_init() and c2_fol_fini()).
 */
struct c2_fol {
	/** Table where fol records are stored. */
	struct c2_table f_table;
	/** Next lsn to use in the fol. */
	c2_lsn_t        f_lsn;
	/** Lock, serializing fol access. */
	struct c2_mutex f_lock;
};

/**
   Initialise in-memory fol structure, creating persistent structures, if
   necessary.

   @post ergo(result == 0, c2_lsn_is_valid(fol->f_lsn))
 */
C2_INTERNAL int c2_fol_init(struct c2_fol *fol, struct c2_dbenv *env);
C2_INTERNAL void c2_fol_fini(struct c2_fol *fol);

/**
   Constructs a in-db representation of a fol record in an allocated buffer.

   This function takes @desc as an input parameter, describing the record to be
   constructed. Representation size is estimated by calling
   c2_fol_rec_type_ops::rto_pack_size(). A buffer is allocated and the record is
   spilled into it. It's up to the caller to free the buffer when necessary.
 */
C2_INTERNAL int c2_fol_rec_pack(struct c2_fol_rec_desc *desc,
				struct c2_buf *buf);

/**
   Reserves and returns lsn.

   @post c2_lsn_is_valid(result);
 */
C2_INTERNAL c2_lsn_t c2_fol_lsn_allocate(struct c2_fol *fol);

/**
   Adds a record to the fol, in the transaction context.

   This function calls c2_fol_rec_pack() internally to pack the record.

   drec->rd_lsn is filled in by c2_fol_add() with lsn assigned to the record.

   drec->rd_refcounter is initial value of record's reference counter. This
   field must be filled by the caller.

   @pre c2_lsn_is_valid(drec->rd_lsn);
   @see c2_fol_add_buf()
 */
C2_INTERNAL int c2_fol_add(struct c2_fol *fol, struct c2_db_tx *tx,
			   struct c2_fol_rec_desc *drec);

/**
   Similar to c2_fol_add(), but with a record already packed into a buffer.

   @pre c2_lsn_is_valid(drec->rd_lsn);
 */
C2_INTERNAL int c2_fol_add_buf(struct c2_fol *fol, struct c2_db_tx *tx,
			       struct c2_fol_rec_desc *drec,
			       struct c2_buf *buf);

/**
   Forces the log.

   On successful return from this call it is guaranteed that all fol records
   with lsn not greater than "upto" are persistent.

   The implementation is free to make other records persistent as it sees fit.
 */
C2_INTERNAL int c2_fol_force(struct c2_fol *fol, c2_lsn_t upto);

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
struct c2_fol_obj_ref {
	/** file identifier */
	struct c2_fid   or_fid;
	/** version that the object had before operation has been applied,
	    or {C2_LSN_NONE, 0} if this is the first operation */
	struct c2_verno or_before_ver;
};

/**
   Reference to a sibling update of the same operation.

   @todo More detailed description is to be supplied as part of DTM design.
 */
struct c2_fol_update_ref {
	/* taken from enum c2_update_state  */
	uint32_t             ur_state;
	struct c2_update_id  ur_id;
};

/**
   Fixed part of a fol record.

   @see c2_fol_rec_desc
 */
struct c2_fol_rec_header {
	/** number of outstanding references to the record */
	uint64_t            rh_refcount;
	/** operation code */
	uint32_t            rh_opcode;
	/** number of objects modified by this update */
	uint32_t            rh_obj_nr;
	/** number of sibling updates in the same operation */
	uint32_t            rh_sibling_nr;
	/** length of the remaining operation type specific data in bytes */
	uint32_t            rh_data_len;
	/**
	    Identifier of this update.

	    @note that the update might be for a different node.
	 */
	struct c2_update_id rh_self;
};

C2_BASSERT(C2_IS_8ALIGNED(sizeof(struct c2_fol_rec_header)));
C2_BASSERT(C2_IS_8ALIGNED(sizeof(struct c2_fol_obj_ref)));
C2_BASSERT(C2_IS_8ALIGNED(sizeof(struct c2_fol_update_ref)));

/**
   In-memory representation of a fol record.

   c2_fol_rec_desc is used in two ways:

   @li as an argument to c2_fol_add(). In this case c2_fol_rec_desc describes a
   new record to be added. All fields, except for rd_lsn, are filled in by a
   user and the user is responsible for the concurrency control and the liveness
   of the structure;

   @li as part of c2_fol_rec returned by c2_fol_rec_lookup() or
   c2_fol_batch(). In this case, c2_fol_rec_desc is filled by the fol code. The
   user has read-only access to it and has to call c2_fol_rec_fini() once it is
   done with inspecting the record.
 */
struct c2_fol_rec_desc {
	/** record log sequence number */
	c2_lsn_t                      rd_lsn;
	struct c2_fol_rec_header      rd_header;
	const struct c2_fol_rec_type *rd_type;
	/** pointer for use by fol record type. */
	void                         *rd_type_private;
	/** references to the objects modified by this update. */
	struct c2_fol_obj_ref        *rd_ref;
	/** a DTM epoch this update is a part of. */
	struct c2_epoch_id           *rd_epoch;
	/** identifiers of sibling updates. */
	struct c2_fol_update_ref     *rd_sibling;
	/** pointer to the remaining operation type specific data. */
	void                         *rd_data;
};

/**
   Record fetched from a fol.

   This structure is returned by c2_fol_rec_lookup() and
   c2_fol_batch(). c2_fol_rec is bound to a particular fol and remembers its
   location in the log.

   The user must call c2_fol_rec_fini() once it has finished dealing with the
   record.

   There are two liveness and concurrency scopes for a c2_fol_rec, fetched from
   a fol:

   @li long-term: fol code guarantees that a record remains in the fol while its
   reference counter is greater than 0. A record's reference counter is
   manipulated by calls to c2_fol_get() and c2_fol_put(). Initial counter value
   is taken from c2_fol_rec_desc::rd_refcount at the moment of c2_fol_add()
   call. (Note that this means that by the time c2_fol_add() call for a record
   with initially zero reference counter returns, the record might be already
   culled.) Between record addition to the log and its culling the only record
   fields that could change are its reference counter and sibling updates state.

   @li short-term: data copied from the fol and pointed to from the record
   (object references, sibling updates and operation type specific data) are
   valid until c2_fol_rec_fini() is called. Multiple threads can access the
   record with a given lsn. It's up to them to synchronize access to mutable
   fields (reference counter and sibling updates state).
 */
struct c2_fol_rec {
	struct c2_fol               *fr_fol;
	struct c2_fol_rec_desc       fr_desc;
	/** cursor in the underlying data-base, pointing to the record location
	    in the fol. */
	struct c2_db_cursor          fr_ptr;
	struct c2_db_pair            fr_pair;
};

/**
   Finds and returns a fol record with a given lsn.

   @pre lsn refers to a record with elevated reference counter.

   @post ergo(result == 0, out->fr_d.rd_lsn == lsn)
   @post ergo(result == 0, out->fr_d.rd_refcount > 0)
   @post ergo(result == 0, c2_fol_rec_invariant(&out->fr_d))
 */
C2_INTERNAL int c2_fol_rec_lookup(struct c2_fol *fol, struct c2_db_tx *tx,
				  c2_lsn_t lsn, struct c2_fol_rec *out);
/**
   Finalizes the record, returned by the c2_fol_rec_lookup() or c2_fol_batch()
   and releases all associated resources.
 */
C2_INTERNAL void c2_fol_rec_fini(struct c2_fol_rec *rec);

C2_INTERNAL bool c2_fol_rec_invariant(const struct c2_fol_rec_desc *drec);

/**
   Adds a reference to a record. The record cannot be culled until its reference
   counter drops to 0. This operation updates the record in the fol.

   @see c2_fol_rec_put()
 */
C2_INTERNAL void c2_fol_rec_get(struct c2_fol_rec *rec);

/**
   Removes a reference to a record.

   When the last reference is removed, the record becomes eligible for
   culling. This operation updates the record in the fol.

   @pre rec->fr_d.rd_refcount > 0

   @see c2_fol_rec_get()
 */
C2_INTERNAL void c2_fol_rec_put(struct c2_fol_rec *rec);

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
C2_INTERNAL int c2_fol_batch(struct c2_fol *fol, c2_lsn_t lsn, uint32_t nr,
			     struct c2_fol_rec *out);

struct c2_fol_rec_type_ops;

/**
   Fol record type.

   There is an instance of struct c2_fol_rec_type for MKDIR, another for WRITE,
   yet another for OPEN, etc.

   Liveness.

   The user is responsible for guaranteeing that a fol record type is not
   unregistered while fol activity is still possible on the node.
 */
struct c2_fol_rec_type {
	/** symbolic type name */
	const char                       *rt_name;
	/** opcode for records of this type */
	uint32_t                          rt_opcode;
	const struct c2_fol_rec_type_ops *rt_ops;
};

/**
   Register a new fol record type.

   @pre c2_fol_rec_type_lookup(rtype->rt_opcode) == NULL
   @post ergo(result == 0, c2_fol_rec_type_lookup(rtype->rt_opcode) == rtype)

   @see c2_fol_rec_type_unregister()
 */
C2_INTERNAL int c2_fol_rec_type_register(const struct c2_fol_rec_type *rtype);

/**
   Dual to c2_fol_rec_type_register().

   @pre c2_fol_rec_type_lookup(rtype->rt_opcode) == rtype
   @post c2_fol_rec_type_lookup(rtype->rt_opcode) == NULL
 */
C2_INTERNAL void c2_fol_rec_type_unregister(const struct c2_fol_rec_type
					    *rtype);

/**
   Finds a record type with a given opcode.

   @post ergo(result != NULL, result->rt_opcode == opcode)
 */
C2_INTERNAL const struct c2_fol_rec_type *c2_fol_rec_type_lookup(uint32_t
								 opcode);

struct c2_fol_rec_type_ops {
	/**
	   Invoked when a transaction containing a record of the type is
	   committed.
	 */
	void (*rto_commit)    (const struct c2_fol_rec_type *type,
			       struct c2_fol *fol, c2_lsn_t lsn);
	/**
	   Invoked when a transaction containing a record of the type is
	   aborted.
	 */
	void (*rto_abort)     (const struct c2_fol_rec_type *type,
			       struct c2_fol *fol, c2_lsn_t lsn);
	/**
	   Invoked when a record of the type becomes persistent.
	 */
	void (*rto_persistent)(const struct c2_fol_rec_type *type,
			       struct c2_fol *fol, c2_lsn_t lsn);
	/**
	   Invoked when a record of the type is culled from a fol.
	 */
	void (*rto_cull)      (const struct c2_fol_rec_type *type,
			       struct c2_db_tx *tx, struct c2_fol_rec *rec);
	/**
	   Parse operation type specific data in desc->rd_data.
	 */
	int  (*rto_open)      (const struct c2_fol_rec_type *type,
			       struct c2_fol_rec_desc *desc);
	/**
	   Release resources associated with a record being finalised.
	 */
	void (*rto_fini)      (struct c2_fol_rec_desc *desc);
	/**
	   Returns number of bytes necessary to store type specific record data.
	 */
	size_t (*rto_pack_size)(struct c2_fol_rec_desc *desc);
	/**
	   Packs type specific record data into a buffer of size returned by
	   ->rto_pack_size().
	 */
	void (*rto_pack)(struct c2_fol_rec_desc *desc, void *buf);
};

C2_INTERNAL int c2_fols_init(void);
C2_INTERNAL void c2_fols_fini(void);

/** @} end of fol group */

/* __COLIBRI_FOL_FOL_H__ */
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
