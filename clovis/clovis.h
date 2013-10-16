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
 * Original creation date: 07-Feb-2013
 */


#pragma once

#ifndef __MERO_CLOVIS_CLOVIS_H__
#define __MERO_CLOVIS_CLOVIS_H__

#include "lib/vec.h"
#include "lib/types.h"
#include "sm/sm.h"

/**
 * @defgroup clovis
 *
 * Overview
 * --------
 *
 * Clovis is *the* interface exported by Mero for use by Mero
 * applications. Examples of Mero applications are:
 *
 *     - Mero file system client (m0t1fs);
 *
 *     - Lustre osd-mero module;
 *
 *     - Mero-based block device.
 *
 * Note that FDMI plugins use a separate interface.
 *
 * Clovis provides the following abstractions:
 *
 *     - object (m0_clovis_obj) is an array of fixed-size blocks;
 *
 *     - index (m0_clovis_idx) is a key-value store;
 *
 *     - scope (m0_clovis_scope) is a spatial and temporal part of system with a
 *       prescribed access discipline. Objects, indices and operations live in
 *       scopes;
 *
 *     - operation (m0_clovis_op) is a process of querying or updating system
 *       state;
 *
 * Scopes are further sub-divided in:
 *
 *     - transaction (m0_clovis_dtx) is a collection of operations atomic in the
 *       face of failures;
 *
 *     - epoch (m0_clovis_epoch) is...
 *
 *     - container (m0_clovis_container) is a collection of objects used by a
 *       particular application or group of applications.
 *
 * Object, index and scope are sub-types of entity (m0_clovis_entity). An entity
 * exists in some scope and has a 128-bit identifier, unique within all entities
 * of the same type in the scope. Identifier management is up to the
 * application, except for the single reserved identifier for "uber scope"
 * (M0_CLOVIS_UBER_SCOPE), representing the root of scope hierarchy.
 *
 * All clovis entry points are non-blocking. To perform a potentially lengthy
 * activity, that might involve network communication (for example, read from an
 * object), clovis entry point (m0_clovis_obj_op() in the case of object read),
 * sets up an operation (m0_clovis_ops) structure containing the parameters of
 * the activity and immediately returns to the caller. The caller must
 * explicitly launch a set of previously prepared operations by calling
 * m0_clovis_op_launch(). Separation of setup and launch provides for more
 * efficient network communication, where multiple operations are accumulated in
 * the same RPC message.
 *
 * The user can track the state of the operation either synchronously, by
 * waiting until the operation reaches a particular state, or asynchronously by
 * supplying (m0_clovis_op_setup()) a call-back to be called when the operation
 * reaches a particular state.
 *
 * An entity embeds an operation (m0_clovis_entity::en_op) used by the common
 * entity operations: entity creation, deletion, opening and closing, see
 * m0_clovis_entity_create(), m0_clovis_entity_delete(),
 * m0_clovis_entity_open(), m0_clovis_entity_close().
 *
 * Operation structures for other operations (object read-write, index get-put)
 * are allocated by the appropriate entry points, see "op" parameter of
 * m0_clovis_obj_op() for an example.
 *
 * Operation errors are returned through m0_clovis_op::op_sm::sm_rc.
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
 *          +---- scope () [abstract, no constructor]
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
 *        op (init, wait, fini) [has private sub-types in clovis_private.h]
 *
 * @endverbatim
 *
 * Operations, common for all entity types are implemented at the entity level:
 * m0_clovis_entity_create(), m0_clovis_entity_delete(),
 * m0_clovis_entity_open(), m0_clovis_entity_close(), m0_clovis_entity_fini().
 *
 * A typical usage would involve initialisation of a concrete entity (e.g.,
 * object), execution of some generic operations and then of some concrete
 * operations, for example:
 *
 * @code
 * m0_clovis_obj  o;
 * m0_clovis_op   ops[2];
 *
 * // initialise object in-memory structure
 * m0_clovis_obj_init(&o, &container, &id);
 *
 * // initiate object creation
 * m0_clovis_entity_create(&o.ob_entity);
 *
 * // initiate write data in the object
 * result = m0_clovis_obj_op(&o, M0_CLOVIS_OC_WRITE, ..., &ops[1]);
 *
 * // launch both operations (creation and write)
 * ops[0] = &o.ob_entity.en_op;
 * m0_clovis_op_launch(ops, ARRAY_SIZE(ops));
 *
 * // wait until creation completes
 * result = m0_clovis_op_wait(op[0]);
 *
 * // wait until write completes
 * result = m0_clovis_op_wait(op[1]);
 *
 * // finalise the object
 * m0_clovis_entity_fini(&o.ob_entity);
 * @endcode
 *
 *
 * Ownership
 * ---------
 *
 * Clovis data structures (scopes, objects and indices) are allocated by the
 * application. The application may free a structure after completing the
 * corresponding finalisation call.
 *
 * An operation structure allocated by the clovis implementation is freed by the
 * clovis after the operation completes.
 *
 * Data blocks used by scatter-gather-scatter lists and key-value records are
 * allocated by the application. For read-only operations M0_CLOVIS_OC_READ,
 * M0_CLOVIS_IC_GET and M0_CLOVIS_IC_NEXT) the application may free the data
 * blocks as soon as the operation completes or fails. For updating operations,
 * the data blocks may be freed as soon as the scope of which the operation is
 * part, becomes stable.
 *
 * Concurrency
 * -----------
 *
 * Clovis implementation guarantees that concurrent calls to the same index are
 * linearizable.
 *
 * All other concurrency control, including ordering of reads and writes to a
 * clovis object, and distributed transaction serializability, is up to the
 * application.
 *
 * @see https://docs.google.com/a/xyratex.com/document/d/sHUAUkByacMNkDBRAd8-AbA
 *
 * @{
 */

