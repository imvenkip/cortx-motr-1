/* -*- C -*- */

#ifndef __COLIBRI_FOL_FOL_H__
#define __COLIBRI_FOL_FOL_H__

/**
   @defgroup fol File operations log

   File operations log (fol) is a per-node collection of records, describing
   updates to file system state carried out on the node. See HLD (url below) for
   the description of requirements, usage patterns and constraints on fol, as
   well as important terminology (update, operation, etc.).

   A fol is represented by an instance of struct c2_fol. A fol record is has two
   data-types associated with it:

   @li c2_fol_rec_desc: a description of a new fol record to be added to a
   fol. This description is used to add a record via call to c2_fol_add();

   @li c2_fol_rec: a record fetched from a fol. c2_fol_rec contains pointers to
   the in-fol data and remembers its location it the fol.

   @see https://docs.google.com/a/horizontalscale.com/Doc?docid=0Aa9lcGbR4emcZGhxY2hqdmdfNjQ2ZHZocWJ4OWo

   @{
 */

/* export */
struct c2_fol;
struct c2_fol_rec_desc;
struct c2_fol_rec;

/* import */
#include "lib/types.h"    /* uint64_t */
#include "lib/mutex.h"
#include "fid/fid.h"
#include "dtm/dtm.h"      /* c2_update_id, c2_update_state */
#include "db/db.h"        /* c2_table, c2_db_cursor */

struct c2_dbenv;
struct c2_db_tx;
struct c2_epoch_id;

/**
   Log sequence number (lsn) uniquely identifies a record in a fol.

   lsn possesses two properties:

   @li a record with a given lsn can be found efficiently, and

   @li lsn of a dependent update is greater than the lsn of an update it depends
   upon.

   lsn should _never_ overflow, because other persistent file system tables
   (most notably object index) store lsns of arbitrarily old records, possibly
   long time truncated from the fol. It would be dangerous to allow such a
   reference to accidentally alias an unrelated record after lsn overflow. Are
   64 bits enough?

   Given 1M operations per second, a 64 bit counter overflows in 600000 years.
 */
typedef uint64_t c2_lsn_t;

enum {
	/** Invalid lsn value. Used to catch uninitialised lsns. */
	C2_LSN_INVALID,
	/** Non-existent lsn. This is used, for example, as a prevlsn, when
	    there is no previous operation on the object. */
	C2_LSN_NONE,
	C2_LSN_RESERVED_NR
};

/** True iff the argument might be an lsn of an existing fol record. */
bool     c2_lsn_is_valid(c2_lsn_t lsn);
/** 3-way comparison (-1, 0, +1) of lsns, compatible with record
    dependencies. */
int      c2_lsn_cmp     (c2_lsn_t lsn0, c2_lsn_t lsn1);
/** Returns an lsn larger than given one. */
c2_lsn_t c2_lsn_inc     (c2_lsn_t lsn);

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
	/** Largest lsn in the fol. */
	c2_lsn_t        f_lsn;
	/** Lock, serializing fol access. */
	struct c2_mutex f_lock;
};

int  c2_fol_init(struct c2_fol *fol, struct c2_dbenv *env);
void c2_fol_fini(struct c2_fol *fol);

/**
   Adds a record to the fol, in the transaction context.

   @param drec - describes the record to be added. drec->rd_lsn is filled in by
   c2_fol_add() with lsn assigned to the record. drec->rd_refcounter is initial
   value of record's reference counter.
 */
int c2_fol_add(struct c2_fol *fol, struct c2_db_tx *tx, 
	       struct c2_fol_rec_desc *drec);

/**
   Forces the log.

   On successful return from this call it is guaranteed that all fol records
   with lsn not greater than "upto" are persistent.

   The implementation is free to make other records persistent as it sees fit.
 */
int c2_fol_force(struct c2_fol *fol, c2_lsn_t upto);

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
	struct c2_fid or_fid;
	/** version that the file had before operation has been applied */
	uint64_t      or_version;
	/** lsn of a record for the previous operation modifying the file, or
	    C2_LSN_NONE, if this is the first operation. */
	c2_lsn_t      or_prevlsn;
};

/**
   Reference to a sibling update of the same operation.

   @todo More detailed description is to be supplies as part of DTM design.
 */
