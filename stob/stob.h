/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 04/28/2010
 */

#ifndef __COLIBRI_STOB_STOB_H__
#define __COLIBRI_STOB_STOB_H__

#include "lib/atomic.h"
#include "lib/types.h"         /* c2_uint128 */
#include "lib/cdefs.h"
#include "lib/vec.h"
#include "lib/chan.h"
#include "lib/rwlock.h"
#include "lib/tlist.h"
#include "addb/addb.h"
#include "sm/sm.h"

#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

/* import */
struct c2_dtx;
struct c2_chan;
struct c2_indexvec;
struct c2_io_scope;

struct c2_db_tx;

/**
   @defgroup stob Storage objects

   Storage object is a fundamental abstraction of C2. Storage objects offer a
   linear address space for data and may have redundancy and may have integrity
   data.

   There are multiple types of storage objects, used for various purposes and
   providing various extensions of the basic storage object interface described
   below. Specifically, containers for data and meta-data are implemented as
   special types of storage objects.

   @see stoblinux
   @{
 */

struct c2_stob;
struct c2_stob_id;
struct c2_stob_op;
struct c2_stob_io;
struct c2_stob_type;
struct c2_stob_type_op;
struct c2_stob_domain;
struct c2_stob_domain_op;

struct c2_stob_type {
	const struct c2_stob_type_op *st_op;
	const char                   *st_name;
	const uint32_t                st_magic;
	struct c2_tl		      st_domains; /**< list of domains */
	struct c2_addb_ctx            st_addb;
};

struct c2_stob_type_op {
	int  (*sto_init)(struct c2_stob_type *stype);
	void (*sto_fini)(struct c2_stob_type *stype);
	/**
	   Locates a storage objects domain with a specified name, creating it
	   if none exists.

	   @return 0 success, any other value means error.
	*/
	int (*sto_domain_locate)(struct c2_stob_type *type,
				 const char *domain_name,
				 struct c2_stob_domain **dom);
};

int  c2_stob_type_init(struct c2_stob_type *kind);
void c2_stob_type_fini(struct c2_stob_type *kind);

/**
   stob domain

   Stob domain is a collection of storage objects of the same type.
   A stob type may have multiple domains, which are linked together to its
   type by 'sd_domain_linkage'.

   A storage domain comes with an operation to find a storage object in the
   domain. A domain might cache storage objects and use some kind of index to
   speed up object lookup. This caching and indexing are not visible at the
   generic level.
*/
struct c2_stob_domain {
	const char 		       *sd_name;
	const struct c2_stob_domain_op *sd_ops;
	struct c2_stob_type            *sd_type;
	struct c2_tlink                 sd_domain_linkage;
	struct c2_rwlock                sd_guard;
	struct c2_addb_ctx              sd_addb;
	uint64_t                        sd_magic;
};

/**
   domain operations vector
 */
struct c2_stob_domain_op {
	/**
	   Cleanup this domain: e.g. delete itself from the domain list in type.
	*/
	void (*sdo_fini)(struct c2_stob_domain *self);
	/**
	   Returns an in-memory representation for the storage object with given
	   identifier in this domain, either by creating a new c2_stob or
	   returning already existing one.

	   Returned object can be in any c2_stob_state state.

	   @pre id is from a part of identifier name-space assigned to dom.
	 */
	int (*sdo_stob_find)(struct c2_stob_domain *dom,
			     const struct c2_stob_id *id, struct c2_stob **out);
	/**
	   Furnish a transactional context for this domain.

	   @todo this is a temporary method, until proper DTM interfaces are in
	   place.
	 */
	int (*sdo_tx_make)(struct c2_stob_domain *dom, struct c2_dtx *tx);
};

void c2_stob_domain_init(struct c2_stob_domain *dom, struct c2_stob_type *t);
void c2_stob_domain_fini(struct c2_stob_domain *dom);

/**
   c2_stob state specifying its relationship with the underlying storage object.
 */
