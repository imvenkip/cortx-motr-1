/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED
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
 * Authors:         Juan   Gonzalez <juan.gonzalez@seagate.com>
 *                  James  Morse    <james.s.morse@seagate.com>
 *                  Sining Wu       <sining.wu@seagate.com>
 * Revision:        Pratik Shinde   <pratik.shinde@seagate.com>
 * Original creation date: 07-Feb-2013
 */

#pragma once

#ifndef __MERO_CLOVIS_CLOVIS_H__
#define __MERO_CLOVIS_CLOVIS_H__

#include "lib/vec.h"
#include "lib/types.h"
#include "sm/sm.h"             /* struct m0_sm */
#include "rpc/rpc_machine.h"   /* M0_RPC_DEF_MAX_RPC_MSG_SIZE */

/**
 * @defgroup clovis
 *
 * Overview
 * --------
 *
 * Clovis is *the* interface exported by Mero for use by the Mero
 * applications. Examples of Mero applications are:
 *
 *     - Mero file system client (m0t1fs);
 *
 *     - Lustre osd-mero module (part of LOMO);
 *
 *     - Lustre HSM backend (part of Castor-A200);
 *
 *     - SWIFT or S3 backend (part of WOMO);
 *
 *     - Mero-based block device (part of BOMO);
 *
 *     - exascale E10 stack (see eiow.org).
 *
 * Clovis is divided into the following sub-interfaces:
 *
 *     - access sub-interface, which provides basic absraction to build storage
 *       application;
 *
 *     - management sub-interface, which provides methods for Mero cluster
 *       deployment, configuration, re-configuration and monitoring;
 *
 *     - extension sub-interface, which allows applications to extend Mero
 *       functionality without modifying the core Mero.
 *
 * This header describes the access sub-interface of clovis, simply called
 * "clovis interface" hereafter.
 *
 * In the following "an application" means a code invoking the clovis interface
 * and "the implementation" refers to the implementation of the said interface.
 *
 * Clovis provides the following abstractions:
 *
 *     - object (m0_clovis_obj) is an array of fixed-size blocks;
 *
 *     - index (m0_clovis_idx) is a key-value store;
 *
 *     - realm (m0_clovis_realm) is a spatial and temporal part of system with a
 *       prescribed access discipline. Objects, indices and operations live in
 *       realms;
 *
 *     - operation (m0_clovis_op) is a process of querying or updating system
 *       state;
 *
 * Realms are further sub-divided in:
 *
 *     - transaction (m0_clovis_dtx) is a collection of operations atomic in the
 *       face of failures;
 *
 *     - epoch (m0_clovis_epoch) is a collection of operations done by an
 *       application, which moves the system from one application-consistent
 *       state to another;
 *
 *     - container (m0_clovis_container) is a collection of objects used by a
 *       particular application or group of applications;
 *
 *     - other types of realms, as can be added in the future.
 *
 * Object, index and realm are sub-types of entity (m0_clovis_entity). Entities
 * provide state, interface and behavior shared by all sub-types.
 *
 * All clovis entry points, except for m0_clovis_op_wait(), are non-blocking.
 * To perform a potentially lengthy activity, that might involve network
 * communication (for example, read from an object), the clovis entry point
 * (m0_clovis_obj_op() in the case of object read), sets up an operation
 * (m0_clovis_ops) structure containing the parameters of the activity and
 * immediately returns to the caller. The caller should explicitly launch a set
 * of previously prepared operations by calling m0_clovis_op_launch().
 * Separation of preparation and launch provides for more efficient network
 * communication, where multiple operations are accumulated in the same network
 * message.
 *
 * In-memory structures (m0_clovis_{obj,index,realm,...}) correspond to some
 * storage entities maintained by the implementation. The implementation does
 * not enforce this correspondence. If an application keeps multiple in-memory
 * structures for the same storage entity (whether in the same process address
 * space or not), it is up to the application to keep the structures coherent
 * (the very notion of coherency, obviously, being application-dependent). At
 * the one end of the spectrum, an application can employ a fully coherent,
 * distributed cache for entities, providing strong consistency guarantees. On
 * the other end, an application can tolerate multiple inconsistent views of the
 * same storage entities, providing NFSv2-like semantics.
 *
 * Sub-typing
 * ----------
 *
 * @verbatim
 *
 *        entity (create, delete, open, close, fini) [abstract, no constructor]
 *          |
 *          |
 *          +---- object (init, read, write, alloc, free)
 *          |
 *          |
 *          +---- index (init, get, put, next)
 *          |
 *          |
 *          +---- realm () [abstract, no constructor]
 *                  |
 *                  |
 *                  +---- container (init)
 *                  |
 *                  |
 *                  +---- epoch (init)
 *                  |
 *                  |
 *                  +---- dtx (init)
 *
 *
 *        op (init, wait, setup, launch, kick, free, fini)
 *           [has private sub-types in clovis_private.h]
 *
 * @endverbatim
 *
 * Identifiers
 * -----------
 *
 * An entity exists in some realm and has a 128-bit identifier, unique within
 * the cluster and never re-used. The high 8 bits of an identifier denote the
 * entity type. Identifier management is up to the application, except that the
 * single identifier M0_CLOVIS_UBER_REALM is reserved for the "uber realm",
 * representing the root of the realm hierarchy, and within each entity type,
 * identifiers less than M0_CLOVIS_ID_APP are reserved for the implementation's
 * internal use.
 *
 * @todo A library on top of clovis for fast scalable identifier allocation will
 * be provided as part of Mero.
 *
 * The implementation is free to reserve some 8-bit combinations for its
 * internal use.
 *
 * @todo an interface to register 8-bit combinations for application use (to
 * introduce application-specific "entity-like" things).
 *
 * Operations
 * ----------
 *
 * An operation structure tracks the state of execution of a request made to the
 * implementation.
 *
 * An operation structure is a state machine going through states described by
 * enum m0_clovis_op_state:
 *
 * @verbatim
 *                                  (0)
 *                                   |
 *                                   |
 *                                   V
 *              +---------------INITIALISED
 *              |                    |
 *              |                    | m0_clovis_op_launch()
 *              V                    V
 *           FAILED<-------------LAUNCHED
 *              ^                    |
 *              |                    |
 *              |                    V
 *              +----------------EXECUTED---------------->STABLE
 * @endverbatim
 *
 * An operation in INITIALISED, FAILED or STABLE state is called "complete" and
 * "outstanding" (or "in-progress") otherwise.
 *
 * An operation is in INITIALISED state after allocation. In this state, the
 * operation processing is not yet started, the application is free to modify
 * operation parameters with a call to m0_clovis_op_setup() or direct field
 * access.
 *
 * Multiple initialised operation structures can be simultaneously moved to the
 * LAUNCHED state, by a call to m0_clovis_op_launch(). This call starts actual
 * operation processing. No changes to the operation structure are allowed by
 * the application after this call is made and until the operation completes.
 * To improve caching and utilisation of system resources, the implementation
 * is free to delay any operation-related acitivities, such as sending network
 * messages, for some time after the operation is launched. The value of
 * m0_clovis_op::op_linger is an application-provided hint about the absolute
 * time by which such delays should be limited.
 *
 * In case of successful execution, a launched operation structure eventually
 * reaches EXECUTED state, meaning that the operation was executed at least in
 * the volatile stores of the respective services. When the operation enters
 * EXECUTED state, the m0_clovis_op_ops::oop_executed() call-back, if provided
 * by the application, is invoked. By the time this state is entered, the
 * operation return code is in m0_clovis_op::op_sm::sm_rc, and all return
 * information (object data in case of READ, keys and values in case of GET and
 * NEXT) are already placed in the application-supplied buffers.
 *
 * After an operation has been executed, it can still be lost due to a
 * failure. The implementation continues to work toward making the operation
 * stable. When this activity successfully terminates, the operation enters the
 * STABLE state and the m0_clovis_op_ops::oop_stable() call-back is invoked, if
 * provided. Once an operation is stable, the implementation guarantees that the
 * operation would survive any "allowed failure", where allowed failures include
 * at least transient service failures (crash and restart with volatile store
 * loss), transient network failures and client failures.
 *
 * In case of a failure, the operation structure moves into FAILED state, the
 * m0_clovis_op_ops::oop_failed() call-back is invoked, and no further state
 * transitions will ensue.
 *
 * The implementation is free to add another states to the operation state
 * machine.
 *
 * All operation structures embed "generic operation" m0_clovis_op as the first
 * member.
 *
 * The application can track the state of the operation either synchronously, by
 * waiting until the operation reaches a particular state (m0_clovis_op_wait()),
 * or asynchronously by supplying (m0_clovis_op_setup()) a call-back to be
 * called when the operation reaches a particular state.
 *
 * Operation structures are either pre-allocated by the application or allocated
 * by the appropriate entry points, see the "op" parameter of m0_clovis_obj_op()
 * for an example. When an operation structure is pre-allocated, the application
 * must set m0_clovis_op::op_size to the size of the pre-allocated structure
 * before passing it to a clovis entry point. This allows the implementation to
 * check that the pre-allocated structure has enough room and return an error
 * (-EMSGSIZE) otherwise.
 *
 * Operation errors are returned through m0_clovis_op::op_sm::sm_rc.
 *
 * Operations, common for all entity types are implemented at the entity level:
 * m0_clovis_entity_create(), m0_clovis_entity_delete(),
 * m0_clovis_entity_fini().
 *
 * A typical usage would involve initialisation of a concrete entity (e.g.,
 * object), execution of some generic operations and then of some concrete
 * operations, for example:
 *
 * @code
 * m0_clovis_obj  o;
 * m0_clovis_op  *ops[2] = {};
 *
 * // initialise object in-memory structure.
 * m0_clovis_obj_init(&o, &container, &id);
 *
 * // initiate object creation. m0_clovis_entity_create() allocated the
 * // operation and stores the pointer to it in ops[0].
 * m0_clovis_entity_create(&o.ob_entity, &ops[0]);
 *
 * // initiate write data in the object.
 * result = m0_clovis_obj_op(&o, M0_CLOVIS_OC_WRITE, ..., &ops[1]);
 *
 * // launch both operations (creation and write)
 * m0_clovis_op_launch(ops, ARRAY_SIZE(ops));
 *
 * // wait until creation completes
 * result = m0_clovis_op_wait(op[0], M0_BITS(M0_CLOVIS_OS_STABLE,
 *                                           M0_CLOVIS_OS_FAILED),
 *                            M0_TIME_NEVER);
 * // wait until write completes
 * result = m0_clovis_op_wait(op[1], M0_BITS(M0_CLOVIS_OS_STABLE,
 *                                           M0_CLOVIS_OS_FAILED),
 *                            M0_TIME_NEVER);
 * // finalise the object
 * m0_clovis_entity_fini(&o.ob_entity);
 *
 * // free the operations
 * m0_clovis_op_free(op[0]);
 * m0_clovis_op_free(op[1]);
 * @endcode
 *
 * Object
 * ------
 *
 * A clovis object is an array of blocks, which can be read from and written
 * onto at the block granularity.
 *
 * Block size is a power of two bytes and is selected at the object creation
 * time.
 *
 * An object has no traditional application-visible meta-data (in particular, it
 * has no size). Instead it has meta-data, called "block attributes" associated
 * with each block. Block attributes can be used to store check-sums, version
 * numbers, hashes, etc. Because of the huge number of blocks in a typical
 * system, the overhead of block attributes book-keeping must be kept at a
 * minimum, which puts restrictions on the block attribute access interface
 * (@todo to be described).
 *
 * There are 4 types of object operations, in addition to the common entity
 * operations:
 *
 *     - READ: transfer blocks and block attributes from an object to
 *       application buffers;
 *
 *     - WRITE: transfer blocks and block attributes from application buffers to
 *       an object;
 *
 *     - ALLOC: pre-allocate certain blocks in an implementation-dependent
 *       manner. This operation guarantees that consecutive WRITE onto
 *       pre-allocated blocks will not fail due to lack of storage space;
 *
 *     - FREE: free storage resources used by specified object
 *       blocks. Consecutive reads from the blocks will return zeroes.
 *
 * READ and WRITE operations are fully scatter-gather-scatter: data are
 * transferred between a sequence of object extents and a sequence of
 * application buffers, the only restrictions being:
 *
 *     - total lengths of the extents must be equal to the total size of the
 *       buffers, and
 *
 *     - extents must be block-size aligned and sizes of extents and buffers
 *       must be multiples of block-size.
 *
 * Internally, the implementation stores an object according to the object
 * layout (specified at the object creation time). The layout determines
 * fault-tolerance and performance related characteristics of the
 * object. Examples layouts are:
 *
 *     - network striping with parity de-clustering. This is the default layout,
 *       it provides a flexible level of fault-tolerance, high availability in
 *       the face of permanent storage device failures and full utilisation of
 *       storage resources;
 *
 *     - network striping without parity (raid0). This provides higher space
 *       utilisation and lower processor costs than parity de-clustering at the
 *       expense of fault-tolerance;
 *
 *     - network mirroring (raid1). This provides high fault-tolerance and
 *       availability at the cost of storage space consumption;
 *
 *     - de-dup, compression, encryption.
 *
 * Index
 * -----
 *
 * A clovis index is a key-value store.
 *
 * An index stores records, each record consisting of a key and a value. Keys
 * and values within the same index can be of variable size. Keys are ordered by
 * the lexicographic ordering of their bit-level representation. Records are
 * ordered by the key ordering. Keys are unique within an index.
 *
 * There are 4 types of index operations:
 *
 *     - GET: given a set of keys, return the matching records from the index;
 *
 *     - PUT: given a set of records, place them in the index, overwriting
 *       existing records if necessary, inserting new records otherwise;
 *
 *     - DEL: given a set of keys, delete the matching records from the index;
 *
 *     - NEXT: given a set of keys, return the records with the next (in the
 *       ascending key order) keys from the index.
 *
 * Indices are stored according to a layout, much like objects.
 *
 * Realm
 * -----
 *
 * To define what a realm is, consider the entire history of a clovis storage
 * system. In the history, each object and index is represented as a "world
 * line" (https://en.wikipedia.org/wiki/World_line), which starts when the
 * entity is created and ends when it is deleted. Points on the world line
 * correspond to operations that update entity state.
 *
 * A realm is the union of some continuous portions of different world
 * lines. That is, a realm is given by a collection of entities and, for each
 * entity in the collection, a start and an end point (operations) on the
 * entity world line. A realm can be visualised as a cylinder in the history.
 *
 * The restriction placed on realms is that each start point in a realm must
 * either be the first point in a world line (i.e., the corresponding entity is
 * created in the realm) or belong to the same realm, called the parent of the
 * realm in question. This arranges realms in a tree.
 *
 * @note Realms are *not* necessarily disjoint.
 *
 * A realm can be in the following application-controllable states:
 *
 *     - OPEN: in this state the realm can be extended by executing new
 *       operations against entities already in the realm or creating new
 *       entities in the realm;
 *
 *     - CLOSED: in this state the realm can no longer be extended, but it is
 *       tracked by the system and maintains its identity. Entities in a closed
 *       realm can be located and read-only operations can be executed on them;
 *
 *     - ABSORBED: in this state the realm is no longer tracked by the
 *       system. All the operations executed as part of the realm are by now
 *       stable and absorbed in the parent realm;
 *
 *     - FAILED: an application aborted the realm or the implementation
 *       unilaterally decided to abort it. The operations executed in the realm
 *       are undone together with a transitive closure of dependent operations
 *       (the precise definition being left to the implementation
 *       discretion). Thus, failure of a realm can lead to cascading failures of
 *       other realms.
 *
 * Examples of realms are:
 *
 *     - a container (m0_clovis_container) can be thought of as a "place" where
 *       a particular storage application lives. In a typical scenario, when an
 *       application is setup on a system, a new container, initially empty,
 *       will be created for the application. The application can create new
 *       entities in the container and manipulate them without risk of conflicts
 *       (e.g., for identifier allocation) with other applications. A container
 *       can be thought of as a virtualised storage system for an application. A
 *       container realm is open as long as application needs its persistent
 *       data. When the application is uninstalled, its realm is deleted;
 *
 *     - a snapshot realm is created with a container as the parent and is
 *       immediately closed. From now on, the snapshot provides a read-only view
 *       of container objects at the moment of the snapshot creation. Finally,
 *       the snapshot is deleted. If a snapshot is not closed immediately, but
 *       remains open, it is a writeable snapshot (clone)---a separate branch in
 *       the container's history. A clone is eventually deleted without being
 *       absorbed in the parent container;
 *
 *     - an epoch (m0_clovis_epoch) is a realm capturing part of the
 *       application's work-flow for resilience. Often an HPC application works
 *       by interspersing "compute phases", when actual data processing is done,
 *       with an "IO phase" when a checkpoint of application state is saved on
 *       the storage system for failure recovery purposes. A clovis application
 *       would, instead, keep an open "current" epoch realm, closed at the
 *       compute-IO phase transition, with the new epoch opened immediately. The
 *       realm tree for such application would look like
 *
 * @verbatim
 *
 *     CONTAINER--->E--->E---...->E--->CURRENT
 *
 * @endverbatim
 *
 *       Where all E epochs are closed and in the process of absorbtion, and all
 *       earlier epochs already absorbed in the container.
 *
 *       If the application fails, it can restart either from the container or
 *       from any closed epoch, which are all guaranteed to be consistent, that
 *       is, reflect storage state at the boundry of a compute phase. The final
 *       CURRENT epoch is potentially inconsistent after a failure and should be
 *       deleted.
 *
 *     - a distributed transaction (m0_clovis_dtx) is a group of operations,
 *       which must be atomic w.r.t. to failures.
 *
 * Ownership
 * ---------
 *
 * clovis entity structures (realms, objects and indices) are allocated by the
 * application. The application may free a structure after completing the
 * corresponding finalisation call. The application must ensure that all
 * outstanding operations on the entity are complete before finalisation.
 *
 * An operation structure allocated by the application, must remain allocated
 * until the operation is complete. Before a complete operation structure can be
 * re-used, it should be finalised by a call to m0_clovis_op_fini(). An
 * operation structure allocated by the clovis implementation can be finalised
 * and re-used as one allocated by the application, and must be eventually freed
 * by the application (by calling m0_clovis_op_free()) some time after the
 * operation completes.
 *
 * Data blocks used by scatter-gather-scatter lists and key-value records are
 * allocated by the application. For read-only operations (M0_CLOVIS_OC_READ,
 * M0_CLOVIS_IC_GET and M0_CLOVIS_IC_NEXT) the application may free the data
 * blocks as soon as the operation reaches EXECUTED or FAILED state. For
 * updating operations, the data blocks must remain allocated until the
 * operation stabilises.
 *
 * Concurrency
 * -----------
 *
 * The clovis implementation guarantees that concurrent calls to the same index
 * are linearizable.
 *
 * All other concurrency control, including ordering of reads and writes to a
 * clovis object, and distributed transaction serializability, is up to the
 * application.
 *
 * @see https://docs.google.com/a/xyratex.com/document/d/sHUAUkByacMNkDBRAd8-AbA
 *
 * @todo entity type structures (to provide constructors, 8-bit identifier tags
 * and an ability to register new entity types).
 *
 * @todo handling of extensible attributes (check-sums, version numbers, etc.),
 * which require interaction with the implementation on the service side.
 *
 * @{
 */