struct c2_fol_update_ref {
	struct c2_update_id  ur_id;
	enum c2_update_state ur_state;
};

struct c2_fol_rec_ops;

/**
   In-memory representation of a fol record.

   c2_fol_rec_desc is used in two ways:

   @li as an argument to c2_fol_add(). In this case c2_fol_rec_desc describes a
   new record to be added. All fields are filled in by a user and the user is
   responsible for the concurrency control and the liveness of the structure;

   @li as part of c2_fol_rec returned by c2_fol_rec_lookup() or
   c2_fol_batch(). In this case, c2_fol_rec_desc is filled by the fol code. The
   user has read-only access to it and has to call c2_fol_rec_fini() once it is
   done with inspecting the record.
 */
struct c2_fol_rec_desc {
	/** record log sequence number */
	c2_lsn_t                     rd_lsn;
	/** number of outstanding references to the record */
	uint64_t                     rd_refcount;
	/** operation code */
	uint32_t                     rd_opcode;

	/** number of objects modified by this update */
	uint32_t                     rd_obj_nr;
	/** references to the objects modified by this update. This points to
	    in-fol data.*/
	struct c2_fol_obj_ref       *rd_ref;

	/** a DTM epoch this update is a part of. This points to in-fol data. */
	struct c2_epoch_id          *rd_epoch;
	/** 
	    Identifier of this update. This points to in-fol data.

	    @note that the update might be for a different node.
	 */
	struct c2_update_id         *rd_self;
	/** number of sibling updates in the same operation */
	uint32_t                     rd_sibling_nr;
	/** identifiers of sibling updates. This points to in-fol data. */
	struct c2_fol_update_ref    *rd_sibling;

	/** length or the remaining operation type specific data in bytes */
	uint32_t                     rd_data_len;
	/** pointer to the remaining operation type specific data. This points
	    to in-fol data. */
	void                        *rd_data;
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

   @li short-term: in-fol data pointed to from the record (object references,
   sibling updates and operation type specific data) are valid until
   c2_fol_rec_fini() is called. Multiple threads can access the record with a
   given lsn. It's up to them to synchronize access to mutable fields (reference
   counter and sibling updates state).
 */
struct c2_fol_rec {
	struct c2_fol               *fr_fol;
	struct c2_fol_rec_desc       fr_d;
	const struct c2_fol_rec_ops *fr_ops;
	/** cursor in the underlying data-base, pointing to the record location
	    in the fol. */
	struct c2_db_cursor          fr_ptr;
};

struct c2_fol_rec_ops {
};

/**
   Finds and returns a fol record with a given lsn.

   @pre lsn refers to a record with elevated reference counter.

   @post ergo(result == 0, out->fr_d.rd_lsn == lsn)
   @post ergo(result == 0, out->fr_d.rd_refcount > 0)
 */
int  c2_fol_rec_lookup(struct c2_fol *fol, struct c2_db_tx *tx, c2_lsn_t lsn, 
		       struct c2_fol_rec *out);
/**
   Finalizes the record, returned by the c2_fol_rec_lookup() or c2_fol_batch()
   and releases all associated resources.
 */
void c2_fol_rec_fini  (struct c2_fol_rec *rec);

bool c2_fol_rec_invariant(const struct c2_fol_rec_desc *drec);

/**
   Adds a reference to a record. The record cannot be culled until its reference
   counter drops to 0.

   @see c2_fol_rec_put()
 */
void c2_fol_rec_get(struct c2_fol_rec *rec);

/**
   Removes a reference to a record.

   When the last reference is removed, the record becomes eligible for culling.

   @pre rec->fr_d.rd_refcount > 0

   @see c2_fol_rec_get()
 */
void c2_fol_rec_put(struct c2_fol_rec *rec);

/**
   Returns a batch of no more than nr records, starting with the given
   lsn. Records are placed in "out" array.

   This function returns the number of records fetched. This number is less than
   "nr" if the end of the fol has been reached.

   @post result <= nr
   @post \forall i >= 0 && i < result, (out[i]->fr_d.rd_lsn >= lsn && 
                                        out[i]->fr_d.rd_refcount > 0)
 */
int c2_fol_batch(struct c2_fol *fol, c2_lsn_t lsn, uint32_t nr, 
		 struct c2_fol_rec *out);

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