enum c2_stob_state {
	/**
	   The state or existence of the underlying storage object are not
	   known. c2_stob can be used as a placeholder in storage object
	   identifiers name-space in this state.
	 */
	CSS_UNKNOWN,
	/**
	   The underlying storage object is known to exist.
	 */
	CSS_EXISTS,
	/**
	   The underlying storage object is known to not exist.
	 */
	CSS_NOENT
};

/**
   Unique storage object identifier.

   A storage object in a cluster is identified by identifier of this type.
 */
struct c2_stob_id {
	struct c2_uint128 si_bits;
};

bool c2_stob_id_eq (const struct c2_stob_id *id0, const struct c2_stob_id *id1);
int  c2_stob_id_cmp(const struct c2_stob_id *id0, const struct c2_stob_id *id1);
bool c2_stob_id_is_set(const struct c2_stob_id *id);

/**
   In-memory representation of a storage object.

   c2_stob is created by a call to one of the c2_stob_create(), c2_stob_locate()
   or c2_stob_get() functions that in turn call corresponding c2_stob_domain_op
   method.

   c2_stob serves multiple purposes:

   @li it acts as a placeholder in storage object identifiers name-space. For
   example, locks can be taken on it;

   @li it acts as a handle for the underlying storage object. IO and meta-data
   operations can be directed to the storage object by calling functions on
   c2_stob;

   @li it caches certain storage object attributes in memory.

   Accordingly, c2_stob can be in one of the states described by enum
   c2_stob_state. Compare these c2_stob roles with the socket interface (bind,
   connect, etc.)
 */
struct c2_stob {
	struct c2_atomic64       so_ref;
	enum c2_stob_state       so_state;
	const struct c2_stob_op *so_op;
	struct c2_stob_id	 so_id;      /**< unique id of this object */
	struct c2_stob_domain 	*so_domain;  /**< its domain */
	struct c2_addb_ctx       so_addb;
};

struct c2_stob_op {
	/**
	   Called when the last reference on the object is released.

	   This method is called under exclusive mode c2_stob_domain::sd_guard
	   lock.

	   An implementation is free to either destroy the c2_stob immediately
	   or cache it in some internal data-structure.
	 */
	void (*sop_fini)(struct c2_stob *stob);
	/**
	  Create an object.

	  Create the storage object for this c2_stob.

	  @return 0 success, other values mean error.
	  @post ergo(result == 0, stob->so_state == CSS_EXISTS)
	*/
	int (*sop_create)(struct c2_stob *stob, struct c2_dtx *tx);

	/**
	   Locate a storage object for this c2_stob.

	   @return 0 success, other values mean error
	   @post ergo(result == 0, stob->so_state == CSS_EXISTS)
	   @post ergo(result == -ENOENT, stob->so_state == CSS_NOENT)
	*/
	int (*sop_locate)(struct c2_stob *obj, struct c2_dtx *tx);

	/**
	   Initialises IO operation structure, preparing it to be queued for a
	   given storage object.

	   This is called when IO operation structure is used to queue IO for
	   the first time or when the last time is queued IO for a different
	   type of storage object.

	   @pre stob->so_state == CSS_EXISTS
	   @pre io->si_state == SIS_IDLE

	   @see c2_stob_io::si_stob_magic
	   @see c2_stob_io::si_stob_private
	 */
	int  (*sop_io_init)(struct c2_stob *stob, struct c2_stob_io *io);

	/**
	   Takes an implementation specific lock serialising state transitions
	   for all operations (at least) against the given object.

	   This lock is used internally by the generic adieu code.

	   @pre !stob->so_op.sop_io_is_locked(stob)
	   @post stob->so_op.sop_io_is_locked(stob)
	 */
	void (*sop_io_lock)(struct c2_stob *stob);
	/**
	   Releases an implementation specific lock taken by
	   c2_stob_op::sop_io_lock().

	   @pre   stob->so_op.sop_io_is_locked(stob)
	   @post !stob->so_op.sop_io_is_locked(stob)
	 */
	void (*sop_io_unlock)(struct c2_stob *stob);
	/**
	   Returns true iff the caller hold the lock taken by
	   c2_stob_op::sop_io_lock().

	   This call is used only by assertions.
	 */
	bool (*sop_io_is_locked)(const struct c2_stob *stob);
	/**
	   IO alignment and granularity.

	   This method returns a power of two, which determines alignment
	   required for the user buffers of stob IO requests against this object
	   and IO granularity.

	   Note that this is not "optimal IO size"---alignment is a requirement
	   rather than hint.

	   Block sizes are needed for the following reasons:

	   @li to insulate stob IO layer from read-modify-write details;

	   @li to allow IO to the portions of objects inaccessible through the
	   flat 64-bit byte-granularity name-space.

	   @note the scheme is very simplistic, enforcing the same unit of
	   alignment and granularity. Sophistication could be added as
	   necessary.
	 */
	uint32_t (*sop_block_shift)(const struct c2_stob *stob);
};