/**
 * Operation codes for entity, object and index.
 */
enum m0_clovis_entity_opcode {
	M0_CLOVIS_EO_INVALID,
	M0_CLOVIS_EO_CREATE,
	M0_CLOVIS_EO_DELETE,
	M0_CLOVIS_EO_NR
};

/** Object operation codes. */
enum m0_clovis_obj_opcode {
	/** Read object data. */
	M0_CLOVIS_OC_READ = M0_CLOVIS_EO_NR + 1,
	/** Write object data. */
	M0_CLOVIS_OC_WRITE,
	/** Pre-allocate space. */
	M0_CLOVIS_OC_ALLOC,
	/** De-allocate space, consecutive reads will return 0s. */
	M0_CLOVIS_OC_FREE,
	M0_CLOVIS_OC_NR
};

/* Index operation codes. */
enum m0_clovis_idx_opcode {
	/** Lookup a value with the given key. */
	M0_CLOVIS_IC_GET = M0_CLOVIS_OC_NR + 1,
	/** Insert or update the value, given a key. */
	M0_CLOVIS_IC_PUT,
	/** Delete the value, if any, for the given key. */
	M0_CLOVIS_IC_DEL,
	/** Given a key, return the next key and its value. */
	M0_CLOVIS_IC_NEXT,
	/** Check an index for an existence. */
	M0_CLOVIS_IC_LOOKUP,
	/** Given an index id, get the list of next indices. */
	M0_CLOVIS_IC_LIST
};

