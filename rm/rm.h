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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 04/28/2011
 */

#pragma once

#ifndef __COLIBRI_RM_RM_H__
#define __COLIBRI_RM_RM_H__

#include "lib/tlist.h"
#include "lib/cookie.h"
#include "net/net.h"
#include "sm/sm.h"

/**
 * @defgroup rm Resource management
 *
 * A resource is an entity in Colibri for which a notion of ownership can be
 * well-defined. See the HLD referenced below for more details.
 *
 * In Colibri almost everything is a resource, except for the low-level
 * types that are used to implement the resource framework.
 *
 * Resource management is split into two parts:
 *
 *     - generic functionality, implemented by the code in rm/ directory and
 *
 *     - resource type specific functionality.
 *
 * These parts interact through the operation vectors (c2_rm_resource_ops,
 * c2_rm_resource_type_ops and c2_rm_right_ops) provided by a resource type
 * and called by the generic code. Type specific code, in turn, calls
 * generic entry-points described in the @b Resource type interface
 * section.
 *
 * In the documentation below, responsibilities of generic and type specific
 * parts of the resource manager are delineated.
 *
 * @b Overview
 *
 * A resource (c2_rm_resource) is associated with various file system entities:
 *
 *     - file meta-data. Rights to use this resource can be thought of as locks
 *       on file attributes that allow them to be cached or modified locally;
 *
 *     - file data. Rights to use this resource are extents in the file plus
 *       access mode bits (read, write);
 *
 *     - free storage space on a server (a "grant" in Lustre
 *       terminology). Right to use this resource is a reservation of a given
 *       number of bytes;
 *
 *     - quota;
 *
 *     - many more, see the HLD for examples.
 *
 * A resource owner (c2_rm_owner) represents a collection of rights to use a
 * particular resource.
 *
 * To use a resource, a user of the resource manager creates an incoming
 * resource request (c2_rm_incoming), that describes a wanted usage right
 * (c2_rm_right_get()). Sometimes the request can be fulfilled immediately,
 * sometimes it requires changes in the right ownership. In the latter case
 * outgoing requests are directed to the remote resource owners (which typically
 * means a network communication) to collect the wanted usage right at the
 * owner. When an outgoing request reaches its target remote domain, an incoming
 * request is created and processed (which in turn might result in sending
 * further outgoing requests). Eventually, a reply is received for the outgoing
 * request. When incoming request processing is complete, it "pins" the wanted
 * right. This right can be used until the incoming request structure is
 * destroyed (c2_rm_right_put()) and the pin is released.
 *
 * See the documentation for individual resource management data-types and
 * interfaces for more detailed description of their behaviour.
 *
 * @b Terminology.
 *
 * Various terms are used to described right ownership flow in a cluster.
 *
 * Owners of rights for a particular resource are arranged in a cluster-wide
 * hierarchy. This hierarchical arrangement depends on system structure (e.g.,
 * where devices are connected, how network topology looks like) and dynamic
 * system behaviour (how accesses to a resource are distributed).
 *
 * Originally, all rights on the resource belong to a single owner or a set of
 * owners, residing on some well-known servers. Proxy servers request and cache
 * rights from there. Lower level proxies and clients request rights in
 * turn. According to the order in this hierarchy, one distinguishes "upward"
 * and "downward" owners relative to a given one.
 *
 * In a given ownership transfer operation, a downward owner is "debtor" and
 * upward owner is "creditor". The right being transferred is called a "loan"
 * (note that this word is used only as a noun). When a right is transferred
 * from a creditor to a debtor, the latter "borrows" and the former "sub-lets"
 * the loan. When a right is transferred in the other direction, the creditor
 * "revokes" and debtor "returns" the loan.
 *
 * A debtor can voluntary return a loan. This is called a "cancel" operation.
 *
 * @b Concurrency control.
 *
 * Generic resource manager makes no assumptions about threading model used by
 * its callers. Generic resource data-structures and code are thread safe.
 *
 * 3 types of locks protect all generic resource manager states:
 *
 *     - per domain c2_rm_domain::rd_lock. This lock serialises addition and
 *       removal of resource types. Typically, it won't be contended much after
 *       the system start-up;
 *
 *     - per resource type c2_rm_resource_type::rt_lock. This lock is taken
 *       whenever a resource or a resource owner is created or
 *       destroyed. Typically, that would be when a file system object is
 *       accessed which is not in the cache;
 *
 *     - per resource owner c2_rm_owner::ro_lock. These locks protect a bulk of
 *       generic resource management state:
 *
 *           - lists of possessed, borrowed and sub-let usage rights;
 *
 *           - incoming requests and their state transitions;
 *
 *           - outgoing requests and their state transitions;
 *
 *           - pins (c2_rm_pin).
 *
 *       Owner lock is accessed (taken and released) at least once during
 *       processing of an incoming request. Main owner state machine logic
 *       (owner_balance()) is structured in a way that is easily adaptable to a
 *       finer grained logic.
 *
 * None of these locks are ever held while waiting for a network communication
 * to complete.
 *
 * Lock ordering: these locks do not nest.
 *
 * @b Liveness.
 *
 * None of the resource manager structures, except for c2_rm_resource, require
 * reference counting, because their liveness is strictly determined by the
 * liveness of an "owning" structure into which they are logically embedded.
 *
 * The resource structure (c2_rm_resource) can be shared between multiple
 * resource owners (c2_rm_owner) and its liveness is determined by the
 * reference counting (c2_rm_resource::r_ref).
 *
 * As in many other places in Colibri, liveness of "global" long-living
 * structures (c2_rm_domain, c2_rm_resource_type) is managed by the upper
 * layers which are responsible for determining when it is safe to finalise
 * the structures. Typically, an upper layer would achieve this by first
 * stopping and finalising all possible resource manager users.
 *
 * Similarly, a resource owner (c2_rm_owner) liveness is not explicitly
 * determined by the resource manager. It is up to the user to determine when
 * an owner (which can be associated with a file, or a client, or a similar
 * entity) is safe to be finalised.
 *
 * When a resource owner is finalised (ROS_FINALISING) it tears down the credit
 * network by revoking the loans it sublet to and by retuning the loans it
 * borrowed from other owners.
 *
 * @b Resource identification and location.
 *
 * @see c2_rm_remote
 *
 * @b Persistent state.
 *
 * @b Network protocol.
 *
 * @see https://docs.google.com/a/xyratex.com/Doc?docid=0AQaCw6YRYSVSZGZmMzV6NzJfN2NiNXM1dHF3&hl=en
 *
 * @{
*/

/* import */
struct c2_bufvec_cursor;

/* export */
struct c2_rm_domain;
struct c2_rm_resource;
struct c2_rm_resource_ops;
struct c2_rm_resource_type;
struct c2_rm_resource_type_ops;
struct c2_rm_owner;
struct c2_rm_remote;
struct c2_rm_loan;
struct c2_rm_group;
struct c2_rm_right;
struct c2_rm_right_ops;
struct c2_rm_incoming;
struct c2_rm_incoming_ops;
struct c2_rm_outgoing;
struct c2_rm_lease;

enum {
	C2_RM_RESOURCE_TYPE_ID_MAX     = 64,
	C2_RM_RESOURCE_TYPE_ID_INVALID = ~0
};

/**
 * Domain of resource management.
 *
 * All other resource manager data-structures (resource types, resources,
 * owners, rights, &c.) belong to some domain, directly or indirectly.
 *
 * Domains support multiple independent resource management services in the same
 * address space (user process or kernel). Each request handler and each client
 * kernel instance run a resource management service, but multiple request
 * handlers can co-exist in the same address space.
 */