enum m0_clovis_entity_type {
	M0_CLOVIS_ET_SCOPE,
	M0_CLOVIS_ET_OBJ,
	M0_CLOVIS_ET_IDX,
};

struct m0_clovis_op {
	unsigned int                   op_code;
	struct m0_sm                   op_sm;
	const struct m0_clovis_op_ops *op_ops;
	struct m0_clovis_entity       *op_entity;
	m0_time_t                      op_linger; /* a town in Luxembourg. */
};

struct m0_clovis_entity {
	enum m0_clovis_entity_type en_type;
	struct m0_uint128          en_id;
	struct m0_clovis_scope    *en_scope;
	struct m0_sm               en_sm;
	struct m0_clovis_op        en_op;
};

struct m0_clovis_obj {
	struct m0_clovis_entity ob_entity;
};

struct m0_clovis_idx {
	struct m0_clovis_entity in_entity;
};

struct m0_clovis_scope {
	struct m0_clovis_entity   sc_entity;
	enum m0_clovis_scope_type sc_type,
};

struct m0_clovis_container {
	struct m0_clovis_scope co_scope;
};

struct m0_clovis_epoch {
	struct m0_clovis_scope ep_scope;
};

struct m0_clovis_dtx {
	struct m0_clovis_scope dt_scope;
};

struct m0_clovis_op_ops {
	void (*oop_replied)(struct m0_clovis_op *op);
	void (*oop_aborted)(struct m0_clovis_op *op);
	void (*oop_done)   (struct m0_clovis_op *op);
};

extern const struct m0_uint128 M0_CLOVIS_UBER_SCOPE;

void m0_clovis_op_setup(struct m0_clovis_op *op,
			const struct m0_clovis_op_ops *ops,
			m0_time_t linger);
void m0_clovis_op_launch(struct m0_clovis_op **op, uint32_t nr);
int32_t m0_clovis_wait(struct m0_clovis_op *op);

void m0_clovis_container_init(struct m0_clovis_obj    *obj,
			      struct m0_clovis_scope  *parent,
			      const struct m0_uint128 *id);
void m0_clovis_epoch_init(struct m0_clovis_obj    *obj,
			  struct m0_clovis_scope  *parent,
			  const struct m0_uint128 *id);
void m0_clovis_dtx_init(struct m0_clovis_obj    *obj,
			struct m0_clovis_scope  *parent,
			const struct m0_uint128 *id);

void m0_clovis_obj_init(struct m0_clovis_obj    *obj,
			struct m0_clovis_scope  *parent,
			const struct m0_uint128 *id);

enum m0_clovis_obj_opcode {
	M0_CLOVIS_OC_READ,
	M0_CLOVIS_OC_WRITE,
	M0_CLOVIS_OC_ALLOC,
	M0_CLOVIS_OC_FREE
};