/**
 * Types of entities supported by clovis.
 */
enum m0_clovis_entity_type {
	M0_CLOVIS_ET_REALM,
	M0_CLOVIS_ET_OBJ,
	M0_CLOVIS_ET_IDX
};

/**
 * Generic clovis operation structure.
 */
struct m0_clovis_op {
	uint64_t                       op_magic;

	/**
	 * Operation code.
	 *
	 * @see m0_clovis_entity_opcode, m0_clovis_realm_opcode
	 * @see m0_clovis_obj_opcode, m0_clovis_idx_opcode,
	 */
	unsigned int                   op_code;
	/** Each op has its own sm group. */
	struct m0_sm_group             op_sm_group;
	/** Operation state machine. */
	struct m0_sm                   op_sm;
	/** Application-supplied call-backs. */
	const struct m0_clovis_op_ops *op_cbs;
	/** The entity this operation is on. */
	struct m0_clovis_entity       *op_entity;
	/** Caching dead-line. */
	m0_time_t                      op_linger; /* a town in Luxembourg. */
	/** Size of the ambient operation structure. */
	size_t                         op_size;
	/** Part of a cookie (m0_cookie) used to identify this operation. */
	uint64_t                       op_gen;

	/* Operation's private data, can be used as arguments for callbacks.*/
	void                          *op_datum;
};