struct c2_rm_domain {
	/**
	 * An array where resource types are registered. Protected by
	 * c2_rm_domain::rd_lock.
	 *
	 * @see c2_rm_resource_type::rt_id
	 */
	struct c2_rm_resource_type *rd_types[C2_RM_RESOURCE_TYPE_ID_MAX];
	struct c2_mutex             rd_lock;
};

/**
 * Represents a resource identity (i.e., a name). Multiple copies of the
 * same name may exist in different resource management domains, but no more
 * than a single copy per domain.
 *
 * c2_rm_resource is allocated and destroyed by the appropriate resource
 * type. An instance of c2_rm_resource would be typically embedded into a
 * larger resource type specific structure containing details of resource
 * identification.
 *
 * Generic code uses c2_rm_resource to efficiently compare resource
 * identities.
 */
struct c2_rm_resource {
	struct c2_rm_resource_type      *r_type;
	const struct c2_rm_resource_ops *r_ops;
	/**
	 * Linkage to a list of all resources of this type, hanging off
	 * c2_rm_resource_type::rt_resources.
	 */
	struct c2_tlink                  r_linkage;
	/**
	 * List of remote owners (linked through c2_rm_remote::rem_linkage) with
	 * which local owner with rights to this resource communicates.
	 */
	struct c2_tl                     r_remote;
	/**
	 * Active references to this resource from resource owners
	 * (c2_rm_resource::r_type) and from remote resource owners
	 * (c2_rm_remote::rem_resource) Protected by
	 * c2_rm_resource_type::rt_lock.
	 */
	uint32_t                         r_ref;
	uint64_t                         r_magix;
};

struct c2_rm_resource_ops {
	/**
	 * Called when a new right is allocated for the resource. The resource
	 * specific code should parse the right description stored in the
	 * buffer and fill c2_rm_right::ri_datum appropriately.
	 */
	int (*rop_right_decode)(struct c2_rm_resource *resource,
				struct c2_rm_right *right,
				struct c2_bufvec_cursor *cur);
	void (*rop_policy)(struct c2_rm_resource *resource,
			   struct c2_rm_incoming *in);
	/**
	 * Called to initialise a usage right for this resource.
	 * Sets up c2_rm_right::ri_ops.
	 */
	void (*rop_right_init)(struct c2_rm_resource *resource,
			       struct c2_rm_right *right);
};

/**
 * Resources are classified into disjoint types.
 *
 * Resource type determines how its instances interact with the resource
 * management generic core:
 *
 * - it determines how the resources of this type are named;
 *
 * - it determines how the resources of this type are located;
 *
 * - it determines what resource rights are defined on the resources of
 *   this type;
 *
 * - how rights are ordered;
 *
 * - how right conflicts are resolved.
 */
struct c2_rm_resource_type {
	const struct c2_rm_resource_type_ops *rt_ops;
	const char                           *rt_name;
	/**
	 * A resource type identifier, globally unique within a cluster, used
	 * to identify resource types on wire and storage.
	 *
	 * This identifier is used as an index in c2_rm_domain::rd_index.
	 *
	 * @todo currently this is assigned manually and centrally. In the
	 * future, resource types identifiers (as well as rpc item opcodes)
	 * will be assigned dynamically by a special service (and then
	 * announced to the clients). Such identifier name-spaces are
	 * resources themselves, so, welcome to a minefield of
	 * bootstrapping.
	 */
	uint64_t			      rt_id;
	struct c2_mutex			      rt_lock;
	/**
	 * List of all resources of this type. Protected by
	 * c2_rm_resource_type::rt_lock.
	 */
	struct c2_tl			      rt_resources;
	/**
	 * Active references to this resource type from resource instances
	 * (c2_rm_owner::ro_resource). Protected by
	 * c2_rm_resource_type::rt_lock.
	 */
	uint32_t			      rt_nr_resources;
	/**
	 * Domain this resource type is registered with.
	 */
	struct c2_rm_domain		     *rt_dom;
};

struct c2_rm_resource_type_ops {
	/**
	 * Checks if the two resources are equal.
	 */
	bool (*rto_eq)(const struct c2_rm_resource *resource0,
		       const struct c2_rm_resource *resource1);
	/**
	 * Checks if the resource has "id".
	 */
	bool (*rto_is)(const struct c2_rm_resource *resource,
		       uint64_t id);
	/**
	 * De-serialises the resource from a buffer.
	 */
	int  (*rto_decode)(const struct c2_bufvec_cursor *cur,
			   struct c2_rm_resource **resource);
	/**
	 * Serialise a resource into a buffer.
	 */
	int  (*rto_encode)(struct c2_bufvec_cursor *cur,
			   const struct c2_rm_resource *resource);
};

/**
 * A resource owner uses the resource via a usage right (also called
 * resource right or simply right as context permits). E.g., a client might
 * have a right of a read-only or write-only or read-write access to a
 * certain extent in a file. An owner is granted a right to use a resource.
 *
 * The meaning of a resource right is determined by the resource
 * type. c2_rm_right is allocated and managed by the generic code, but it has a
 * scratchpad field (c2_rm_right::ri_datum), where type specific code stores
 * some additional information.
 *
 * A right can be something simple as a single bit (conveying, for example,
 * an exclusive ownership of some datum) or a collection of extents tagged
 * with access masks.
 *
 * A right is said to be "pinned" or "held" when it is necessary for some
 * ongoing operation. A pinned right has C2_RPF_PROTECT pins (c2_rm_pin) on its
 * c2_rm_right::ri_pins list. Otherwise a right is simply "cached".
 *
 * Rights are typically linked into one of c2_rm_owner lists. Pinned rights can
 * only happen on c2_rm_owner::ro_owned[OWOS_HELD] list. They cannot be moved
 * out of this list until unpinned.
 */
struct c2_rm_right {
	struct c2_rm_owner           *ri_owner;
	const struct c2_rm_right_ops *ri_ops;
	/**
	 * resource type private field. By convention, 0 means "empty"
	 * right.
	 */
	uint64_t                      ri_datum;
	/**
	 * Linkage of a right (and the corresponding loan, if applicable) to a
	 * list hanging off c2_rm_owner.
	 */
	struct c2_tlink               ri_linkage;
	/**
	 * A list of pins, linked through c2_rm_pins::rp_right, stuck into this
	 * right.
	 */
	struct c2_tl                  ri_pins;
	uint64_t                      ri_magix;
};

struct c2_rm_right_ops {
	/**
	 * Called when the generic code is about to free a right. Type specific
	 * code releases any resources associated with the right.
	 */
	void (*rro_free)(struct c2_rm_right *droit);
	/**
	 * Serialise a resource into a buffer.
	 */
	int  (*rro_encode)(const struct c2_rm_right *right,
			   struct c2_bufvec_cursor *cur);
	/**
	 * De-serialises the resource from a buffer.
	 */
	int  (*rro_decode)(struct c2_rm_right *right,
			   struct c2_bufvec_cursor *cur);
	/**
	 * Return the size of the right's data.
	 */
	c2_bcount_t (*rro_len) (const struct c2_rm_right *right);

