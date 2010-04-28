/* -*- C -*- */

#ifndef __COLIBRI_STOB_STOB_H__
#define __COLIBRI_STOB_STOB_H__

#include <inttypes.h>

#include "lib/adt.h"
#include "lib/cc.h"

/* import */
struct c2_dtx;
struct c2_clink;
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

struct c2_stob;
struct c2_stob_id;
struct c2_stob_op;
struct c2_stob_io;
struct c2_stob_type;
struct c2_stob_type_op;

struct c2_stob_type {
	const struct c2_stob_type_op *st_op;
	const char                   *st_name;
	const uint32_t                st_magic;
};

struct c2_stob {
	const struct c2_stob_op *so_op;
};

struct c2_stob_type_op {
	int (*sto_init)(struct c2_stob *stob);
};

struct c2_stob_op {
	void (*sop_fini)   (struct c2_stob *stob);
	int  (*sop_io_init)(struct c2_stob *stob, struct c2_stob_io *io);
};

int  c2_stob_type_add(struct c2_stob_type *kind);
void c2_stob_type_del(struct c2_stob_type *kind);


/**
   @name adieu

   Asynchronous Direct Io Extensible User interface (adieu) for storage objects.

   <b>Overview</b>.

   Storage object has an interface for a non-blocking (asynchronous) 0-copy
   (direct) vectored IO.

   A user of this interface builds an IO operation description and queues it
   against a storage object. IO completion or failure notification is done by
   signalling a user supplied c2_clink. As usual, the user can either wait on
   the clink or register a call-back with it.

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
       guaranteed that on a successful return from this call, a clink embedded
       into IO operation data-structure will be eventually signalled.

       @li An execution of a queued IO operation can be delayed for some time
       due to storage traffic control regulations, concurrency control, resource
       quotas or barriers.

       @li An IO operation is executed, possibly by splitting it into
       implementation defined fragments. A user can request an "ordered
       fragments execution" mode (c2_stob_io_flags::SIF_PREFIX) constraining
       execution concurrency as to guarantee that after execution completion
       (with success or failure) a storage is updated as if some possibly empty
       prefix of the IO operation executed successfully (this is similar to the
       failure mode of POSIX write call). When ordered fragments execution mode
       is not requested, an implementation is free to execute fragments in any
       order and with any degree of concurrency. Ordered fragments execution
       mode request has no effect on read-only IO operations.

       @li When whole operation execution completes, a clink embedded into IO
       operation data-structure is signalled. It is guaranteed that no IO is
       outstanding at this moment and that adieu implementation won't touch
       either IO operation structure or associated data pages afterward.

       @li After analyzing IO result codes, a user is free to either de-allocate
       IO operation structure by calling c2_stob_io_fini() or use it to queue
       another IO operation potentially against different object.

   <b>Ordering and barriers.</b>

   The only guarantee about relative order of IO operations state transitions is
   that execution of any operation submitted before
   c2_stob_io_opcode::SIO_BARRIER operation completes before any operation
   submitted after the barrier starts executing.

   A barrier operation completes when all operations submitted before it
   (including other barrier operations) complete.

   <b>Result codes.</b>

   In addition to filling in data pages with the data (in a case read
   operation), adieu supplies two status codes on IO completion:

       @li <tt>c2_stob_io::si_rc</tt> is a return code of IO operation. 0 means
       success, any other possible value is negated errno;

       @li <tt>c2_stob_io::si_count</tt> is a number of bytes successfully
       transferred between data pages and the storage object. When IO is
       executed in ordered fragments mode, exactly <tt>c2_stob_io::si_count</tt>
       bytes of the storage object, starting from the offset
       <tt>c2_stob_io::si_stob.ov_index[0]</tt> were transferred.

   <b>Data ownership.</b>

   Data pages are owned by adieu implementation from the moment of call to
   c2_stob_io_launch() until the clink is signalled. adieu users must not
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
   ownership). While owned by the implementation, c2_stob_io pins corresponding
   storage object. This reference must be released by the user after it has been
   notified about IO completion by calling c2_stob_io_release().

   Additionally, c2_stob_io pins io scope (c2_io_scope) while owned by the
   implementation. This reference is automatically released when IO operation
   completes.

   <b>Concurrency.</b>

   When c2_stob_io is owned by a user, the user is responsible for concurrency
   control.

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
                   SIS_INACTIVE
                       ^  |
                       |  |
        IO completion  |  | c2_stob_io_launch()
                       |  |
                       |  V
                    SIS_ACTIVE

   @endverbatim
   @{
 */

/**
   Type of a storage object IO operation.
 */
enum c2_stob_io_opcode {
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
	SIS_INACTIVE,
	/**
	   Operation has been queued for execution by a call to
	   c2_stob_io_launch(), but hasn't yet been completed. adieu owns
	   c2_stob_io and data pages.
	 */
	SIS_ACTIVE
};

/**
   Flags controlling the execution of IO operation.
 */
enum c2_stob_io_flags {
	/**
	   Execute operation in "ordered fragments" mode. See Functional
	   specification for details.
	 */
	SIF_PREFIX = (1 << 0)
};

/**
   Asynchronous direct IO operation against a storage object.
 */
struct c2_stob_io {
	enum c2_stob_io_opcode      si_opcode;
	/**
	   Where data are located in the user address space.
	 */
	struct c2_diovec            si_user;
	/**
	   Where data are located in the storage object name-space.
	 */
	struct c2_indexvec          si_stob;
	/**
	   Clink where IO operation completion is signalled.
	 */
	struct c2_clink             si_wait;

	/* The fields below are modified only by an adieu implementation. */

	/**
	   Storage object this operation is against.
	 */
	struct c2_stob             *si_obj;
	const struct c2_stob_io_op *si_op;      /*< operation vector */
	uint32_t                    si_rc;
	c2_bcount_t                 si_count;
	enum c2_stob_io_state       si_state;
	struct c2_dtx              *si_tx;
	void                       *si_stob_private;
	uint32_t                    si_stob_magic;
};

struct c2_stob_io_op {
	void (*sio_fini)  (struct c2_stob_io *io);
	int  (*sio_launch)(struct c2_stob_io *io, struct c2_dtx *tx,
			   struct c2_io_scope *scope);
	void (*sio_cancel)(struct c2_stob_io *io);
};

/**
   @post ergo(result == 0, io->si_state == SIS_INACTIVE)
 */
int  c2_stob_io_init  (struct c2_stob_io *io);

/**
   @pre io->si_state == SIS_INACTIVE
 */
void c2_stob_io_fini  (struct c2_stob_io *io);

/**
   @pre !c2_clink_is_armed(&io->si_wait)
   @pre io->si_state == SIS_INACTIVE
   @pre c2_vec_count(&io->si_input.div_vec) == c2_vec_count(&io->si_output.ov_vec)
   @post c2_clink_is_armed(&io->si_wait)
 */
int  c2_stob_io_launch (struct c2_stob_io *io, struct c2_stob *obj, 
			struct c2_dtx *tx, struct c2_io_scope *scope);
void c2_stob_io_cancel (struct c2_stob_io *io);
/**
   @pre  io->si_state == SIS_INACTIVE
   @post io->si_state == SIS_INACTIVE
 */
void c2_stob_io_release(struct c2_stob_io *io);

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