/**
 * Operation state, stored in m0_clovis_op::op_sm::sm_state.
 */
enum m0_clovis_op_state {
	M0_CLOVIS_OS_UNINITIALISED,
	M0_CLOVIS_OS_INITIALISED,
	M0_CLOVIS_OS_LAUNCHED,
	M0_CLOVIS_OS_EXECUTED,
	M0_CLOVIS_OS_STABLE,
	M0_CLOVIS_OS_FAILED,
	M0_CLOVIS_OS_NR
};

/**
 * Common structure shared by objects, indices and realms.
 */
struct m0_clovis_entity {
	/** Entity type. */
	enum m0_clovis_entity_type en_type;
	/** Globally unique, not re-usable entity identifier. */
	struct m0_uint128          en_id;
	/** Parent realm, this entity lives in. */
	struct m0_clovis_realm    *en_realm;
	/**
	 * Entity state machine. Used internally by the implementation. For the
	 * reference, the state diagram is:
	 *
	 * @verbatim
	 *                  create
	 *        CREATING<--------+
	 *            |            |
	 *            |            |
	 *            |            |
	 *            |            |
	 *            +---------->INIT<----------------------CLOSING
	 *            |            | |                           ^
	 *            |            | |                           |
	 *            |            | |                           | close
	 *            |            | |                           |
	 *        DELETING<--------+ +-------->OPENING-------->OPEN
	 *                  delete      open
	 * @endverbatim
	 *
	 */
	struct m0_sm               en_sm;
	/** Each entity has its own sm group. */
	struct m0_sm_group         en_sm_group;
};