/**
   Returns an in-memory representation for a stob with a given identifier,
   creating the former if necessary.

   Resulting c2_stob can be in any state. c2_stob_find() neither fetches the
   object attributes from the storage nor checks for object's existence. This
   function is used to create a placeholder on which other functions
   (c2_stob_locate(), c2_stob_create(), locking functions, etc.) can be called.

   On success, this function acquires a reference on the returned object.
 */
int  c2_stob_find  (struct c2_stob_domain *dom, const struct c2_stob_id *id,
		    struct c2_stob **out);
/**
   Initialise generic c2_stob fields.

   @post obj->so_state == CSS_UNKNOWN
 */
void c2_stob_init  (struct c2_stob *obj, const struct c2_stob_id *id,
		    struct c2_stob_domain *dom);
void c2_stob_fini  (struct c2_stob *obj);

/**
   Locate a storage object for this c2_stob.

   @return 0 success, other values mean error
   @post ergo(result == 0, stob->so_state == CSS_EXISTS)
   @post ergo(result == -ENOENT, stob->so_state == CSS_NOENT)
 */
int  c2_stob_locate(struct c2_stob *obj, struct c2_dtx *tx);

/**
   Create an object.

   Create the storage object for this c2_stob.

   @return 0 success, other values mean error.
   @post ergo(result == 0, stob->so_state == CSS_EXISTS)
 */
int  c2_stob_create(struct c2_stob *obj, struct c2_dtx *tx);

/**
   Acquires an additional reference on the object.

   @see c2_stob_put()
   @see c2_stob_find()
 */
void c2_stob_get(struct c2_stob *obj);

/**
   Releases a reference on the object.

   When the last reference is released, the object can either return to the
   cache or can be immediately destroyed at the storage object type
   discretion. If object is cached, its state (c2_stob::so_state) can change at
   any moment.

   @see c2_stob_get()
   @see c2_stob_find()
 */
void c2_stob_put(struct c2_stob *obj);