int m0_clovis_obj_op(struct m0_clovis_obj       *obj,
		     enum m0_clovis_obj_opcode   opcode,
		     struct m0_indexvec         *ext,
		     struct m0_bufvec           *data,
		     struct m0_bufvec           *attr,
		     struct uint64_t             attr_mask,
		     struct m0_clovis_op       **op);

void m0_clovis_idx_init(struct m0_clovis_idx    *idx,
			struct m0_clovis_scope  *parent,
			const struct m0_uint128 *id);

enum m0_clovis_idx_opcode {
	M0_CLOVIS_IC_GET,
	M0_CLOVIS_IC_PUT,
	M0_CLOVIS_IC_NEXT,
};

int m0_clovis_idx_op(struct m0_clovis_idx       *idx,
		     enum m0_clovis_idx_opcode   opcode,
		     struct m0_bufvec           *key,
		     struct m0_bufvec           *val,
		     struct m0_bufvec           *chk,
		     struct m0_clovis_op       **op);

enum m0_clovis_scope_type {
	M0_CLOVIS_ST_CONTAINER,
	M0_CLOVIS_ST_EPOCH,
	M0_CLOVIS_ST_DTX
};

void m0_clovis_scope_create(struct m0_clovis_scope    *scope,
			    struct m0_clovis_scope_op *op,
			    struct m0_clovis_scope    *parent,
			    struct m0_uint128          id,
			    enum m0_clovis_scope_type  stype,
			    uint64_t wcount, uint64_t rcount);

void m0_clovis_scope_open(struct m0_clovis_scope   *scope,
			  struct m0_clovis_op      *op,
			  struct m0_clovis_scope   *parent,
			  struct m0_uint128         id);

void m0_clovis_scope_close(struct m0_clovis_scope   *scope,
			   struct m0_clovis_op      *op,
			   uint64_t wcount, uint64_t rcount);

void m0_clovis_entity_create(struct m0_clovis_entity *entity);
void m0_clovis_entity_delete(struct m0_clovis_entity *entity);
void m0_clovis_entity_open  (struct m0_clovis_entity *entity);
void m0_clovis_entity_close (struct m0_clovis_entity *entity);
void m0_clovis_entity_fini  (struct m0_clovis_entity *entity);


/* import */
#include "lib/types.h"
#include "lib/vec.h"
#include "lib/buf.h"
#include "sm/sm.h"
#include "dtm/dtm.h"

/* export */
struct m0_clovis_dom;
struct m0_clovis_dtx;
struct m0_clovis_attr;
struct m0_clovis_obj;
struct m0_clovis_obj_attr;
struct m0_clovis_bag;
struct m0_clovis_bag_attr;
struct m0_clovis_id;
struct m0_clovis_sgsl;
struct m0_clovis_op;
struct m0_clovis_rec;

/**
 * Unique identifier for various clovis entities.
 *
 * Domains, objects and containers have separate identifier
 * name-spaces. Identifier allocation, assignment, recycling and reuse are up to
 * the application, except for a pre-defined M0_CLOVIS_DOM0_ID identifier in
 * the domain name-space.
 */
struct m0_clovis_id {
	struct m0_uint128 cid_128;
};

/**
 * Supported checksum algorithms.
 */
enum m0_clovis_chksum {
	M0_CCHK_NONE,
	M0_CCHK_NR
};

/**
 * Attributes common for objects (m0_clovis_obj_attr), domains and containers
 * (m0_clovis_bag_attr).
 *
 * These attributed are provided by the application when a new object is created
 * (m0_clovis_{obj,bag,domain}_create()) and returned by the implementation when
 * an existing object is opened (m0_clovis_{bag,obj,domain}_open()).
 */
struct m0_clovis_attr {
	enum m0_clovis_chksum cla_chksum_algorithm;
};

/**
 * m0_clovis_common contains state common for domains, objects and containers.
 */
struct m0_clovis_common {
	/**
	 * State machine. Possible state are taken from enum m0_clovis_state.
	 */
	struct m0_sm          com_sm;
	/**
	 * Identifier of this entity.
	 */
	struct m0_clovis_is   com_id;
	struct m0_clovis_attr com_attr;
};

/**
 * Possible entity states, where an entity is domain, object or container.
 *
 * @verbatim
 *
 *                  create
 *        CREATING<--------+
 *            |            |
 *            |        	   |
 *            |        	   |
 *            |        	   |
 *            +---------->INIT<----------------------CLOSING
 *            |        	   | |                           ^
 *            |            | |                           |
 *            |            | |                           | close
 *            |            | |                           |
 *        DELETING<--------+ +-------->OPENING-------->ACTIVE
 *                  delete      open
 *
 * @endverbatim
 *
 */