/**
 * Object attributes.
 *
 * This is supplied by an application when an object is created and returned by
 * the implementation when an object is opened.
 */
struct m0_clovis_obj_attr {
	/** Binary logarithm (bit-shift) of object IO buffer size. */
	m0_bcount_t oa_bshift;

	/** Layout ID for an object. */
	uint64_t    oa_layout_id;
};

/**
 * Object is an array of blocks. Each block has 64-bit index and a block
 * attributes.
 */
struct m0_clovis_obj {
	struct m0_clovis_entity   ob_entity;
	struct m0_clovis_obj_attr ob_attr;

	/** list of pending transactions */
	struct m0_tl              ob_pending_tx;
	struct m0_mutex           ob_pending_tx_lock;
};

/**
 * Index is an ordered key-value store.
 *
 * A record is a key-value pair. A new record can be inserted in an index,
 * record with a given key can be looked for, updated or deleted.
 *
 * An index can be iterated starting from a given key. Keys are ordered in the
 * lexicographical order of their bit-representations.
 */
struct m0_clovis_idx {
	struct m0_clovis_entity in_entity;
};

enum m0_clovis_realm_type {
	M0_CLOVIS_ST_CONTAINER,
	M0_CLOVIS_ST_EPOCH,
	M0_CLOVIS_ST_DTX,
	M0_CLOVIS_ST_NR
};

/**
 * Forward declaration: m0_clovis represents a clovis instance, a connection
 * to a mero cluster.
 *
 * Defined in clovis/clovis_internal.h
 */
struct m0_clovis;

/**
 * Realm is where entities (including other realms) live.
 *
 * @see m0_clovis_container, m0_clovis_epoch, m0_clovis_dtx.
 */
struct m0_clovis_realm {
	struct m0_clovis_entity   re_entity;
	enum m0_clovis_realm_type re_type;
	struct m0_clovis         *re_instance;
};

/**
 * Container is a special type of realm, used to partition storage system among
 * applications.
 */
struct m0_clovis_container {
	struct m0_clovis_realm co_realm;
};

/**
 * Epoch is a special type of realm, used by an application (or a
 * collaborative set of applications) to partition their work in consistent
 * portions.
 *
 * Epoch boundary should be a consistent (from application point of view) state
 * of storage. By resuming from a given epoch, applications can implement a
 * scalable failure recovery mechanism.
 */
struct m0_clovis_epoch {
	struct m0_clovis_realm ep_realm;
};

/**
 * Distributed transaction is a special type of realm, which is a group of
 * operations atomic w.r.t. certain failures.
 */
struct m0_clovis_dtx {
	struct m0_clovis_realm dt_realm;
};

/**
 * Operation callbacks.
 */
struct m0_clovis_op_ops {
	void (*oop_executed)(struct m0_clovis_op *op);
	void (*oop_failed)(struct m0_clovis_op *op);
	void (*oop_stable)   (struct m0_clovis_op *op);
};

/**
 * m0_clovis_config contains configuration parameters to setup an
 * clovis instance.
 */
struct m0_clovis_config {
	/** oostore mode is set when 'is_oostore' is TRUE. */
	bool        cc_is_oostore;
	/**
	 * Flag for verify-on-read. Parity is checked when doing
	 * READ's if this flag is set.
	 */
	bool        cc_is_read_verify;

	/** Local endpoint.*/
	const char *cc_local_addr;
	/** HA service's endpoint.*/
	const char *cc_ha_addr;
	/** Confd service's endpoint.*/
	const char *cc_confd;
	/** Process fid for rmservice@clovis. */
	const char *cc_process_fid;
	const char *cc_profile;

	/**
	 * The minimum length of the 'tm' receive queue,
         * use M0_NET_TM_RECV_QUEUE_DEF_LEN if unsure.
         */
	uint32_t    cc_tm_recv_queue_min_len;
	/**
	 * The maximum rpc message size, use M0_RPC_DEF_MAX_RPC_MSG_SIZE
	 * if unsure.
	 */
	uint32_t    cc_max_rpc_msg_size;

	/* TODO: This parameter is added for a temporary solution of
	 * layout selection for s3 team. This has to be removed when
	 * sophisticated solution is implemented.*/
	uint32_t    cc_layout_id;

	int         cc_idx_service_id;
	void       *cc_idx_service_conf;
};

/** The identifier of the root of realm hierarchy. */
extern const struct m0_uint128 M0_CLOVIS_UBER_REALM;

/**
 * First identifier that applications are free to use.
 *
 * It is guaranteed that M0_CLOVIS_UBER_REALM falls into reserved extent.
 * @invariant m0_uint128_cmp(&M0_CLOVIS_UBER_REALM, &M0_CLOVIS_ID_APP) < 0
 */
extern const struct m0_uint128 M0_CLOVIS_ID_APP;

/**
 * Sets application-manipulable operation parameters.
 *
 * @param op Operation to be setup with callback functions.
 * @param cbs Callback functions.
 * @param linger The absolute time by which delays should be limited.
 * If linger < m0_time_now(), the op is executed as soon as possible.
 *
 * @pre op != NULL
 * @pre op->op_sm.sm_state == M0_CLOVIS_OS_INITIALISED
 */