/**
   @name adieu

   Asynchronous Direct Io Extensible User interface (adieu) for storage objects.

   <b>Overview</b>.

   adieu is an interface for a non-blocking (asynchronous) 0-copy (direct)
   vectored IO against storage objects.

   A user of this interface builds an IO operation description and queues it
   against a storage object. IO completion or failure notification is done by
   signalling a user supplied c2_chan. As usual, the user can either wait on
   the chan or register a call-back with it.

   adieu supports scatter-gather type of IO operations (that is, vectored on
   both input and output data).

   adieu can work both on local and remote storage objects. adieu IO operations
   are executed as part of a distributed transaction.

   <b>Functional specification.</b>

   Externally, adieu usage has the following phases:

       @li diovec registration. Some types of storage objects require that
       buffers from which IO is done are registered with its IO sub-system
       (examples: RDMA). This step is optional, IO from unregistered buffers
       should also be possible (albeit might incur additional data-copy).

       @li IO description creation. A IO operation description object c2_stob_io
       is initialised.

       @li IO operation is queued by a call to c2_stob_io_launch(). It is
       guaranteed that on a successful return from this call, a chan embedded
       into IO operation data-structure will be eventually signalled.

       @li An execution of a queued IO operation can be delayed for some time
       due to storage traffic control regulations, concurrency control, resource
       quotas or barriers.

       @li An IO operation is executed, possibly by splitting it into
       implementation defined fragments. A user can request an "prefixed
       fragments execution" mode (c2_stob_io_flags::SIF_PREFIX) constraining
       execution concurrency as to guarantee that after execution completion
       (with success or failure) a storage is updated as if some possibly empty
       prefix of the IO operation executed successfully (this is similar to the
       failure mode of POSIX write call). When prefixed fragments execution mode
       is not requested, an implementation is free to execute fragments in any
       order and with any degree of concurrency. Prefixed fragments execution
       mode request has no effect on read-only IO operations.

       @li When whole operation execution completes, a chan embedded into IO
       operation data-structure is signalled. It is guaranteed that no IO is
       outstanding at this moment and that adieu implementation won't touch
       either IO operation structure or associated data pages afterward.

       @li After analyzing IO result codes, a user is free to either de-allocate
       IO operation structure by calling c2_stob_io_fini() or use it to queue
       another IO operation potentially against different object.

   <b>Ordering and barriers.</b>

   The only guarantee about relative order of IO operations state transitions is
   that execution of any updating operation submitted before
   c2_stob_io_opcode::SIO_BARRIER operation completes before any updating
   operation submitted after the barrier starts executing. For the purpose of
   this definition, an updating operation is an operation of any valid type
   different from SIO_READ (i.e., barriers are updating operations).

   A barrier operation completes when all operations submitted before it
   (including other barrier operations) complete.

   @warning Clarify the scope of a barrier: a single storage object, a storage
   object domain, a storage object type, all local storage objects or all
   objects in the system.

   <b>Result codes.</b>

   In addition to filling in data pages with the data (in a case read
   operation), adieu supplies two status codes on IO completion:

       @li <tt>c2_stob_io::si_rc</tt> is a return code of IO operation. 0 means
       success, any other possible value is negated errno;

       @li <tt>c2_stob_io::si_count</tt> is a number of blocks (as defined by
       c2_stob_op::sop_block_shift()) successfully transferred between data
       pages and the storage object. When IO is executed in prefixed fragments
       mode, exactly <tt>c2_stob_io::si_count</tt> blocks of the storage object,
       starting from the offset <tt>c2_stob_io::si_stob.ov_index[0]</tt> were
       transferred.

   <b>Data ownership.</b>

   Data pages are owned by adieu implementation from the moment of call to
   c2_stob_io_launch() until the chan is signalled. adieu users must not
   inspect or modify data during that time. An implementation is free to modify
   the data temporarily, un-map pages, etc. An implementation must not touch
   the data at any other time.

   <b>Liveness rules.</b>

   c2_stob_io can be freed once it is owned by an adieu user (see data
   ownership). It has no explicit reference counting, a user must add its own
   should c2_stob_io be shared between multiple threads.

   The user must guarantee that the target storage object is pinned in memory
   while IO operation is owned by the implementation. An implementation is free
   to touch storage object while IO is in progress.

   Similarly, the user must pin the transaction and IO scope while c2_stob_io is
   owned by the implementation.

   <b>Concurrency.</b>

   When c2_stob_io is owned by a user, the user is responsible for concurrency
   control.

   Implementation guarantees that synchronous channel notification (through
   clink call-back) happens in the context not holding IO lock (see
   c2_stob_op::sop_io_lock()).

   At the moment there are two types of storage object supporting adieu:

   @li Linux file system based one, using Linux libaio interfaces;

   @li AD stob type implements adiue on top of underlying backing store storage
   object.

   <b>State.</b>
   @verbatim

                      (O)(X)
                       |  ^
                       |  |
     c2_stob_io_init() |  | c2_stob_io_fini()
                       |  |
                       V  |
                     SIS_IDLE
                       |  ^
                       |  |
   c2_stob_io_launch() |  | IO completion
                       |  |
                       V  |
                     SIS_BUSY

   @endverbatim

   @todo A natural way to extend this design is to introduce additional
   SIS_PREPARED state and to split IO operation submission into two stages: (i)
   "preparation" stage that is entered once "IO geometry" is known (i.e., once
   c2_vec of data pages and c2_vec storage objects are known) and (ii)
   "queueing" stage that is entered when in addition to IO geometry, actual data
   pages are allocated. The motivating example for this refinement is a data
   server handling read or write RPC from a client. The RPC contains enough
   information to build IO vectors, while data arrive later through RDMA. To
   avoid dead-locks, it is crucial to avoid dynamic resource allocations (first
   of all, memory allocations) in data path after resources are consumed by
   RDMA. To this end, IO operation must be completely set up and ready for
   queueing before RMDA starts, i.e., before data pages are available.

   @todo implement barriers

   @{
 */