	/** @name operations.
	 *
	 *  The following operations are implemented by resource type and used
	 *  by generic code to analyse rights relationships.
	 *
	 *  "0" means the empty right in the following.
         */
        /** @{ */
        /**
         * @retval True, iff r0 intersects with r1.
	 *  Rights intersect when there is some usage authorised by right r0 and
	 *  by right r1.
	 *
	 *  For example, a right to read an extent [0, 100] (denoted R:[0, 100])
	 *  intersects with a right to read or write an extent [50, 150],
	 *  (denoted RW:[50, 150]) because they can be both used to read bytes
	 *  in the extent [50, 100].
	 *
	 *  "Intersects" is assumed to satisfy the following conditions:
	 *
	 *      - intersects(A, B) iff intersects(B, A) (symmetrical),
	 *
	 *      - (A != 0) iff intersects(A, A) (almost reflexive),
	 *
	 *      - !intersects(A, 0)
	 */
        bool (*rro_intersects) (const struct c2_rm_right *r0,
                                const struct c2_rm_right *r1);
        /**
         * @retval True if r0 is subset (or proper subset) of r1.
	 */
        bool (*rro_is_subset) (const struct c2_rm_right *r0,
                               const struct c2_rm_right *r1);
        /**
         * Adjoins r1 to r0, updating r0 in place to be the sum right.
	 */
        int (*rro_join) (struct c2_rm_right *r0,
                          const struct c2_rm_right *r1);
        /**
         * Splits r0 into two parts - diff(r0,r1) and intersection(r0, r1)
	 * Destructively updates r0 with diff(r0, r1) and updates
	 * intersection with intersection of (r0, r1)
	 */
        int (*rro_disjoin) (struct c2_rm_right *r0,
                            const struct c2_rm_right *r1,
			    struct c2_rm_right *intersection);
	/**
         * @retval True, iff r0 conflicts with r1.
	 *  Rights conflict iff one of them authorises a usage incompatible with
	 *  another.
	 *
	 *  For example, R:[0, 100] conflicts with RW:[50, 150], because the
	 *  latter authorises writes to bytes in the [50, 100] extent, which
	 *  cannot be done while R:[0, 100] is held by some other owner.
	 *
	 *  "Conflicts" is assumed to satisfy the same conditions as
	 *  "intersects" and, in addition,
	 *
	 *      - conflicts(A, B) => intersects(A, B), because if rights share
         *        nothing they cannot conflict. Note that this condition
         *        restricts possible resource semantics. For example, to satisfy
         *        it, a right to write to a variable must always imply a right
         *        to read it.
	 */
        bool (*rro_conflicts) (const struct c2_rm_right *r0,
			       const struct c2_rm_right *r1);
        /** Difference between rights.
	 *
	 *  The difference is a part of r0 that doesn't intersect with r1.
	 *
	 *  For example, diff(RW:[50, 150], R:[0, 100]) == RW:[101, 150].
	 *
	 *   X <= Y means that diff(X, Y) is 0. X >= Y means Y <= X.
	 *
	 *   Two rights are equal, X == Y, when X <= Y and Y <= X.
	 *
	 *   "Difference" must satisfy the following conditions:
	 *
	 *       - diff(A, A) == 0,
	 *
	 *       - diff(A, 0) == A,
	 *
	 *       - diff(0, A) == 0,
	 *
	 *       - !intersects(diff(A, B), B),
	 *
	 *       - diff(A, diff(A, B)) == diff(B, diff(B, A)).
	 *
	 *  diff(A, diff(A, B)) is called a "meet" of A and B, it's an
	 *  intersection of rights A and B. The condition above ensures
	 *  that meet(A, B) == meet(B, A),
	 *
	 *       - diff(A, B) == diff(A, meet(A, B)),
	 *
	 *       - meet(A, meet(B, C)) == meet(meet(A, B), C),
	 *
	 *       - meet(A, 0) == 0, meet(A, A) == A, &c.,
	 *
	 *       - meet(A, B) <= A,
	 *
	 *       - (X <= A and X <= B) iff X <= meet(A, B),
	 *
	 *       - intersects(A, B) iff meet(A, B) != 0.
	 *
	 *  This function destructively updates "r0" in place.
         */
        int  (*rro_diff)(struct c2_rm_right *r0, const struct c2_rm_right *r1);
	/** Creates a copy of "src" in "dst".
	 *
	 *  @pre dst is empty.
	 */
        int  (*rro_copy)(struct c2_rm_right *dst,
			 const struct c2_rm_right *src);
        /** @} end of Rights operations. */
};

enum c2_rm_remote_state {
	REM_FREED = 0,
	REM_INITIALISED,
	REM_SERVICE_LOCATING,
	REM_SERVICE_LOCATED,
	REM_OWNER_LOCATING,
	REM_OWNER_LOCATED
};

/**
 * A representation of a resource owner from another domain.
 *
 * This is a generic structure.
 *
 * c2_rm_remote is a portal through which interaction with the remote resource
 * owners is transacted. c2_rm_remote state transitions happen under its
 * resource's lock.
 *
 * A remote owner is needed to borrow from or sub-let to an owner in a different
 * domain. To establish the communication between local and remote owner the
 * following stages are needed:
 *
 *     - a service managing the remote owner must be located in the
 *       cluster. The particular way to do this depends on a resource type. For
 *       some resource types, the service is immediately known. For example, a
 *       "grant" (i.e., a reservation of a free storage space on a data
 *       service) is provided by the data service, which is already known by
 *       the time the grant is needed. For such resource types,
 *       c2_rm_remote::rem_state is initialised to REM_SERVICE_LOCATED. For
 *       other resource types, a distributed resource location data-base is
 *       consulted to locate the service. While the data-base query is going
 *       on, the remote owner is in REM_SERVICE_LOCATING state;
 *
 *     - once the service is known, the owner within the service should be
 *       located. This is done generically, by sending a resource management fop
 *       to the service. The service responds with the remote owner identifier
 *       (c2_rm_remote::rem_cookie) used for further communications. The service
 *       might respond with an error, if the owner is no longer there. In this
 *       case, c2_rm_state::rem_state goes back to REM_SERVICE_LOCATING.
 *
 *       Owner identification is an optional step, intended to optimise remote
 *       service performance. The service should be able to deal with the
 *       requests without the owner identifier. Because of this, owner
 *       identification can be piggy-backed to the first operation on the
 *       remote owner.
 *
 * @verbatim
 *           fini
 *      +----------------INITIALISED
 *      |                     |
 *      |                     | query the resource data-base
 *      |                     |
 *      |    TIMEOUT          V
 *      +--------------SERVICE_LOCATING<----+
 *      |                     |             |
 *      |                     | reply: OK   |
 *      |                     |             |
 *      V    fini             V             |
 *    FREED<-----------SERVICE_LOCATED      | reply: moved
 *      ^                     |             |
 *      |                     | get id      |
 *      |                     |             |
 *      |    TIMEOUT          V             |
 *      +----------------OWNER_LOCATING-----+
 *      |                     |
 *      |                     | reply: id
 *      |                     |
 *      |    fini             V
 *      +----------------OWNER_LOCATED
 *
 * @endverbatim
 */
struct c2_rm_remote {
	enum c2_rm_remote_state rem_state;
	/**
	 * A resource for which the remote owner is represented.
	 */
	struct c2_rm_resource  *rem_resource;

	struct c2_rpc_session  *rem_session;
	/** A channel to signal state changes. */
	struct c2_chan          rem_signal;
	/**
	 * A linkage into the list of remotes for a given resource hanging off
	 * c2_rm_resource::r_remote.
	 */
	struct c2_tlink         rem_linkage;
	/** An identifier of the remote owner within the service. Valid in
	 *  REM_OWNER_LOCATED state. This identifier is generated by the
	 *  resource manager service.
	 */
	struct c2_cookie        rem_cookie;
	uint64_t                rem_id;
	uint64_t                rem_magix;
};

/**
 * A group of cooperating owners.
 *
 * The owners in a group coordinate their activities internally (by means
 * outside of resource manager control) as far as resource management is
 * concerned.
 *
 * Resource manager assumes that rights granted to the owners from the same
 * group never conflict.
 *
 * Typical usage is to assign all owners from the same distributed
 * transaction (or from the same network client) to a group. The decision
 * about a group scope has concurrency related implications, because the
 * owners within a group must coordinate access between themselves to
 * maintain whatever scheduling properties are desired, like serialisability.
 */