void m0_clovis_op_setup(struct m0_clovis_op *op,
			const struct m0_clovis_op_ops *cbs,
			m0_time_t linger);
/**
 * Launches a collection of operations. Operations must belong to the same
 * m0_clovis instances.
 *
 * @note the launched operations may be in other states than
 * M0_CLOVIS_OS_LAUNCHED by the time this call returns.
 *
 * @param op Array of operations to be launched.
 * @param nr Number of operations.
 *
 * @pre ergo(op != NULL)
 * @pre m0_forall(i, nr, op[i] != NULL)
 * @pre m0_forall(i, nr, op[i]->op_sm.sm_state == M0_CLOVIS_OS_INITIALISED)
 * @pre m0_forall(i, nr, m0_entity_type_is_valid(op[i]->op_entity))
 * @post m0_forall(i, nr, op[i]->op_sm.sm_state >= M0_CLOVIS_OS_LAUNCHED)
 */
void m0_clovis_op_launch(struct m0_clovis_op **op, uint32_t nr);

/**
 * Waits until the operation reaches a desired state.
 *
 * @param bits Bitmask of states based on m0_clovis_op_state. M0_BITS() macro
 * should be used to build a bitmask. *
 * @param op Single operation to wait on.
 * @param to Absolute timeout for the wait.
 *
 * @code
 * // Wait until the operation completes, 10 seconds max.
 * result = m0_clovis_op_wait(op, M0_BITS(M0_CLOVIS_OS_STABLE,
 *                                        M0_CLOVIS_OS_FAILED),
 *                            m0_time_from_now(10, 0));
 * if (result == -ETIMEDOUT)
 *          // Timed out.
 * else if (result == 0) {
 *         // Wait completed in time.
 *         if (op->op_sm.sm_state == M0_CLOVIS_OS_STABLE) {
 *                 ...
 *         } else {
 *                 M0_ASSERT(op->op_sm.sm_state == M0_CLOVIS_OS_FAILED);
 *                 ...
 *         }
 * } else {
 *         // Some other error.
 * }
 * @endcode
 *
 * @pre op != NULL
 * @pre bits != 0
 * @pre (bits & ~M0_BITS(M0_CLOVIS_OS_LAUNCHED, M0_CLOVIS_OS_EXECUTED,
 *                      M0_CLOVIS_OS_STABLE, M0_CLOVIS_OS_FAILED)) == 0
 */
int32_t m0_clovis_op_wait(struct m0_clovis_op *op, uint64_t bits, m0_time_t to);

/**
 * Asks the implementation to speed up progress of this operation toward
 * stability.
 *
 * The implementation is free to either honour this call by modifying various
 * internal caching and queuing policies to process the operation with less
 * delays, or to ignore this call altogether. This call may incur resource
 * under-utilisation and other overheads.
 *
 * @param op Operation to be kicked.
 *
 * @pre op != NULL
 * @pre op->op_sm.sm_state >= M0_CLOVIS_OS_INITIALISED
 */
void m0_clovis_op_kick(struct m0_clovis_op *op);

/**
 * Finalises a complete operation. The state machine will be moved to
 * M0_CLOVIS_OS_UNINITIALISED.
 *
 * @param op Operation being finalised.
 *
 * @pre op != NULL
 * @pre M0_IN(op->op_sm.sm_state, (M0_CLOVIS_OS_INITIALISED,
 *                                 M0_CLOVIS_OS_STABLE, M0_CLOVIS_OS_FAILED))
 */
void m0_clovis_op_fini(struct m0_clovis_op *op);

/**
 * Frees a complete operation, allocated by the implementation.
 *
 * @param op Operation being freed.
 *
 * @pre op != NULL
 * pre op->op_sm.sm_state == M0_CLOVIS_OS_UNINITIALISED
 */
void m0_clovis_op_free(struct m0_clovis_op *op);

void m0_clovis_container_init(struct m0_clovis_container *con,
			      struct m0_clovis_realm     *parent,
			      const struct m0_uint128    *id,
			      struct m0_clovis           *instance);
void m0_clovis_epoch_init    (struct m0_clovis_epoch     *epoch,
			      struct m0_clovis_realm     *parent,
			      const struct m0_uint128    *id);
void m0_clovis_dtx_init      (struct m0_clovis_dtx       *dtx,
			      struct m0_clovis_realm     *parent,
			      const struct m0_uint128    *id);

/**
 * Initialises a clovis object so that it can be created or deleted, or have
 * read, write, alloc and free operations executed on it.
 *
 * The size of data and parity buffer (m0_clovis_obj::ob_attr::oa_bshift) is
 * set to default value 'CLOVIS_DEFAULT_BUF_SHIFT'.
 *
 * @param obj The object to initialise.
 * @param parent The realm operations on this object will be part of.
 * @param id The identifier assigned by the application to this object.
 *
 * @pre obj != NULL
 * @pre parent != NULL
 * @pre id != NULL && m0_uint128_cmp(&M0_CLOVIS_ID_APP, id) < 0
 */
void m0_clovis_obj_init      (struct m0_clovis_obj       *obj,
			      struct m0_clovis_realm     *parent,
			      const struct m0_uint128    *id);
/**
 * Finalises an obj, leading to finilise entity and to free any additiona
 *  memory allocated to represent it.
 *
 * @param obj Pointer to the object to finalise.
 *
 * @pre obj != NULL
 */
void m0_clovis_obj_fini(struct m0_clovis_obj *obj);

/**
 * Initialises the index corresponding to a given object.
 *
 * Keys in this index are 64-bit block offsets (in BE representation, with
 * lexicographic ordering) and the values are battrs (and maybe data?) for the
 * block.
 *
 * The index structure, initialised by this function, provides access to object
 * data through clovis index interface.
 *
 * @post m0_uint128_eq(&idx->in_entity.en_id, &obj->ob_entity.en_id)
 */
