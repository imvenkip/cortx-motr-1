/* -*- C -*- */

#ifndef __COLIBRI_STOB_STOB_H__
#define __COLIBRI_STOB_STOB_H__

#include "lib/cdefs.h"
#include "lib/adt.h"
#include "lib/cc.h"
#include "sm/sm.h"

/* import */
struct c2_sm;
struct c2_dtx;
struct c2_chan;
struct c2_diovec;
struct c2_indexvec;
struct c2_io_scope;

struct c2_list;
struct c2_list_link;

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

struct c2_stob_id {
	uint64_t	seq;
	uint64_t	id;
};

struct c2_stob;
struct c2_stob_op;
struct c2_stob_io;
struct c2_stob_type;
struct c2_stob_type_op;
struct c2_stob_domain;

struct c2_stob_type {
	const struct c2_stob_type_op *st_op;
	const char                   *st_name;
	const uint32_t                st_magic;
	struct c2_list		      st_domains; /**< list of domains */
};

struct c2_stob {
	struct c2_stob_type 	*so_type;
	const struct c2_stob_op *so_op;
	struct c2_list_link	 so_linkage; /**< linkage into its domain */
	struct c2_stob_id	 so_id;      /**< unique id of this object */
	struct c2_stob_domain 	*so_domain;  /**< its domain */
};

struct c2_stob_type_op {
	int  (*sto_init)(struct c2_stob_type *stype);
	void (*sto_fini)(struct c2_stob_type *stype);
	/**
	   Locates and initlialises the storage objects domain, makes it ready
	   for operations.

	   This operation is called before any other operations.

	   @return 0 success, any other value means error.
	   @see c2_stob_domain_op::sdo_fini()
	*/
	int  (*sto_domain_locate)(struct c2_stob_domain *dom, ...);
};

struct c2_stob_op {
	int  (*sop_init)   (struct c2_stob *stob);
	void (*sop_fini)   (struct c2_stob *stob);
	/**
	   Initialises IO operation structure, preparing it to be queued for a
	   given storage object.

	   This is called when IO operation structure is used to queue IO for
	   the first time or when the last time is queued IO for a different
	   type of storage object.

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
};

int  c2_stob_type_add(struct c2_stob_type *kind);
void c2_stob_type_del(struct c2_stob_type *kind);


struct c2_stob_domain_op;
/**
   stob domain

   Stob domain is a collection of storage objects of the same type.
   A stob type may have multiple domains, which are linked together to its
   type by 'sd_domain_linkage'. A stob domain may have multiple storage objects,
   which are linked together by 'sd_objects'.
*/
struct c2_stob_domain {
	const char 		        *sd_name;
	const struct c2_stob_domain_op *sd_ops;
	struct c2_list_link	        sd_domain_linkage;

	struct c2_list	 	        sd_objects;
};

/**
   domain operations vector
*/
struct c2_stob_domain_op {
	/**
	   Init this domain. E.g. init the list, connecting to mapping db.
	*/
	int (*sdo_init)(struct c2_stob_domain *self);

	/**
	   Cleanup this domain.
	*/
	void (*sdo_fini)(struct c2_stob_domain *self);

	/**
	   alloc in-memory structure for an object, and add it into this domain.
	*/
	struct c2_stob *(*sdo_alloc)(struct c2_stob_domain *d,
			               struct c2_stob_id *id);
	/**
	   free in-memory structure for an object
	*/
	void (*sdo_free)(struct c2_stob_domain *d, struct c2_stob *o);

	/**
	  Create an object.

	  Create an object on storage physically, and insert the
	  mapping from the id to the internal object repsentative into its
	  database.

	  @return 0 success, other values mean error.
	  @post when succeed, out points to the internal object
	*/
	int (*sdo_create)(struct c2_stob_domain *d, struct c2_stob *o);

	/**
	   setup the mapping from id to intnerl representative by looking up
	   in its database.

	  @return 0 success, other values mean error
	*/
	int (*sdo_locate)(struct c2_stob_domain *d, struct c2_stob *o);

};


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

       @li <tt>c2_stob_io::si_count</tt> is a number of bytes successfully
       transferred between data pages and the storage object. When IO is
       executed in prefixed fragments mode, exactly
       <tt>c2_stob_io::si_count</tt> bytes of the storage object, starting from
       the offset <tt>c2_stob_io::si_stob.ov_index[0]</tt> were transferred.

   <b>Data ownership.</b>

   Data pages are owned by adieu implementation from the moment of call to
   c2_stob_io_launch() until the chan is signalled. adieu users must not
   inspect or modify data during that time. An implementation is free to modify
   the data temporarily, un-map pages, etc. An implementation must not touch
   the data at any other time.

   <b>IO cancellation.</b>

   The c2_stob_io_cancel() function attempts to cancel IO. This is only a
   best-effort call without any hard guarantees, because it may be impossible to
   cancel ongoing IO for certain implementations. Cancelled IO terminates with
   -EINTR result code.

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

   @note at the moment the only type of storage object supporting adieu is a
   Linux file system based one, using Linux libaio interfaces.

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
	   writing N bytes at offset X, it is guaranteed that when operation
	   completes, bytes in the extent [X, X+M] are written to. When
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
	 */
	struct c2_diovec            si_user;
	/**
	   Where data are located in the storage object name-space.
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
	   Number of bytes transferred between data pages and storage object.

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
	/**
	   IO operation is a state machine, see State diagram for adieu.
	 */
	struct c2_sm                si_mach;
};

struct c2_stob_io_op {
	/**
	   Called by c2_stob_io_launch() to queue IO operation.

	   @note This method releases lock before successful returning.

	   @pre io->si_state == SIS_BUSY
	   @pre stob->so_op.sop_io_is_locked(stob)
	   @post ergo(result != 0, io->si_state == SIS_IDLE)
	   @post equi(result == 0, !stob->so_op.sop_io_is_locked(stob))
	 */
	int  (*sio_launch) (struct c2_stob_io *io);
	/**
	   Attempts to cancel IO operation. Has no effect when called before IO
	   has been queued or after IO has completed.
	 */
	void (*sio_cancel) (struct c2_stob_io *io);
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
   @pre c2_chan_has_waiters(&io->si_wait)
   @pre io->si_state == SIS_IDLE
   @pre io->si_opcode != SIO_INVALID
   @pre c2_vec_count(&io->si_user.div_vec.ov_vec) == c2_vec_count(&io->si_stob.ov_vec)
   @post ergo(result != 0, io->si_state == SIS_IDLE)

   @note IO can be already completed by the time c2_stob_io_launch()
   finishes. Because of this no post-conditions for io->si_state are imposed in
   the successful return case.
 */
int  c2_stob_io_launch (struct c2_stob_io *io, struct c2_stob *obj, 
			struct c2_dtx *tx, struct c2_io_scope *scope);
void c2_stob_io_cancel (struct c2_stob_io *io);

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