enum m0_clovis_state {
	/**
	 * Entity has been initialised.
	 *
	 * @see m0_clovis_dom_init(), m0_clovis_obj_init(), m0_clovis_bag_init().
	 */
	M0_CS_INIT = 1,
	/**
	 * Entity is being opened.
	 *
	 * @see m0_clovis_dom_open(), m0_clovis_obj_open(), m0_clovis_bag_open().
	 */
	M0_CS_OPENING,
	/**
	 * Not previously existing entity is being created.
	 *
	 * @see m0_clovis_dom_create(), m0_clovis_obj_create().
	 * @see m0_clovis_bag_create().
	 */
	M0_CS_CREATING,
	/**
	 * Existing entity is being deleted.
	 *
	 * @see m0_clovis_dom_delete(), m0_clovis_obj_delete().
	 * @see m0_clovis_bag_delete().
	 */
	M0_CS_DELETING,
	/**
	 * Opened entity is being closed.
	 *
	 * @see m0_clovis_dom_close(), m0_clovis_obj_close().
	 * @see m0_clovis_bag_close().
	 */
	M0_CS_CLOSING,
	/**
	 * Entity has been opened and not yet closed. Operations can be executed
	 * against the entity.
	 */
	M0_CS_ACTIVE,
	/**
	 * State transition failed. The entity is invalid and cannot be
	 * used. m0_clovis_common::com_sm::sm_rc contains the error code.
	 */
	M0_CS_FAILED
};

/**
 * Supported domain types.
 */
enum m0_clovis_dom_type {
	/**
	 * Exclusively owned domain.
	 *
	 * Such domain is, at any given moment, accessed by at most one
	 * application. This application is called domain owner.
	 *
	 * It is up to the application to guaranatee that when an exclusively
	 * owned domain is opened (m0_clovis_dom_open()), the previous user
	 * of the clovis interface that opened the domain either successfully
	 * completed the m0_clovis_dom_close() call or is somehow guaranteed
	 * to make no more calls against this domain (the latter case typically
	 * means that the previous user is known to be dead or was STONITHed).
	 */
	M0_CLOVIS_DOM_TYPE_EXCL = 1,
	M0_CLOVIS_DOM_TYPE_NR
};

/**
 * Clovis domain is a collection of resources with certain guaranteed
 * fault-tolerance properties and certain required restrictions on access
 * patterns.
 *
 * Objects (m0_clovis_obj) and containers (m0_clovis_bag) are externally visible
 * domain resources. Internally, clovis implementation manages other resources:
 * free storage space, layouts, &c.
 */
struct m0_clovis_dom {
	struct m0_clovis_common cdo_com;
	enum m0_clovis_dom_type cdo_type;
};

/**
 * A distributed transaction is a group of operations (m0_clovis_op) which are
 * guaranteed to survive a failure (from a certain class of failures)
 * atomically.
 *
 * @see https://docs.google.com/a/xyratex.com/document/d/1RacseZooNnfbKiaD-s5rmTpLlpw_QlPomAX9iH4PlfI
 */
struct m0_clovis_dtx {
	struct m0_dtx cdt_dx;
};

/**
 * An object is an array of data blocks of equal size.
 *
 * Blocks can be read and written. An object has no meta-data, specifically, it
 * has no size.
 *
 * An object, optionally, supports data-integrity checksums that can be
 * calculated and verified by the application.
 */
struct m0_clovis_obj {
	struct m0_clovis_common   cob_com;
	struct m0_clovis_obj_attr cob_attr;
};

/**
 * Object attributes.
 *
 * These attributed are provided by the application when a new object is created
 * (m0_clovis_obj_create()) and returned by the implementation when an existing
 * object is opened (m0_clovis_obj_open()).
 */
struct m0_clovis_obj_attr {
	/**
	 * Binary logarithm of the block size.
	 *
	 * Block size is (1 << coa_bshift).
	 */
	uint32_t coa_bshift;
};

/**
 * A container is an ordered key-value store.
 *
 * A record (m0_clovis_rec) is a key-value pair. A new record can be inserted in
 * a container, record with a given key can be looked for, updated or deleted.
 *
 * A container can be iterated starting from a given key. Keys are ordered in
 * the lexicographical order of their bit-representations.
 */