void m0_clovis_obj_idx_init(struct m0_clovis_idx       *idx,
			    const struct m0_clovis_obj *obj);

/**
 * Initialises object operation.
 *
 * @param obj Object the operation is targeted to.
 * @param opcode Operation code for the operation.
 * @param ext  Extents in the object, measured in blocks.
 * @param data Application buffers for the operation.
 * @param attr Application buffers for block attributes.
 * @param mask Attribute mask.
 * @param[out] op Pointer to the operation pointer. If the operation pointer is
 *	       NULL, clovis will allocate one. Otherwise, clovis will check
 *	       the operation and make sure it is reusable for this operation.
 *
 * @remarks "data" defines buffers from which data are read on WRITE and
 * written to on READ.
 *
 * @remarks "attr" and "mask" together define which block attributes are read
 * or written.
 *
 * @remarks The application can provide a pre-allocated operation. Otherwise,
 * a new operation is allocated by this entry point, which eventually must be
 * explicitely freed by the app.
 *
 * @pre obj != NULL
 * @pre M0_IN(opcode, (M0_CLOVIS_OC_READ, M0_CLOVIS_OC_WRITE,
 *                     M0_CLOVIS_OC_ALLOC, M0_CLOVIS_OC_FREE))
 * @pre ext != NULL
 * @pre obj->ob_attr.oa_bshift >= CLOVIS_MIN_BUF_SHIFT
 * @pre m0_vec_count(&ext->iv_vec) % (1ULL << obj->ob_attr.oa_bshift) == 0
 * @pre op != NULL
 * @pre ergo(M0_IN(opcode, (M0_CLOVIS_OC_READ, M0_CLOVIS_OC_WRITE)),
 *           data != NULL && attr != NULL &&
 *           m0_vec_count(&ext->iv_vec) == m0_vec_count(&data->ov_vec) &&
 *           m0_vec_count(&attr->ov_vec) == 8 * m0_no_of_bits_set(mask) *
 *                       (m0_vec_count(&ext->iv_vec) >> obj->ob_attr.oa_bshift)
 * @pre ergo(M0_IN(opcode, (M0_CLOVIS_OC_ALLOC, M0_CLOVIS_OC_FREE)),
 *           data == NULL && attr == NULL && mask == 0)
 *
 * @post *op != NULL && *op->op_code == opcode &&
 *       *op->op_sm.sm_state == M0_CLOVIS_OS_INITIALISED)
 */
void m0_clovis_obj_op(struct m0_clovis_obj       *obj,
		      enum m0_clovis_obj_opcode   opcode,
		      struct m0_indexvec         *ext,
		      struct m0_bufvec           *data,
		      struct m0_bufvec           *attr,
		      uint64_t                    mask,
		      struct m0_clovis_op       **op);

/**
 * Initialises clovis index in a given realm.
 *
 * Notes for M0_CLOVIS_IDX_DIX index service type:
 * 'id' should be a valid mero fid of type 'x' (see m0_dix_fid_type). Zero fid
 * container is reserved for distributed meta-indices and shouldn't be used for
 * user indices, i.e. indices with M0_FID_TINIT('x', 0, *) fids are reserved.
 *
 * @code
 * struct m0_fid fid = M0_FID_TINIT('x', 1, 1);
 *
 * m0_clovis_idx_init(&idx, &realm, (struct m0_uint128 *)&fid);
 * @endcode
 *
 * Non-distributed indices (having fid type 'i') are going to be supported in
 * future.
 */
void m0_clovis_idx_init(struct m0_clovis_idx    *idx,
			struct m0_clovis_realm  *parent,
			const struct m0_uint128 *id);

void m0_clovis_idx_fini(struct m0_clovis_idx *idx);

/**
 * Initialises an index operation.
 *
 * For M0_CLOVIS_IC_NEXT operation arguments should be as follows:
 * - 'keys' buffer vector first element should contain a starting key and other
 *   elements should be set to NULL. Buffer vector size indicates number of
 *   records to return.
 *   Starting key can be NULL. In this case starting key is treated as the
 *   smallest possible key of the index. If starting key doesn't exist in the
 *   index, then retrieved records will start with the smallest key following
 *   the starting key. Otherwise, a record corresponding to the starting key
 *   will be included in a result.
 * - 'vals' vector should be at least of the same size as 'keys' and should
 *   contain NULLs. After successful operation completion retrieved index
 *   records are stored in 'keys' and 'vals' buffer vectors. If some error
 *   occurred during i-th index record retrieval then rcs[i] != 0. -ENOENT error
 *   means that there are no more records to return.
 *
 * For M0_CLOVIS_IC_GET operation arguments should be as follows:
 * - 'keys' buffer vector should contain keys for records being requested.
 *   At least one key should be specified and no NULL keys are allowed.
 * - 'vals' vector should be at least of the same size as 'keys' and should
 *   contain NULLs. After successful operation completion retrieved record
 *   values are stored in 'vals' buffer vector. If some value retrieval has
 *   failed, then corresponding element in 'rcs' array != 0.
 *
 * 'rcs' holds array of per-item return codes for the operation. It should be
 * allocated by user with a size of at least 'keys->ov_vec.v_nr' elements. For
 * example, 6 records with keys k0...k5 were requested through GET request with
 * k3 being absent in the index. After operation completion rcs[3] will be
 * -ENOENT and rcs[0,1,2,4,5] will be 0.
 *
 * Per-item return codes are more fine-grained than global operation return
 * code (op->op_sm.sm_rc). On operation completion the global return code
 * is set to negative value if it's impossible to process any item (invalid
 * index fid, lost RPC connection, etc.).
 * - If the operation global return code is 0, then user should check per-item
 *   return codes.
 * - If the operation global return code is not 0, then per-item return codes
 *   are undefined.
 *
 * 'rcs' argument is mandatory for all operations except M0_CLOVIS_IC_LOOKUP.
 *
 * @pre idx != NULL
 * @pre M0_IN(opcode, (M0_CLOVIS_IC_LOOKUP, M0_CLOVIS_IC_LIST,
 *                     M0_CLOVIS_IC_GET, M0_CLOVIS_IC_PUT,
 *                     M0_CLOVIS_IC_DEL, M0_CLOVIS_IC_NEXT))
 * @pre ergo(*op != NULL, *op->op_size >= sizeof **op)
 * @pre ergo(opcode == M0_CLOVIS_IC_LOOKUP, rcs != NULL)
 * @pre ergo(opcode != M0_CLOVIS_IC_LOOKUP, keys != NULL)
 * @pre M0_IN(opcode, (M0_CLOVIS_IC_DEL,
 *                     M0_CLOVIS_IC_LOOKUP,
 *                     M0_CLOVIS_IC_LIST)) == (vals == NULL)
 * @pre ergo(opcode == M0_CLOVIS_IC_LIST,
 *           m0_forall(i, keys->ov_vec.v_nr,
 *                     keys->ov_vec.v_count[i] == sizeof(struct m0_uint128)))
 * @pre ergo(opcode == M0_CLOVIS_IC_GET, keys->ov_vec.v_nr != 0)
 * @pre ergo(opcode == M0_CLOVIS_IC_GET,
 *           m0_forall(i, keys->ov_vec.v_nr, keys->ov_buf[i] != NULL))
 * @pre ergo(vals != NULL, keys->ov_vec.v_nr == vals->ov_vec.v_nr)
 *
 * @post ergo(result == 0, *op != NULL && *op->op_code == opcode &&
 *                         *op->op_sm.sm_state == M0_CLOVIS_OS_INITIALISED)
 */