struct c2_rm_group {
};

/**
   c2_rm_owner state machine states.

   @dot
   digraph rm_owner {
	ROS_INITIAL -> ROS_INITIAL
	ROS_INITIAL -> ROS_INITIALISING
	ROS_INITIALISING -> ROS_FINAL
	ROS_INITIALISING -> ROS_ACTIVE
	ROS_ACTIVE -> ROS_QUIESCE
	ROS_QUIESCE -> ROS_FINALISING
	ROS_FINALISING -> ROS_FINAL
	ROS_FINALISING -> ROS_DEFUNCT
   }
   @enddot
 */
enum c2_rm_owner_state {
	/**
	 *  Initial state.
	 *
	 *  In this state owner rights lists are empty (including incoming and
	 *  outgoing request lists).
	 */
	ROS_INITIAL = 1,
	/**
	 * Initial network setup state:
	 *
	 *     - registering with the resource data-base;
	 *
	 *     - &c.
         */
	ROS_INITIALISING,
	/**
	 * Active request processing state. Once an owner reached this state it
	 * must pass through the finalising state.
	 */
	ROS_ACTIVE,
	/**
	 * No new requests are allowed in this state.
	 * Existing incoming requests are drained in this state.
	 */
	ROS_QUIESCE,
	/**
	 * Flushes all the loans.
	 * The owner collects from debtors and repays creditors.
	 */
	ROS_FINALISING,
	/**
	 *  Final state.
	 *
	 *  During finalisation, if owner fails to clear the loans, it
	 * it enters DEFUNCT state.
	 */
	ROS_DEFUNCT,
	/**
	 *  Final state.
	 *
	 *  In this state owner rights lists are empty (including incoming and
	 *  outgoing request lists).
	 */
	ROS_FINAL
};

enum {
	/**
	 * Incoming requests are assigned a priority (greater numerical value
	 * is higher). When multiple requests are ready to be fulfilled, higher
	 * priority ones have a preference.
	 */
	C2_RM_REQUEST_PRIORITY_MAX = 3,
	C2_RM_REQUEST_PRIORITY_NR
};

/**
 * c2_rm_owner::ro_owned[] list of usage rights possessed by the owner is split
 * into sub-lists enumerated by this enum.
 */
enum c2_rm_owner_owned_state {
	/**
	 * Sub-list of pinned rights.
	 *
	 * @see c2_rm_right
	 */
	OWOS_HELD,
	/**
	 * Not-pinned right is "cached". Such right can be returned to an
	 * upward owner from which it was previously borrowed (i.e., right can
	 * be "cancelled") or sub-let to downward owners.
	 */
	OWOS_CACHED,
	OWOS_NR
};

/**
 * Lists of incoming and outgoing requests are subdivided into sub-lists.
 */
enum c2_rm_owner_queue_state {
	/**
	 * "Ground" request is not excited.
	 */
	OQS_GROUND,
	/**
	 * Excited requests are those for which something has to be done. An
	 * outgoing request is excited when it completes (or times out). An
	 * incoming request is excited when it's ready to go from RI_WAIT to
	 * RI_CHECK state.
	 *
	 * Resource owner state machine goes through lists of excited requests
	 * processing them. This processing can result in more excitement
	 * somewhere, but eventually terminates.
	 *
	 * @see http://en.wikipedia.org/wiki/Excited_state
	 */
	OQS_EXCITED,
	OQS_NR
};

/**
 * Resource ownership is used for two purposes:
 *
 *  - concurrency control. Only resource owner can manipulate the resource
 *    and ownership transfer protocol assures that owners do not step on
 *    each other. That is, resources provide traditional distributed
 *    locking mechanism;
 *
 *  - replication control. Resource owner can create a (local) copy of a
 *    resource. The ownership transfer protocol with the help of version
 *    numbers guarantees that multiple replicas are re-integrated
 *    correctly. That is, resources provide a cache coherency
 *    mechanism. Global cluster-wide cache management policy can be
 *    implemented on top of resources.
 *
 * A resource owner possesses rights on a particular resource. Multiple
 * owners within the same domain can possess rights on the same resource,
 * but no two owners in the cluster can possess conflicting rights at the
 * same time. The last statement requires some qualification:
 *
 *  - "time" here means the logical time in an observable history of the
 *    file system. It might so happen, that at a certain moment in physical
 *    time, data-structures (on different nodes, typically) would look as
 *    if conflicting rights were granted, but this is only possible when
 *    such rights will never affect visible system behaviour (e.g., a
 *    consensual decision has been made by that time to evict one of the
 *    nodes);
 *
 *  - in a case of optimistic conflict resolution, "no conflicting rights"
 *    means "no rights on which conflicts cannot be resolved afterwards by
 *    the optimistic conflict resolution policy".
 *
 * c2_rm_owner is a generic structure, created and maintained by the
 * generic resource manager code.
 *
 * Off a c2_rm_owner, hang several lists and arrays of lists for rights
 * book-keeping: c2_rm_owner::ro_borrowed, c2_rm_owner::ro_sublet and
 * c2_rm_owner::ro_owned[], further subdivided by states.
 *
 * As rights form a lattice (see c2_rm_right_ops), it is always possible to
 * represent the cumulative sum of all rights on a list as a single
 * c2_rm_right. The reason the lists are needed is that rights in the lists
 * have some additional state associated with them (e.g., loans for
 * c2_rm_owner::ro_borrowed, c2_rm_owner::ro_sublet or pins
 * (c2_rm_right::ri_pins) for c2_rm_owner::ro_owned[]) that can be manipulated
 * independently.
 *
 * Owner state diagram:
 *
 * @verbatim
 *
 *                                  INITIAL
 *                                     |
 *                                     V
 *                               INITIALISING-------+
 *                                     |            |
 *                                     V            |
 *                                   ACTIVE         |
 *                                     |            |
 *                                     V            |
 *                                  QUIESCE         |
 *                                     |            |
 *                                     V            V
 *                     DEFUNCT<----FINALISING---->FINAL
 *
 * @endverbatim
 *
 * @invariant under ->ro_lock { // keep books balanced at all times
 *         join of rights on ->ro_owned[] and
 *                 rights on ->ro_sublet equals to
 *         join of rights on ->ro_borrowed           &&
 *
 *         meet of (join of rights on ->ro_owned[]) and
 *                 (join of rights on ->ro_sublet) is empty.
 * }
 *
 * @invariant under ->ro_lock {
 *         ->ro_owned[OWOS_HELD] is exactly the list of all held rights (ones
 *         with elevated user count)
 *
 * invariant is checked by rm/rm.c:owner_invariant().
 *
 * }
 */
struct c2_rm_owner {
	struct c2_sm           ro_sm;