/**
   Type of a storage object IO operation.
 */
enum c2_stob_io_opcode {
	SIO_INVALID,
	SIO_READ,
	SIO_WRITE,
	SIO_BARRIER,
	SIO_SYNC
};

/**
   State of adieu IO operation.
 */
enum c2_stob_io_state {
	/** State used to detect un-initialised c2_stob_io. */
	SIS_ZERO = 0,
	/**
	    User owns c2_stob_io and data pages. No IO is ongoing.
	 */
	SIS_IDLE,
	/**
	   Operation has been queued for execution by a call to
	   c2_stob_io_launch(), but hasn't yet been completed. adieu owns
	   c2_stob_io and data pages.
	 */
	SIS_BUSY
};

/**
   Flags controlling the execution of IO operation.
 */
enum c2_stob_io_flags {
	/**
	   Execute operation in "prefixed fragments" mode.

	   It is called "prefixed" because in this mode it is guaranteed that
	   some initial part of the operation is executed. For example, when
	   writing N blocks at offset X, it is guaranteed that when operation
	   completes, blocks in the extent [X, X+M] are written to. When
	   operation completed successfully, M == N, otherwise, M might be less
	   than N. That is, here "prefix" means the same as in "string prefix"
	   (http://en.wikipedia.org/wiki/Prefix_(computer_science) ), because
	   [X, X+M] is a prefix of [X, X+N] when M <= N.
	 */
	SIF_PREFIX = (1 << 0)
};

/**
   Asynchronous direct IO operation against a storage object.
 */
struct c2_stob_io {
	enum c2_stob_io_opcode      si_opcode;
	/**
	   Flags with which this IO operation is queued.
	 */
	enum c2_stob_io_flags       si_flags;
	/**
	   Where data are located in the user address space.

	   @note buffer sizes in c2_stob_io::si_user.ov_vec.v_count[]
	   are in block size units (as determined by
	   c2_stob_op::sop_block_shift). Buffer addresses in
	   c2_stob_io::si_user.ov_buf[] must be shifted block-shift bits
	   to the left.
	 */
	struct c2_bufvec	    si_user;
	/**
	   Where data are located in the storage object name-space.

	   Segments in si_stob must be non-overlapping and go in increasing
	   offset order.

	   @note extent sizes in c2_stob_io::si_stob.ov_vec.v_count[] and extent
	   offsets in c2_stob_io::si_stob.ov_index[] are in block size units (as
	   determined by c2_stob_op::sop_block_shift).
	 */
	struct c2_indexvec          si_stob;
	/**
	   Channel where IO operation completion is signalled.

	   @note alternatively a channel embedded in every state machine can be
	   used.
	 */
	struct c2_chan              si_wait;

	/* The fields below are modified only by an adieu implementation. */