/**
 * @todo For now 'rcs' may be NULL if index backend is not Mero KVS
 * and operation code is not M0_CLOVIS_IC_GET.
 * All backends should be updated to fill 'rcs' for all operation codes.
 */
int m0_clovis_idx_op(struct m0_clovis_idx       *idx,
		     enum m0_clovis_idx_opcode   opcode,
		     struct m0_bufvec           *keys,
		     struct m0_bufvec           *vals,
		     int32_t                    *rcs,
		     struct m0_clovis_op       **op);

void m0_clovis_realm_create(struct m0_clovis_realm    *realm,
			    uint64_t wcount, uint64_t rcount,
			    struct m0_clovis_op **op);

void m0_clovis_realm_open(struct m0_clovis_realm   *realm,
			  uint64_t wcount, uint64_t rcount,
			  struct m0_clovis_op **op);

void m0_clovis_realm_close(struct m0_clovis_realm   *realm,
			   uint64_t wcount, uint64_t rcount,
			   struct m0_clovis_op **op);

/**
 * Sets an operation to create or delete an entity.
 *
 * @param entity In-memory representation of the entity that is to be created.
 * @param[out] op Pointer to the operation. The operation can be pre-allocated
 * by the application. Otherwise, this entry point will allocate it.
 * @return 0 for success, (*op)->op_sm.sm_rc otherwise.
 *
 * @pre entity != NULL
 * @pre op != NULL
 */
/**@{*/
int m0_clovis_entity_create(struct m0_clovis_entity *entity,
			    struct m0_clovis_op **op);
int m0_clovis_entity_delete(struct m0_clovis_entity *entity,
			    struct m0_clovis_op **op);
/**@}*/

/**
 * Finalises an entity, freeing any additional memory allocated to represent it.
 *
 * @param entity Pointer to the entity to finalise.
 *
 * @pre entity != NULL
 * @pre entity->en_sm.sm_state == M0_CLOVIS_ES_INIT
 */
void m0_clovis_entity_fini(struct m0_clovis_entity *entity);

/**
 * Returns the maximum size a clovis operation is expected to be.
 * If pre-allocating 'struct m0_clovis_op's, allocations smaller than this
 * size may be rejected with EMSGSIZE
 */
size_t m0_clovis_op_maxsize(void);

/**
 * Initialises state machine types et al.
 *
 * @param m0c Where to store the allocated instance.
 * @param conf clovis configuration parameters.
 * @param init_m0 Indicate whether or not Mero needs to be initilised.
 * @return 0 for success, anything else for an error.
 *
 * @pre m0c must point to a NULL struct m0_clovis *.
 * @pre local_ep must not be NULL or the empty string.
 */
int m0_clovis_init(struct m0_clovis **m0c,
		   struct m0_clovis_config *conf,
		   bool init_m0);

/**
 * Finalises clovis, finalise state machine group et al.
 *
 * @pre *m0c may not be NULL, it will be changed to NULL by this call.
 */
void m0_clovis_fini(struct m0_clovis **m0c, bool fini_m0);

/**
 * clovis osync entry point [Experimental]
 *
 * @param obj The object is going to be sync'ed.
 * @return 0 for success, anything else for an error.
 */
int m0_clovis_obj_sync(struct m0_clovis_obj *obj);

/**
 * Clovis sync instance entry point [Experimental]
 *
 * @param obj The object is going to be sync'ed.
 * @param wait Ask clovis to wait till pending tx's are done if set to be ture.
 * @return 0 for success, anything else for an error.
 */
int m0_clovis_sync(struct m0_clovis *m0c, int wait);

/**
 * Entity type is valid helper.
 *
 * @param ent An entity.
 * @return Whether the entities type is valid.
 */
M0_INTERNAL int m0_entity_type_is_valid(struct m0_clovis_entity *ent);

/** @} end of clovis group */

#endif /* __MERO_CLOVIS_CLOVIS_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