	struct c2_sm_group     ro_sm_grp;
	/**
	 * Resource this owner possesses the rights on.
	 */
	struct c2_rm_resource *ro_resource;
	/**
	 * A group this owner is part of.
	 *
	 * If this is NULL, the owner is not a member of any group (a
	 * "standalone" owner).
	 */
	struct c2_rm_group    *ro_group;
	/**
	 * An upward creditor, from where this owner borrows rights.
	 */
	struct c2_rm_remote   *ro_creditor;
	/**
	 * A list of loans, linked through c2_rm_loan::rl_right:ri_linkage that
	 * this owner borrowed from other owners.
	 *
	 * @see c2_rm_loan
	 */
	struct c2_tl           ro_borrowed;
	/**
	 * A list of loans, linked through c2_rm_loan::rl_right:ri_linkage that
	 * this owner extended to other owners. Rights on this list are not
	 * longer possessed by this owner: they are counted in
	 * c2_rm_owner::ro_borrowed, but not in c2_rm_owner::ro_owned.
	 *
	 * @see c2_rm_loan
	 */
	struct c2_tl           ro_sublet;
	/**
	 * A list of rights, linked through c2_rm_right::ri_linkage possessed
	 * by the owner.
	 */
	struct c2_tl           ro_owned[OWOS_NR];
	/**
	 * An array of lists, sorted by priority, of incoming requests. Requests
	 * are linked through c2_rm_incoming::rin_want::ri_linkage.
	 *
	 * @see c2_rm_incoming
	 */
	struct c2_tl           ro_incoming[C2_RM_REQUEST_PRIORITY_NR][OQS_NR];
	/**
	 * An array of lists, of outgoing, not yet completed, requests.
	 */
	struct c2_tl           ro_outgoing[OQS_NR];
	/**
	 * Generation count associated with a owner cookie.
	 */
	uint64_t	       ro_id;
};

enum {
	/**
	 * Value of c2_rm_loan::rl_id for a self-loan.
	 * This value is invalid for any other type of loan.
	 */
	C2_RM_LOAN_SELF_ID = 1
};

/**
 * A loan (of a right) from one owner to another.
 *
 * c2_rm_loan is always on some list (to which it is linked through
 * c2_rm_loan::rl_right:ri_linkage field) in an owner structure. This owner is
 * one party of the loan. Another party is c2_rm_loan::rl_other. Which party is
 * creditor and which is debtor is determined by the list the loan is on.
 */
struct c2_rm_loan {
	struct c2_rm_right   rl_right;
	/**
	 * Other party in the loan. Either an "upward" creditor or "downward"
	 * debtor, or "self" in case of a fake loan issued by the top-level
	 * creditor to maintain its invariants.
	 */
	struct c2_rm_remote *rl_other;
	/**
	 * An identifier generated by the remote end that should be passed back
	 * whenever operating on a loan (think loan agreement number).
	 */
	struct c2_cookie     rl_cookie;
	uint64_t             rl_id;
	uint64_t             rl_magix;
};

/**
   States of incoming request. See c2_rm_incoming for description.

   @dot
   digraph rm_incoming_state {
	RI_INITIALISED -> RI_CHECK
	RI_INITIALISED -> RI_FINAL
	RI_CHECK -> RI_SUCCESS
	RI_CHECK -> RI_FAILURE [label="Live lock"]
	RI_CHECK -> RI_WAIT [label="Pins placed"]
	RI_WAIT -> RI_FAILURE [label="Timeout"]
	RI_WAIT -> RI_CHECK [label="Last completion"]
	RI_WAIT -> RI_WAIT [label="Completion"]
	RI_SUCCESS -> RI_RELEASED [label="Right released"]
	RI_FAILURE -> RI_FINAL [label="Finalised"]
	RI_RELEASED -> RI_FINAL
   }
   @enddot
 */
enum c2_rm_incoming_state {
	RI_INITIALISED = 1,
	/** Ready to check whether the request can be fulfilled. */
	RI_CHECK,
	/** Request has been fulfilled. */
	RI_SUCCESS,
	/** Request cannot be fulfilled. */
	RI_FAILURE,
	/** Has to wait for some future event, like outgoing request completion
	 *  or release of a locally held usage right.
	 */
	RI_WAIT,
	/** Right has been released (possibly by c2_rm_right_put()). */
	RI_RELEASED,
	/** Request finalised. */
	RI_FINAL
};

/**
 * Types of an incoming usage right request.
 */
enum c2_rm_incoming_type {
	/**
	 * A request for a usage right from a local user. When the request
	 * succeeds, the right is held by the owner.
	 */
	C2_RIT_LOCAL,
	/**
	 * A request to loan a usage right to a remote owner. Fulfillment of
	 * this request might cause further outgoing requests to be sent, e.g.,
	 * to revoke rights sub-let to remote owner.
	 */
	C2_RIT_BORROW,
	/**
	 * A request to return a usage right previously sub-let to this owner.
	 */
	C2_RIT_REVOKE
};

/**
 * Some universal (i.e., not depending on a resource type) granting policies.
 */
enum c2_rm_incoming_policy {
	RIP_NONE = 1,
	/**
	 * If possible, don't insert a new right into the list of possessed
	 * rights. Instead, pin possessed rights overlapping with the requested
	 * right.
	 */
	RIP_INPLACE,
	/**
	 * Insert a new right into the list of possessed rights, equal to the
	 * requested right.
	 */
	RIP_STRICT,
	/**
	 * ...
	 */
	RIP_JOIN,
	/**
	 * Grant maximal possible right, not conflicting with others.
	 */
	RIP_MAX,
	RIP_NR
};

/**
 * Flags controlling incoming usage right request processing. These flags are
 * stored in c2_rm_incoming::rin_flags and analysed in c2_rm_right_get().
 */
enum c2_rm_incoming_flags {
	/**
	 * Previously sub-let rights may be revoked, if necessary, to fulfill
	 * this request.
	 */
	RIF_MAY_REVOKE = (1 << 0),
	/**
	 * More rights may be borrowed, if necessary, to fulfill this request.
	 */
	RIF_MAY_BORROW = (1 << 1),
	/**
	 * The interaction between the request and locally possessed rights is
	 * the following:
	 *
	 *     - by default, locally possessed rights are ignored. This scenario
	 *       is typical for a local request (C2_RIT_LOCAL), because local
	 *       users resolve conflicts by some other means (usually some
	 *       form of concurrency control, like locking);
	 *
	 *      - if RIF_LOCAL_WAIT is set, the request can be fulfilled only
	 *        when there is no locally possessed rights conflicting with the
	 *        wanted right. This is typical for a remote request
	 *        (C2_RIT_BORROW or C2_RIT_REVOKE);
	 *
	 *      - if RIF_LOCAL_TRY is set, the request will be immediately
	 *        denied, if there are conflicting local rights. This allows to
	 *        implement a "try-lock" like functionality.
	 */
	RIF_LOCAL_WAIT = (1 << 2),
	/**
	 * Fail the request if it cannot be fulfilled because of the local
	 * conflicts.
	 *
	 * @see RIF_LOCAL_WAIT
	 */
	RIF_LOCAL_TRY  = (1 << 3),
};