	/**
	   Storage object this operation is against.
	 */
	struct c2_stob             *si_obj;
	/** operation vector */
	const struct c2_stob_io_op *si_op;
	/**
	   Result code.

	   This field is valid after IO completion has been signalled.
	 */
	int32_t                     si_rc;
	/**
	   Number of blocks transferred between data pages and storage object.

	   This field is valid after IO completion has been signalled.
	 */
	c2_bcount_t                 si_count;
	/**
	   State of IO operation. See state diagram for adieu. State transition
	   from SIS_BUSY to SIS_IDLE is asynchronous for adieu user.
	 */
	enum c2_stob_io_state       si_state;
	/**
	   Distributed transaction this IO operation is part of.

	   This field is owned by the adieu implementation.
	 */
	struct c2_dtx              *si_tx;
	/**
	   IO scope (resource accounting group) this IO operation is a part of.
	 */
	struct c2_io_scope         *si_scope;
	/**
	   Pointer to implementation private data associated with the IO
	   operation.

	   This pointer is initialized when c2_stob_io is queued for the first
	   time. When IO completes, the memory allocated by implementation is
	   not immediately freed (the implementation is still guaranteed to
	   never touch this memory while c2_stob_io is owned by a user).

	   @see c2_stob_io::si_stob_magic
	 */
	void                       *si_stob_private;
	/**
	   Stob type magic used to detect when c2_stob_io::si_stob_private can
	   be re-used.

	   This field is set to the value of c2_stob_type::st_magic when
	   c2_stob_io::si_stob_private is allocated. When the same c2_stob_io is
	   used to queue IO again, the magic is compared against type magic of
	   the target storage object. If magic differs (meaning that previous IO
	   was against an object of different type), implementation private data
	   at c2_stob_io::si_stob_private are freed and new private data are
	   allocated. Otherwise, old private data are re-used.

	   @see c2_stob_io::si_stob_private

	   @note magic number is used instead of a pointer to a storage object
	   or storage object class, to avoid pinning them for undefined amount
	   of time.
	 */
	uint32_t                    si_stob_magic;
};

struct c2_stob_io_op {
	/**
	   Called by c2_stob_io_fini() to finalize implementation resources.

	   Also called when the same c2_stob_io is re-used for a different type
	   of IO.

	   @see c2_stob_io_private_fini().
	 */
	void (*sio_fini)(struct c2_stob_io *io);
	/**
	   Called by c2_stob_io_launch() to queue IO operation.

	   @note This method releases lock before successful returning.

	   @pre io->si_state == SIS_BUSY
	   @pre stob->so_op.sop_io_is_locked(stob)
	   @post ergo(result != 0, io->si_state == SIS_IDLE)
	   @post equi(result == 0, !stob->so_op.sop_io_is_locked(stob))
	 */
	int  (*sio_launch)(struct c2_stob_io *io);
};

/**
   @post io->si_state == SIS_IDLE
 */
void c2_stob_io_init  (struct c2_stob_io *io);

/**
   @pre io->si_state == SIS_IDLE
 */
void c2_stob_io_fini  (struct c2_stob_io *io);

/**
   @pre obj->so_state == CSS_EXISTS
   @pre c2_chan_has_waiters(&io->si_wait)
   @pre io->si_state == SIS_IDLE
   @pre io->si_opcode != SIO_INVALID
   @pre c2_vec_count(&io->si_user.ov_vec) == c2_vec_count(&io->si_stob.ov_vec)
   @pre c2_stob_io_user_is_valid(&io->si_user)
   @pre c2_stob_io_stob_is_valid(&io->si_stob)

   @post ergo(result != 0, io->si_state == SIS_IDLE)

   @note IO can be already completed by the time c2_stob_io_launch()
   finishes. Because of this no post-conditions for io->si_state are imposed in
   the successful return case.
 */
int  c2_stob_io_launch (struct c2_stob_io *io, struct c2_stob *obj,
			struct c2_dtx *tx, struct c2_io_scope *scope);

/**
   Returns true if user is a valid vector of user IO buffers.
 */
bool c2_stob_io_user_is_valid(const struct c2_bufvec *user);
/**
   Returns true if stob is a valid vector of target IO extents.
 */
bool c2_stob_io_stob_is_valid(const struct c2_indexvec *stob);

/**
   Scale buffer address into block-sized units.

   @see c2_stob_addr_open()
 */
void *c2_stob_addr_pack(const void *buf, uint32_t shift);

/**
   Scale buffer address back from block-sized units.

   @see c2_stob_addr_pack()
 */
void *c2_stob_addr_open(const void *buf, uint32_t shift);

/** @} end member group adieu */

/** @} end group stob */

/* __COLIBRI_STOB_STOB_H__ */
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