struct m0_clovis_bag {
	struct m0_clovis_common   cba_com;
	struct m0_clovis_bag_attr cba_attr;
};

struct m0_clovis_bag_attr {
};

/**
 * Data buffer.
 *
 * Data buffers are used for:
 *
 *     - application-supplied data which are to be written to an object;
 *
 *     - space for implementation-returned data read from an object;
 *
 *     - application-supplied key and value to be inserted in a container;
 *
 *     - key to be looked up, updated or deleted from a container;
 *
 *     - implementation-returned value of a record from a container;
 *
 *     - implementation-returned next key-value record in a container.
 */
struct m0_clovis_data {
	/** Vector of buffers. */
	struct m0_bufvec cda_buf;
	/** Vector of checksum buffers. If the vector has size 0, the
	    application is not interested in checksums. */
	struct m0_bufvec cda_chk;
};

/**
 * Scatter-gather-scatter list used by object operations.
 */
struct m0_clovis_sgsl {
	/**
	 * Vector of extents in the object offset space.
	 */
	struct m0_indexvec    csg_ext;
	/**
	 * Data buffer of the same size as total extents length.
	 */
	struct m0_clovis_data csg_buf;
};

/**
 * State of clovis operation (m0_clovis_op).
 *
 * @verbatim
 *                    (*)
 *                     |
 *                     | m0_clovis_op_init()
 *                     |
 *                     V
 *                   INIT
 *                     |
 *                     | m0_clovis_obj_op(), m0_clovis_bag_op()
 *                     |
 *                     V
 *   FAILED<-------ONGOING<-------+
 *     |               |          |
 *     |               |          | m0_clovis_cur_next()
 *     |               |          |
 *     |               V          |
 *     |           COMPLETE-------+
 *     |               |
 *     |               | m0_clovis_op_fini()
 *     |               |
 *     |               V
 *     +------------->(+)
 *
 * @endverbatim
 *
 */
enum m0_clovis_op_state {
	M0_CO_INIT = 1,
	M0_CO_ONGOING,
	M0_CO_FAILED,
	M0_CO_COMPLETE
};

/**
 * Attributes common for object and container operations.
 */
struct m0_clovis_op {
	/**
	 * Operation type is taken from one of enums, defined below:
	 * m0_clovis_obj_op_type or m0_clovis_bag_op_type.
	 */
	unsigned                 co_type;
	/** State machine tracking operation progress. */
	struct m0_sm             co_sm;
	/** Object or container the operation is against. */
	struct m0_clovis_common *co_target;
	/**
	 * Transaction this operation is part of. This must be NULL for a
	 * read-only operation (M0_COOT_READ, M0_CBOT_LOOKUP and M0_CBOT_CURSOR)
	 * and must be non-NULL for an updating operaiton (any other).
	 */
	struct m0_clovis_dtx    *co_dx;
};

/**
 * Object operation types.
 */
enum m0_clovis_obj_op_type {
	/**
	 * Read data from the extents specified by
	 * m0_clovis_obj_op::cio_data::csg_ext and place them in the buffers
	 * specified by m0_clovis_obj_op::cio_data::csg_buf.
	 *
	 */
	M0_COOT_READ,
	/**
	 * Take data from the buffers specified by
	 * m0_clovis_obj_op::cio_data::csg_buf and write them to the extents
	 * specified by m0_clovis_obj_op::cio_data::csg_ext.
	 */
	M0_COOT_WRITE,
	/**
	 * "Free" operation deallocates data blocks from the object. It
	 * generalises truncate and punch operations. Only extent vector
	 * (m0_clovis_obj_op::cio_data::csg_ext) is used by this operation. Data
	 * buffer (m0_clovis_obj_op::cio_data::csg_buf) must have size 0.
	 *
	 * Subsequent reads from deallocated object extents return zeroes.
	 */
	M0_COOT_FREE,
	/**
	 * "Alloc" operation pre-allocates storage space for the specified
	 * extents in object offset space. No change for already allocated parts
	 * of the object. The requirements on m0_clovis_obj_op::cio_data are the
	 * same as in M0_COOT_FREE.
	 *
	 * The exact meaning of "pre-allocation" is implementation dependent.
	 */
	M0_COOT_ALLOC
};