/**
 * Resource usage right request.
 *
 * The same c2_rm_incoming structure is used to track state of the incoming
 * requests both "local", i.e., from the same domain where the owner resides
 * and "remote".
 *
 * An incoming request is created for
 *
 *     - local right request, when some user wants to use the resource;
 *
 *     - remote right request from a "downward" owner which asks to sub-let
 *       some rights;
 *
 *     - remote right request from an "upward" owner which wants to revoke some
 *       rights.
 *
 * These usages are differentiated by c2_rm_incoming::rin_type.
 *
 * An incoming request is a state machine, going through the following stages:
 *
 *     - [CHECK]   This stage determines whether the request can be fulfilled
 *                 immediately. Local request can be fulfilled immediately if
 *                 the wanted right is possessed by the owner, that is, if
 *                 in->rin_want is implied by a join of owner->ro_owned[].
 *
 *                 A non-local (loan or revoke) request can be fulfilled
 *                 immediately if the wanted right is implied by a join of
 *                 owner->ro_owned[OWOS_CACHED], that is, if the owner has
 *                 enough rights to grant the loan and the wanted right does
 *                 not conflict with locally held rights.
 *
 *     - [POLICY]  If the request can be fulfilled immediately, the "policy" is
 *                 invoked which decides which right should be actually granted,
 *                 sublet or revoked. That right can be larger than
 *                 requested. A policy is, generally, resource type dependent,
 *                 with a few universal policies defined by enum
 *                 c2_rm_incoming_policy.
 *
 *     - [SUCCESS] Finally, fulfilled request succeeds.
 *
 *     - [ISSUE]   Otherwise, if the request can not be fulfilled immediately,
 *                 "pins" (c2_rm_pin) are added which will notify the request
 *                 when the fulfillment check might succeed.
 *
 *                 Pins are added to:
 *
 *                     - every conflicting right held by this owner (when
 *                       RIF_LOCAL_WAIT flag is set on the request and always
 *                       for a remote request);
 *
 *                     - outgoing requests to revoke conflicting rights sub-let
 *                       to remote owners (when RIF_MAY_REVOKE flag is set);
 *
 *                     - outgoing requests to borrow missing rights from remote
 *                       owners (when RIF_MAY_BORROW flag is set);
 *
 *                 Outgoing requests mentioned above are created as necessary
 *                 in the ISSUE stage.
 *
 *     - [CYCLE]   When all the pins stuck in the ISSUE state are released
 *                 (either when a local right is released or when an outgoing
 *                 request completes), go back to the CHECK state.
 *
 * Looping back to the CHECK state is necessary, because possessed rights are
 * not "pinned" during wait and can go away (be revoked or sub-let). The rights
 * are not pinned to avoid dependencies between rights that can lead to
 * dead-locks and "cascading evictions". The alternative is to pin rights and
 * issue outgoing requests synchronously one by one and in a strict order (to
 * avoid dead-locks). The rationale behind current decision is that probability
 * of a live-lock is low enough and the advantage of issuing concurrent
 * asynchronous outgoing requests is important.
 *
 * @todo Should live-locks prove to be a practical issue, C2_RPF_BARRIER pins
 * can be used to reduce concurrency and assure state machine progress.
 *
 * It's a matter of policy - how many outgoing requests are sent out in ISSUE
 * state. The fewer requests are sent, the more CHECK-ISSUE-WAIT loop
 * iterations would typically happen. An extreme case of sending no more than a
 * single request is also possible and has some advantages: outgoing request
 * can be allocated as part of incoming request, simplifying memory management.
 *
 * It is also a matter of policy, how exactly the request is satisfied after a
 * successful CHECK state. Suppose, for example, that the owner possesses
 * rights R0 and R1 such that wanted right W is implied by join(R0, R1), but
 * neither R0 nor R1 alone imply W. Some possible CHECK outcomes are:
 *
 *     - increase user counts in both R0 and R1;
 *
 *     - insert a new right equal to W into owner->ro_owned[];
 *
 *     - insert a new right equal to join(R0, R1) into owner->ro_owned[].
 *
 * All have their advantages and drawbacks:
 *
 *     - elevating R0 and R1 user counts keeps owner->ro_owned[] smaller, but
 *       pins more rights than strictly necessary;
 *
 *     - inserting W behaves badly in a standard use case where a thread doing
 *       sequential IO requests a right on each iteration;
 *
 *     - inserting the join pins more rights than strictly necessary.
 *
 * All policy questions are settled by per-request flags and owner settings,
 * based on access pattern analysis.
 *
 * Following is a state diagram, where stages that are performed without
 * blocking (for network communication) are lumped into a single state:
 *
 * @verbatim
 *                                 SUCCESS-----------------------+
 *                                    ^                          |
 *             too many iterations    |                          |
 *                  live-lock         |    last completion       |
 *                +-----------------CHECK<-----------------+     |
 *                |                   |                    |     |
 *                |                   |                    |     |
 *                V                   |                    |     |
 *        +----FAILURE                | pins placed        |     |
 *        |       ^                   |                    |     |
 *        |       |                   |                    |     |
 *        |       |                   V                    |     |
 *        |       +----------------WAITING-----------------+     |
 *        |            timeout      ^   |                        |
 *        |                         |   | completion             |
 *        |                         |   |                        |
 *        |                         +---+                        |
 *        |                                                      |
 *        |                         RELEASED<--------------------+
 *        |                            |
 *        |                            |
 *        |                            V
 *        +------------------------->FINAL
 *
 * @endverbatim
 *
 * c2_rm_incoming fields and state transitions are protected by the owner's
 * mutex.
 *
 * @note a cedent can grant a usage right larger than requested.
 *
 * An incoming request is placed by c2_rm_right_get() on one of owner's
 * c2_rm_owner::ro_incoming[] lists depending on its priority. It remains on
 * this list until request processing failure or c2_rm_right_put() call.
 *
 * @todo a new type of incoming request C2_RIT_GRANT (C2_RIT_FOIEGRAS?) can be
 * added to forcibly grant new rights to the owner, for example, as part of a
 * coordinated global distributed resource usage balancing between
 * owners. Processing of requests of this type would be very simple, because
 * adding new rights never blocks. Similarly, a new outgoing request type
 * C2_ROT_TAKE could be added.
 */
struct c2_rm_incoming {
	enum c2_rm_incoming_type	 rin_type;
	struct c2_sm                     rin_sm;
	/**
	 * Stores the error code for incoming request. A separate field is
	 * needed because rin_sm.sm_rc is associated with an error of a state.
	 *
	 * For incoming it's possible that an error is set in RI_WAIT and
	 * then incoming has to be put back in RI_CHECK state before it can
	 * be put into RI_FAILURE. The state-machine model does not handle
	 * this well.
	 */
	int32_t				 rin_rc;
	enum c2_rm_incoming_policy	 rin_policy;
	uint64_t			 rin_flags;
	/** The right requested. */
	struct c2_rm_right		 rin_want;
	/**
	 * List of pins, linked through c2_rm_pin::rp_incoming_linkage, for all
	 * rights held to satisfy this request.
	 *
	 * @invariant meaning of this list depends on the request state:
	 *
	 *     - RI_CHECK, RI_SUCCESS: a list of C2_RPF_PROTECT pins on rights
	 *       in ->rin_want.ri_owner->ro_owned[];
	 *
	 *      - RI_WAIT: a list of C2_RPF_TRACK pins on outgoing requests
	 *        (through c2_rm_outgoing::rog_want::rl_right::ri_pins) and
	 *        held rights in ->rin_want.ri_owner->ro_owned[OWOS_HELD];
	 *
	 *      - other states: empty.
	 */
	struct c2_tl			 rin_pins;
	/**
	 * Request priority from 0 to C2_RM_REQUEST_PRIORITY_MAX.
	 */
	int				 rin_priority;
	const struct c2_rm_incoming_ops *rin_ops;
	uint64_t                         rin_magix;
};

/**
 * Operations assigned by a resource manager user to an incoming
 * request. Resource manager calls methods in this operation vector when events
 * related to the request happen.
 */
struct c2_rm_incoming_ops {
	/**
	 * This is called when incoming request processing completes either
	 * successfully (rc == 0) or with an error (-ve rc).
	 */
	void (*rio_complete)(struct c2_rm_incoming *in, int32_t rc);
	/**
	 * This is called when a request arrives that conflicts with the right
	 * held by this incoming request.
	 */
	void (*rio_conflict)(struct c2_rm_incoming *in);
};

/**
 * Types of outgoing requests sent by the request manager.
 */
enum c2_rm_outgoing_type {
	/**
	 * A request to borrow a right from an upward resource owner. This
	 * translates into a C2_RIT_BORROW incoming request.
	 */
	C2_ROT_BORROW = 1,
	/**
	 * A request returning a previously borrowed right.
	 */
	C2_ROT_CANCEL,
	/**
	 * A request to return previously borrowed right. This translates into
	 * a C2_RIT_REVOKE incoming request on the remote owner.
	 */
	C2_ROT_REVOKE
};

/**
 * An outgoing request is created on behalf of some incoming request to track
 * the state of right transfer with some remote domain.
 *
 * An outgoing request is created to:
 *
 *     - borrow a new right from some remote owner (an "upward" request) or
 *
 *     - revoke a right sublet to some remote owner (a "downward" request) or
 *
 *     - cancel this owner's right and return it to an upward owner.
 *
 * Before a new outgoing request is created, a list of already existing
 * outgoing requests (c2_rm_owner::ro_outgoing) is scanned. If an outgoing
 * request of a matching type for a greater or equal right exists, new request
 * is not created. Instead, the incoming request pins existing outgoing
 * request.
 *
 * c2_rm_outgoing fields and state transitions are protected by the owner's
 * mutex.
 */
struct c2_rm_outgoing {
	enum c2_rm_outgoing_type rog_type;
	/*
	 * The error code (from reply or timeout) for this outgoing request.
	 */
	int32_t			 rog_rc;
	/** A right that is to be transferred. */
	struct c2_rm_loan        rog_want;
	uint64_t                 rog_magix;
};

enum c2_rm_pin_flags {
	C2_RPF_TRACK   = (1 << 0),
	C2_RPF_PROTECT = (1 << 1),
	C2_RPF_BARRIER = (1 << 2)
};

/**
 * A pin is used to
 *
 *     - C2_RPF_TRACK: track when a right changes its state;
 *
 *     - C2_RPF_PROTECT: to protect a right from revocation;
 *
 *     - C2_RPF_BARRIER: to prohibit C2_RPF_PROTECT pins from being added to the
 *       right.
 *
 * Fields of this struct are protected by the owner's lock.
 *
 * Abstractly speaking, pins allow N:M (many to many) relationships between
 * incoming requests and rights: an incoming request has a list of pins "from"
 * it and a right has a list of pins "to" it. A typical use case is as follows:
 *
 * @b Protection.
 *
 * While a right is actively used, it cannot be revoked. For example, while file
 * write is going on, the right to write in the target file extent must be
 * held. A right is held (or pinned) from the return from c2_rm_right_get()
 * until the matching call to c2_rm_right_put(). To mark the right as pinned,
 * c2_rm_right_get() adds a C2_RPF_PROTECT pin from the incoming request to the
 * returned right (generally, more than one right can be pinned as result on
 * c2_rm_right_get()). This pin is removed by the call to
 * c2_rm_right_put(). Multiple incoming requests can pin the same right.
 *
 * @b Tracking.
 *
 * An incoming request with a RIF_LOCAL_WAIT flag might need to wait until a
 * conflicting pinned right becomes unpinned. To this end, an C2_RPF_TRACK pin
 * is added from the incoming request to the right.
 *
 * When the last C2_RPF_PROTECT pin is removed from a right, the right becomes
 * "cached" and the list of pins to the right is scanned. For each C2_RPF_TRACK
 * pin on the list, its incoming request is checked to see whether this was the
 * last tracking pin the request is waiting for.
 *
 * An incoming request might also issue an outgoing request to borrow or revoke
 * some rights, necessary to fulfill the request. An C2_RPF_TRACK pin is added
 * from the incoming request to the right embedded in the outgoing request
 * (c2_rm_outgoing::rog_want::rl_right). Multiple incoming requests can pin the
 * same outgoing request. When the outgoing request completes, the incoming
 * requests waiting for it are checked as above.
 *
 * @b Barrier.
 *
 * Not currently used. The idea is to avoid live-locks and guarantee progress of
 * incoming request processing by pinning the rights with a C2_RPF_BARRIER pin.
 *
 * @verbatim
 *
 *
 *      ->ro_owned[]--->R------>R        R<------R<----------+
 *                      |       |        |       |           |
 *>ro_incoming[]        |       |        |       |           |
 *      |               |       |        |       |           |
 *      |               |       |        |       |    ->ro_outgoing[]
 *      V               |       |        |       |
 *  INC[CHECK]----------T-------T--------T-------T
 *      |                       |                |
 *      |                       |                |
 *      V                       |                |
 *  INC[SUCCESS]----------------P                |
 *      |                                        |
 *      |                                        |
 *      V                                        |
 *  INC[CHECK]-----------------------------------T
 *
 * @endverbatim
 *
 * On this diagram, INC[S] is an incoming request in a state S, R is a right, T
 * is an C2_RPF_TRACK pin and P is an C2_RPF_PROTECT pin.
 *
 * The incoming request in the middle has been processed successfully and now
 * protects its right.
 *
 * The topmost incoming request waits for 2 possessed rights to become unpinned
 * and also waiting for completion of 2 outgoing requests. The incoming request
 * on the bottom waits for completion of the same outgoing request.
 *
 * c2_rm_right_put() scans the request's pin list (horizontal direction) and
 * removes all pins. If the last pin was removed from a right, right's pin list
 * is scanned (vertical direction), checking incoming requests for possible
 * state transitions.
 */
struct c2_rm_pin {
	uint32_t               rp_flags;
	struct c2_rm_right    *rp_right;
	/** An incoming request that stuck this pin. */
	struct c2_rm_incoming *rp_incoming;
	/**
	 * Linkage into a list of all pins for a right, hanging off
	 *  c2_rm_right::ri_pins.
	 */
	struct c2_tlink        rp_right_linkage;
	/**
	 * Linkage into a list of all pins, held to satisfy an incoming
	 * request. This list hangs off c2_rm_incoming::rin_pins.
	 */
	struct c2_tlink        rp_incoming_linkage;
	uint64_t               rp_magix;
};

C2_INTERNAL void c2_rm_domain_init(struct c2_rm_domain *dom);
C2_INTERNAL void c2_rm_domain_fini(struct c2_rm_domain *dom);

/**
 * Registers a resource type with a domain.
 *
 * @pre  rtype->rt_id == C2_RM_RESOURCE_TYPE_ID_INVALID &&
 *       rtype->rt_dom == NULL
 * @post IS_IN_ARRAY(rtype->rt_id, dom->rd_types) && rtype->rt_dom == dom
 */
C2_INTERNAL void c2_rm_type_register(struct c2_rm_domain *dom,
				     struct c2_rm_resource_type *rt);

/**
 * Deregisters a resource type.
 *
 * @pre  IS_IN_ARRAY(rtype->rt_id, dom->rd_types) && rtype->rt_dom != NULL
 * @post rtype->rt_id == C2_RM_RESOURCE_TYPE_ID_INVALID &&
 *       rtype->rt_dom == NULL
 */
C2_INTERNAL void c2_rm_type_deregister(struct c2_rm_resource_type *rtype);

/**
 * Adds a resource to the list of resources and increments resource type
 * reference count.
 *
 * @pre c2_tlist_is_empty(res->r_linkage) && res->r_ref == 0
 * @pre rtype->rt_resources does not contain a resource equal (in the
 *      c2_rm_resource_type_ops::rto_eq() sense) to res
 *
 * @post res->r_ref > 0
 * @post res->r_type == rtype
 * @post c2_tlist_contains(&rtype->rt_resources, &res->r_linkage)
 */
C2_INTERNAL void c2_rm_resource_add(struct c2_rm_resource_type *rtype,
				    struct c2_rm_resource *res);
/**
 * Removes a resource from the list of resources. Dual to c2_rm_resource_add().
 *
 * @pre res->r_type->rt_nr_resources > 0
 * @pre c2_tlist_contains(&rtype->rt_resources, &res->r_linkage)
 *
 * @post !c2_tlist_contains(&rtype->rt_resources, &res->r_linkage)
 */
C2_INTERNAL void c2_rm_resource_del(struct c2_rm_resource *res);