struct m0_clovis_obj_op {
	struct m0_clovis_op         cio_op;
	const struct m0_clovis_sgsl cio_data;
};

/**
 * Container operation types.
 */
enum m0_clovis_bag_op_type {
	/**
	 * Lookup a record with a given key.
	 */
	M0_CBOT_LOOKUP,
	/**
	 * Insert a given record in the container.
	 */
	M0_CBOT_INSERT,
	/**
	 * Delete the record with a given key.
	 */
	M0_CBOT_DELETE,
	/**
	 * Replace the value associated with the given key.
	 */
	M0_CBOT_UPDATE,
	/**
	 * Start a cursor and position it on the given key.
	 */
	M0_CBOT_CURSOR
};

/**
 * Key-value record.
 */
struct m0_clovis_rec {
	struct m0_clovis_data cre_key;
	struct m0_clovis_data cre_val;
};

/**
 * Container operation.
 */
struct m0_clovis_bag_op {
	struct m0_clovis_op  cbo_op;
	struct m0_clovis_rec cbo_rec;
};

/**
 * Pre-defined identifier of domain0, created by the implementation.
 *
 * Application may not use this identifier for the domains created by the
 * application. domain0 should be used only to start transactions used to create
 * other domains.
 */
M0_EXTERN const struct m0_clovis_id M0_CLOVIS_DOM0_ID;

void m0_clovis_dom_init  (struct m0_clovis_dom *dom,
			  enum m0_clovis_dom_type type,
			  const struct m0_clovis_id *id);
void m0_clovis_dom_fini  (struct m0_clovis_dom *dom);
void m0_clovis_dom_create(struct m0_clovis_dom *dom, struct m0_clovis_dtx *dx);
void m0_clovis_dom_open  (struct m0_clovis_dom *dom);
void m0_clovis_dom_close (struct m0_clovis_dom *dom);
void m0_clovis_dom_delete(struct m0_clovis_dom *dom, struct m0_clovis_dtx *dx);

void m0_clovis_dtx_init  (struct m0_clovis_dtx *dx);
void m0_clovis_dtx_fini  (struct m0_clovis_dtx *dx);
void m0_clovis_dtx_open  (struct m0_clovis_dtx *dx, struct m0_clovis_dom *dom);
void m0_clovis_dtx_close (struct m0_clovis_dtx *dx);
void m0_clovis_dtx_add   (struct m0_clovis_dtx *dx, struct m0_clovis_op *op);
void m0_clovis_dtx_force (struct m0_clovis_dtx *dx);

void m0_clovis_op_init   (struct m0_clovis_obj_op *op, unsigned otype,
			  struct m0_clovis_obj *obj, struct m0_clovis_dtx *dx);
void m0_clovis_op_fini   (struct m0_clovis_obj_op *op);

void m0_clovis_obj_init  (struct m0_clovis_obj *obj,
			  const struct m0_clovis_id *id);
void m0_clovis_obj_fini  (struct m0_clovis_obj *obj);
void m0_clovis_obj_create(struct m0_clovis_obj *obj, struct m0_clovis_dtx *dx);
void m0_clovis_obj_open  (struct m0_clovis_obj *obj);
void m0_clovis_obj_close (struct m0_clovis_obj *obj);
void m0_clovis_obj_delete(struct m0_clovis_obj *obj, struct m0_clovis_dtx *dx);

void m0_clovis_obj_op    (struct m0_clovis_obj_op *op);

void m0_clovis_bag_init  (struct m0_clovis_bag *bag,
			  const struct m0_clovis_id *id);
void m0_clovis_bag_fini  (struct m0_clovis_bag *bag);
void m0_clovis_bag_create(struct m0_clovis_bag *bag, struct m0_clovis_dtx *dx);
void m0_clovis_bag_open  (struct m0_clovis_bag *bag);
void m0_clovis_bag_close (struct m0_clovis_bag *bag);
void m0_clovis_bag_delete(struct m0_clovis_bag *bag, struct m0_clovis_dtx *dx);

void m0_clovis_bag_op    (struct m0_clovis_bag_op *op);
void m0_clovis_cur_next  (struct m0_clovis_bag_op *op);

bool m0_clovis_id_is_set (const struct m0_clovis_is *id);
bool m0_clovis_id_eq     (const struct m0_clovis_is *id0,
			  const struct m0_clovis_is *id1);

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