/**
 * Initialises owner fields and increments resource reference counter.
 *
 * The owner's right lists are initially empty.
 *
 * @pre owner->ro_state == ROS_FINAL
 * @pre creditor->rem_state >= REM_SERVICE_LOCATED
 *
 * @post (owner->ro_state == ROS_INITIALISING || owner->ro_state == ROS_ACTIVE)&&
 *       owner->ro_resource == res)
 */
C2_INTERNAL void c2_rm_owner_init(struct c2_rm_owner *owner,
				  struct c2_rm_resource *res,
				  struct c2_rm_remote *creditor);

/**
 * Loans a right to an owner from itself.
 *
 * This is used to initialise a "top-most" resource owner that has no upward
 * creditor.
 *
 * This call doesn't copy "r": user supplied right is linked into owner lists.
 *
 * @see c2_rm_owner_init()
 *
 * @pre  owner->ro_state == ROS_INITIALISING
 * @post owner->ro_state == ROS_INITIALISING
 * @post c2_tlist_contains(&owner->ro_owned[OWOS_CACHED], &r->ri_linkage))
 */
C2_INTERNAL int c2_rm_owner_selfadd(struct c2_rm_owner *owner,
				    struct c2_rm_right *r);

/**
 * Retire the owner before finalising it. This function will revoke sublets
 * and give up loans.
 *
 * @pre owner->ro_state == ROS_ACTIVE || ROS_QUIESCE
 * @see c2_rm_owner_fini
 *
 */
C2_INTERNAL void c2_rm_owner_retire(struct c2_rm_owner *owner);

/**
 * Finalises the owner. Dual to c2_rm_owner_init().
 *
 * @pre owner->ro_state == ROS_FINAL
 * @pre c2_tlist_is_empty(owner->ro_borrowed) &&
 * c2_tlist_is_empty(owner->ro_sublet) &&
 *                         c2_tlist_is_empty(owner->ro_owned[*]) &&
 *                         c2_tlist_is_empty(owner->ro_incoming[*][*]) &&
 *                         c2_tlist_is_empty(owner->ro_outgoing[*]) &&
 *
 */
C2_INTERNAL void c2_rm_owner_fini(struct c2_rm_owner *owner);

/**
 * Locks state machine group of an owner
 */
C2_INTERNAL void c2_rm_owner_lock(struct c2_rm_owner *owner);
/**
 * Unlocks state machine group of an owner
 */
C2_INTERNAL void c2_rm_owner_unlock(struct c2_rm_owner *owner);

/**
 * Locks state machine group of an owner
 */
void c2_rm_owner_lock(struct c2_rm_owner *owner);
/**
 * Unlocks state machine group of an owner
 */
void c2_rm_owner_unlock(struct c2_rm_owner *owner);

/**
 * Initialises generic fields in struct c2_rm_right.
 *
 * This is called by generic RM code to initialise an empty right of any
 * resource type and by resource type specific code to initialise generic fields
 * of a struct c2_rm_right.
 *
 * This function calls c2_rm_resource_ops::rop_right_init().
 */
C2_INTERNAL void c2_rm_right_init(struct c2_rm_right *right,
				  struct c2_rm_owner *owner);

/**
 * Finalised generic fields in struct c2_rm_right. Dual to c2_rm_right_init().
 */
C2_INTERNAL void c2_rm_right_fini(struct c2_rm_right *right);

/**
 * @param src_right - A source right which is to be duplicated.
 * @param dest_right - A destination right. This right will be allocated,
 *                     initialised and then filled with src_right.
 * Allocates and duplicates a right.
 */
C2_INTERNAL int c2_rm_right_dup(const struct c2_rm_right *src_right,
				struct c2_rm_right **dest_right);

/**
 * @param src_right - A source right which is to be duplicated.
 * @param dest_right - A destination right. This right will be allocated,
 *                     initialised and then filled with src_right.
 * Allocates and duplicates a right.
 */
int c2_rm_right_dup(const struct c2_rm_right *src_right,
		    struct c2_rm_right **dest_right);

/**
 * Initialises the fields of for incoming structure.
 * This creates an incoming request with an empty c2_rm_incoming::rin_want
 * right.
 *
 * @param in - incoming right request structure
 * @param owner - for which incoming request is intended.
 * @param type - incoming request type
 * @param policy - applicable policy
 * @param flags - type of request (borrow, revoke, local)
 * @see c2_rm_incoming_fini
 */
C2_INTERNAL void c2_rm_incoming_init(struct c2_rm_incoming *in,
				     struct c2_rm_owner *owner,
				     enum c2_rm_incoming_type type,
				     enum c2_rm_incoming_policy policy,
				     uint64_t flags);

/**
 * Finalises the fields of
 * @param in
 * @see Dual to c2_rm_incoming_init().
 */
C2_INTERNAL void c2_rm_incoming_fini(struct c2_rm_incoming *in);

/**
 * Initialises the fields of remote owner.
 * @param rem
 * @param res - Resource for which proxy is obtained.
 * @see c2_rm_remote_fini
 */
C2_INTERNAL void c2_rm_remote_init(struct c2_rm_remote *rem,
				   struct c2_rm_resource *res);

/**
 * Finalises the fields of remote owner.
 *
 * @param rem
 * @see c2_rm_remote_init
 * @pre rem->rem_state == REM_INITIALIZED ||
 *      rem->rem_state == REM_SERVICE_LOCATED ||
 *      rem->rem_state == REM_OWNER_LOCATED
 */
C2_INTERNAL void c2_rm_remote_fini(struct c2_rm_remote *rem);

/**
 * Starts a state machine for a resource usage right request. Adds pins for
 * this request. Asynchronous operation - the right will not generally be held
 * at exit.
 *
 * @pre IS_IN_ARRAY(in->rin_priority, owner->ro_incoming)
 * @pre in->rin_state == RI_INITIALISED
 * @pre c2_tlist_is_empty(&in->rin_want.ri_linkage)
 *
 */
C2_INTERNAL void c2_rm_right_get(struct c2_rm_incoming *in);

/**
 * Allocates suitably sized buffer and encode it into that buffer.
 */
C2_INTERNAL int c2_rm_right_encode(const struct c2_rm_right *right,
				   struct c2_buf *buf);

/**
 * Decodes a right from its serialised presentation.
 */
C2_INTERNAL int c2_rm_right_decode(struct c2_rm_right *right,
				   struct c2_buf *buf);

/**
 * Releases the right pinned by struct c2_rm_incoming.
 *
 * @pre in->rin_state == RI_SUCCESS
 * @post c2_tlist_empty(&in->rin_pins)
 */
C2_INTERNAL void c2_rm_right_put(struct c2_rm_incoming *in);

/** @} */

/**
 * @defgroup Resource manager networking
 */
/** @{ */

/**
 * Constructs a remote owner associated with "right".
 *
 * After this function returns, "other" is in the process of locating the remote
 * service and remote owner, as described in the comment on c2_rm_remote.
 */
C2_INTERNAL int c2_rm_net_locate(struct c2_rm_right *right,
				 struct c2_rm_remote *other);

/**
   @todo Assigns a service to a given remote.

   @pre  rem->rem_state < REM_SERVICE_LOCATED
   @post rem->rem_state == REM_SERVICE_LOCATED
void c2_rm_remote_service_set(struct c2_rm_remote *rem,
			      struct c2_service_id *sid);
*/
/**
 * Assigns an owner id to a given remote.
 *
 * @pre  rem->rem_state < REM_OWNER_LOCATED
 * @post rem->rem_state == REM_OWNER_LOCATED
 */
C2_INTERNAL void c2_rm_remote_owner_set(struct c2_rm_remote *rem, uint64_t id);

/** @} end of Resource manager networking */

/* __COLIBRI_RM_RM_H__ */
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
