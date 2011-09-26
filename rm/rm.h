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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 04/28/2011
 */
#ifndef __COLIBRI_RM_RM_H__
#define __COLIBRI_RM_RM_H__

#include "net/net.h"         /* c2_service_id */

/**
   @defgroup rm Resource management

   A resource is an entity in Colibri for which a notion of ownership can be
   well-defined. See the HLD referenced below for more details.

   In Colibri almost everything is a resource, except for the low-level
   types that are used to implement the resource framework.

   Resource management is split into two parts:

       - generic functionality, implemented by the code in rm/ directory and

       - resource type specific functionality.

   These parts interacts by the operation vectors (c2_rm_resource_ops,
   c2_rm_resource_type_ops and c2_rm_right_ops) provided by a resource type
   and called by the generic code. Type specific code, in turn, calls
   generic entry-points described in the <b>Resource type interface</b>
   section.

   In the documentation below, responsibilities of generic and type specific
   parts of the resource manager are delineated.

   <b>Overview.</b>

   A resource (c2_rm_resource) is associated with various file system entities:

       - file meta-data. Rights to use this resource can be thought of as locks
         on file attributes that allow them to be cached or modified locally;

       - file data. Rights to use this resource are extents in the file plus
         access mode bits (read, write);

       - free storage space on a server (a "grant" in Lustre
         terminology). Right to use this resource is a reservation of a given
         number of bytes;

       - quota;

       - many more, see the HLD for examples.

   A resource owner (c2_rm_owner) represents a collection of rights to use a
   particular resource.

   To use a resource, a user of the resource manager creates an incoming
   resource request (c2_rm_incoming), that describes a wanted usage right
   (c2_rm_right_get())). Sometimes the request can be fulfilled immediately,
   sometimes it requires changes in the right ownership. In the latter case
   outgoing requests are directed to the remote resource owners (which typically
   means a network communication) to collect the wanted usage right at the
   owner. When an outgoing request reaches its target remote domain, an incoming
   request is created and processed (which in turn might result in sending
   further outgoing requests). Eventually, a reply is received for the outgoing
   request. When incoming request processing is complete, it "pins" the wanted
   right. This right can be used until the incoming request structure is
   destroyed (c2_rm_right_put()) and the pin is released.

   See the documentation for individual resource management data-types and
   interfaces for more detailed description of their behaviour.

   <b>Terminology.</b>

   Various terms are used to described right ownership flow in a cluster.

   Owners of rights for a particular resource are arranged in a cluster-wide
   hierarchy. This hierarchical arrangement depends on system structure (e.g.,
   where devices are connected, how network topology looks like) and dynamic
   system behaviour (how accesses to a resource are distributed).

   Originally, all rights on the resource belong to a single owner or a set of
   owners, residing on some well-known servers. Proxy servers request and cache
   rights from there. Lower level proxies and clients request rights in
   turn. According to the order in this hierarchy, one distinguishes "upward"
   and "downward" owners relative to a given one.

   In a given ownership transfer operation, a downward owner is "debtor" and
   upward owner is "creditor". The right being transferred is called a "loan"
   (note that this word is used only as a noun). When a right is transferred
   from a creditor to a debtor, the latter "borrows" and the former "sub-lets"
   the loan. When a right is transferred in the other direction, the creditor
   "revokes" and debtor "returns" the loan.

   A debtor can voluntary return a loan. This is called a "cancel" operation.

   <b>Concurrency control.</b>

   Generic resource manager makes no assumptions about threading model used by
   its callers. Generic resource data-structures and code are thread safe.

   3 types of locks protect all generic resource manager state:

       - per domain c2_rm_domain::rd_lock. This lock serialises addition and
         removal of resource types. Typically, it won't be contended much after
         the system start-up;

       - per resource type c2_rm_resource_type::rt_lock. This lock is taken
         whenever a resource or a resource owner is created or
         destroyed. Typically, that would be when a file system object is
         accessed which is not in the cache;

       - per resource owner c2_rm_owner::ro_lock. These locks protect a bulk of
         generic resource management state:

             - lists of possessed, borrowed and sub-let usage rights;

             - incoming requests and their state transitions;

             - outgoing requests and their state transitions;

             - pins (c2_rm_pin).

         Owner lock is accessed (taken and released) at least once during
         processing of an incoming request. Main owner state machine logic
         (owner_balance()) is structured in a way that is easily adaptable to a
         finer grained logic.

   None of these locks are ever held while waiting for a network communication
   to complete.

   Lock ordering: these locks do not nest.

   <b>Liveness.</b>

   None of the resource manager structures, except for c2_rm_resource, require
   reference counting, because their liveness is strictly determined by the
   liveness of an "owning" structure into which they are logically embedded.

   The resource structure (c2_rm_resource) can be shared between multiple
   resource owners (c2_rm_owner) and its liveness is determined by the
   reference counting (c2_rm_resource::r_ref).

   As in many other places in Colibri, liveness of "global" long-living
   structures (c2_rm_domain, c2_rm_resource_type) is managed by the upper
   layers which are responsible for determining when it is safe to finalise
   the structures. Typically, an upper layer would achieve this by first
   stopping and finalising all possible resource manager users.

   Similarly, a resource owner (c2_rm_owner) liveness is not explicitly
   determined by the resource manager. It is up to the user to determine when
   an owner (which can be associated with a file, or a client, or a similar
   entity) is safe to be finalised.

   When a resource owner is finalised (ROS_FINALISING) it tears down the credit
   network by revoking the loans it sublet to and by retuning the loans it
   borrowed from other owners.

   <b>Resource identification and location.</b>

   @see c2_rm_remote

   <b>Persistent state.</b>

   <b>Network protocol.</b>

   @see https://docs.google.com/a/xyratex.com/Doc?docid=0AQaCw6YRYSVSZGZmMzV6NzJfN2NiNXM1dHF3&hl=en

   @{
*/

/* import */
struct c2_vec_cursor;

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
   Domain of resource management.

   All other resource manager data-structures (resource types, resources,
   owners, rights, &c.) belong to some domain, directly or indirectly.

   Domains are needed to support multiple independent services within the same
   address space. Typically, there will be a domain per service, which means a
   domain per address space, however multiple services can, in principle, run in
   the same address space.
 */
struct c2_rm_domain {
        /**
           An array where resource types are registered. Protected by
           c2_rm_domain::rd_lock.

           @see c2_rm_resource_type::rt_id
         */
        struct c2_rm_resource_type *rd_types[C2_RM_RESOURCE_TYPE_ID_MAX];
        struct c2_mutex             rd_lock;
};

/**
   c2_rm_resource represents a resource identity (i.e., a name). Multiple
   copies of the same name may exist in different resource management
   domains, but no more than a single copy per domain.

   c2_rm_resource is allocated and destroyed by the appropriate resource
   type. An instance of c2_rm_resource would be typically embedded into a
   larger resource type specific structure containing details of resource
   identification.

   Generic code uses c2_rm_resource to efficiently compare resource
   identities.
 */
struct c2_rm_resource {
        struct c2_rm_resource_type      *r_type;
        const struct c2_rm_resource_ops *r_ops;
        /**
           Linkage to a list of all resources of this type, hanging off
           c2_rm_resource_type::rt_resources.
         */
        struct c2_list_link              r_linkage;
        /**
           Active references to this resource from resource owners
           (c2_rm_resource::r_type). Protected by
           c2_rm_resource_type::rt_lock.
         */
        uint32_t                         r_ref;
};

struct c2_rm_resource_ops {
        /**
           Called when a new right is allocated for the resource. The resource
           specific code should parse the right description stored in the
           buffer and fill c2_rm_right::ri_datum appropriately.
         */
        int (*rop_right_decode)(struct c2_rm_resource *resource,
                                struct c2_rm_right *right,
                                struct c2_vec_cursor *bufvec);
        void (*rop_policy)(struct c2_rm_resource *resource,
                           struct c2_rm_incoming *in);
};

/**
   Resources are classified into disjoint types.

   Resource type determines how its instances interact with the resource
   management generic core:

   - it determines how the resources of this type are named;

   - it determines how the resources of this type are located;

   - it determines what resource rights are defined on the resources of
     this type;

   - how rights are ordered;

   - how right conflicts are resolved.
 */
struct c2_rm_resource_type {
        const struct c2_rm_resource_type_ops *rt_ops;
        const char                           *rt_name;
        /**
           A resource type identifier, globally unique within a cluster, used
           to identify resource types on wire and storage.

           This identifier is used as an index in c2_rm_domain::rd_index.

           @todo currently this is assigned manually and centrally. In the
           future, resource types identifiers (as well as rpc item opcodes)
           will be assigned dynamically by a special service (and then
           announced to the clients). Such identifier name-spaces are
           resources themselves, so, welcome to a minefield of
           bootstrapping.
         */
        uint64_t                              rt_id;
        struct c2_mutex                       rt_lock;
        /**
           List of all resources of this type. Protected by
           c2_rm_resource_type::rt_lock.
         */
        struct c2_list                        rt_resources;
        /**
           Active references to this resource type from resource instances
           (c2_rm_owner::ro_resource). Protected by
           c2_rm_resource_type::rt_lock.
         */
        uint32_t                              rt_nr_resources;
        /**
           Domain this resource type is registered with.
         */
        struct c2_rm_domain                  *rt_dom;
};

struct c2_rm_resource_type_ops {
        bool (*rto_eq)(const struct c2_rm_resource *resource0,
                       const struct c2_rm_resource *resource1);
        int  (*rto_decode)(struct c2_vec_cursor *bufvec,
                           struct c2_rm_resource **resource);
        int  (*rto_encode)(struct c2_vec_cursor *bufvec,
                           struct c2_rm_resource **resource);
};

/**
   A resource owner uses the resource via a usage right (also called
   resource right or simply right as context permits). E.g., a client might
   have a right of a read-only or write-only or read-write access to a
   certain extent in a file. An owner is granted a right to use a resource.

   The meaning of a resource right is determined by the resource
   type. c2_rm_right is allocated and managed by the generic code, but it has a
   scratchpad field (c2_rm_right::ri_datum), where type specific code stores
   some additional information.

   A right can be something simple as a single bit (conveying, for example,
   an exclusive ownership of some datum) or a collection of extents tagged
   with access masks.

   A right is said to be "pinned" or "held" when it is necessary for some
   ongoing operation. A pinned right has RPF_PROTECT pins (c2_rm_pin) on its
   c2_rm_right::ri_pins list. Otherwise a right is simply "cached".

   Rights are typically linked into one of c2_rm_owner lists. Pinned rights can
   only happen on c2_rm_owner::ro_owned[OWOS_HELD] list. They cannot be moved
   out of this list until unpinned.
 */
struct c2_rm_right {
        struct c2_rm_resource        *ri_resource;
        const struct c2_rm_right_ops *ri_ops;
        /** resource type private field. By convention, 0 means "empty"
            right. */
        uint64_t                      ri_datum;
        /**
           Linkage of a right (and the corresponding loan, if applicable) to a
           list hanging off c2_rm_owner.
         */
        struct c2_list_link           ri_linkage;
        /**
           A list of pins, linked through c2_rm_pins::rp_right, stuck into this
           right.
         */
        struct c2_list                ri_pins;
};

struct c2_rm_right_ops {
        /**
           Called when the generic code is about to free a right. Type specific
           code releases any resources associated with the right.
         */
        void (*rro_free)(struct c2_rm_right *droit);
        int  (*rro_encode)(struct c2_rm_right *right,
                           struct c2_vec_cursor *bufvec);

        /** @name Rights ordering

            Rights on a given resource can be partially ordered by the
            "implies" relation. For example, a right to read a [0, 1000]
            extent of some file, implies a right to read a [0, 100] extent
            of the same file. This partial ordering is assumed to form a
            lattice (http://en.wikipedia.org/wiki/Lattice_(order)) possibly
            after a special "empty" right is introduced. Specifically, for
            any two rights on the same resource

            - their intersection ("meet") is defined as a largest right
              implied by both original rights and

            - their union ("join") is defined as a smallest right that implies
              both original rights.

            Rights A and B are equal if each implies the other. If A implies B,
            then their difference is defined as a largest right implied by A
            that has empty intersection with B.
         */
        /** @{ */
        /** intersection. This method updates r0 in place. */
        void (*rro_meet)   (struct c2_rm_right *r0,
                            const struct c2_rm_right *r1);
        /** True, iff r0 intersects with r1 */
        bool (*rro_intersects) (const struct c2_rm_right *r0,
                                const struct c2_rm_right *r1);
        /** union. This method updates r0 in place. */
        void (*rro_join)   (struct c2_rm_right *r0,
                            const struct c2_rm_right *r1);
        /**
            difference. This method updates r0 in place.

            @pre r0->ri_ops->rro_implies(r0, r1)
         */
        void (*rro_diff)   (struct c2_rm_right *r0,
                            const struct c2_rm_right *r1);
        /** true, iff r1 is "less than or equal to" r0. */
        bool (*rro_implies)(const struct c2_rm_right *r0,
                            const struct c2_rm_right *r1);
	/** Copy the resource type specific part. */
	void (*rro_copy)   (struct c2_rm_right *r0,
			    const struct c2_rm_right *r1);
        /** @} end of Rights ordering */
};

enum c2_rm_remote_state {
        REM_FREED = 0,
        REM_INITIALIZED,
        REM_SERVICE_LOCATING,
        REM_SERVICE_LOCATED,
        REM_RESOURCE_LOCATING,
        REM_RESOURCE_LOCATED
};

/**
   A representation of a resource owner from another domain.

   This is a generic structure.

   c2_rm_remote is a portal through which interaction with the remote resource
   owners is transacted. c2_rm_remote state transitions happen under its
   resource's lock.

   A remote owner is needed to borrow from or loan to an owner in a different
   domain. To establish the communication between local and remote owner the
   following stages are needed:

       - a service managing the remote owner must be located in the
         cluster. The particular way to do this depends on a resource type. For
         some resource types, the service is immediately known. For example, a
         "grant" (i.e., a reservation of a free storage space on a data
         service) is provided by the data service, which is already known by
         the time the grant is needed. For such resource types,
         c2_rm_remote::rem_state is initialised to REM_SERVICE_LOCATED. For
         other resource types, a distributed resource location data-base is
         consulted to locate the service. While the data-base query is going
         on, the remote owner is in REM_SERVICE_LOCATING state;

       - once the service is known, the owner within the service should be
         located. This is done generically, by sending a resource management
         fop to the service. The service responds with the remote owner
         identifier (c2_rm_remote::rem_id) used for further communications. The
         service might respond with an error, if the owner is no longer
         there. In this case, c2_rm_state::rem_state goes back to
         REM_SERVICE_LOCATING.

         Owner identification is an optional step, intended to optimise remote
         service performance. The service should be able to deal with the
         requests without the owner identifier. Because of this, owner
         identification can be piggy-backed to the first operation on the
         remote owner.

   @verbatim
             fini
        +----------------INITIALISED
        |                     |
        |                     | query the resource data-base
        |                     |
        |    TIMEOUT          V
        +--------------SERVICE_LOCATING<----+
        |                     |             |
        |                     | reply: OK   |
        |                     |             |
        V    fini             V             |
      FREED<-----------SERVICE_LOCATED      | reply: moved
        ^                     |             |
        |                     | get id      |
        |                     |             |
        |    TIMEOUT          V             |
        +--------------RESOURCE_LOCATING----+
        |                     |
        |                     | reply: id
        |                     |
        |    fini             V
        +--------------RESOURCE_LOCATED

   @endverbatim
 */
struct c2_rm_remote {
        enum c2_rm_remote_state  rem_state;
        /**
           A resource for which the remote owner is represented.
         */
        struct c2_rm_resource   *rem_resource;
        /** A channel to signal state changes. */
        struct c2_chan           rem_signal;
        /** A service to be contacted to talk with the remote owner. Valid in
            states starting from REM_SERVICE_LOCATED. */
        struct c2_service_id     rem_service;
        /** An identifier of the remote owner within the service. Valid in
            REM_RESOURCE_LOCATED state. This identifier is generated by the
            resource manager service. */
        uint64_t                 rem_id;
};

/**
   A group of cooperating owners.

   The owners in a group coordinate their activities internally (by means
   outside of resource manager control) as far as resource management is
   concerned.

   Resource manager assumes that rights granted to the owners from the same
   group never conflict.

   Typical usage is to assign all owners from the same distributed
   transaction (or from the same network client) to a group. The decision
   about a group scope has concurrency related implications, because the
   owners within a group must coordinate access between themselves to
   maintain whatever scheduling properties are desired, like serialisability.
 */
struct c2_rm_group {
};

/**
   c2_rm_owner state machine states.
 */
enum c2_rm_owner_state {
        /**
            Terminal and initial state.

            In this state owner rights lists are empty (including incoming and
            outgoing request lists).
         */
        ROS_FINAL = 1,
        /**
           Initial network setup state:

               - registering with the resource data-base;

               - &c.
         */
        ROS_INITIALISING,
        /**
           Active request processing state. Once an owner reached this state it
           must pass through the finalising state.
         */
        ROS_ACTIVE,
        /**
           No new requests are allowed in this state.

           The owner collects from debtors and repays creditors.
         */
        ROS_FINALISING
};

enum {
        /**
           Incoming requests are assigned a priority (greater numerical value
           is higher). When multiple requests are ready to be fulfilled, higher
           priority ones have a preference.
         */
        C2_RM_REQUEST_PRIORITY_MAX = 3,
        C2_RM_REQUEST_PRIORITY_NR
};

/**
   c2_rm_owner::ro_owned[] list of usage rights possessed by the owner is split
   into sub-lists enumerated by this enum.
 */
enum c2_rm_owner_owned_state {
        /**
           Sub-list of pinned rights.

           @see c2_rm_right
         */
        OWOS_HELD,
        /**
           Not-pinned right is "cached". Such right can be returned to an
           upward owner from which it was previously borrowed (i.e., right can
           be "cancelled") or sub-let to downward owners.
         */
        OWOS_CACHED,
        OWOS_NR
};

/**
   Lists of incoming and outgoing requests are subdivided into sub-lists.
 */
enum c2_rm_owner_queue_state {
        /**
           "Ground" request is not excited.
         */
        OQS_GROUND,
        /**
           Excited requests are those for which something has to be done. An
           outgoing request is excited when it completes (or times out). An
           incoming request is excited when it's ready to go from RI_WAIT to
           RI_CHECK state.

           Resource owner state machine goes through lists of excited requests
           processing them. This processing can result in more excitement
           somewhere, but eventually terminates.

           @see http://en.wikipedia.org/wiki/Excited_state
         */
        OQS_EXCITED,
        OQS_NR
};

/**
   Resource ownership is used for two purposes:

    - concurrency control. Only resource owner can manipulate the resource
      and ownership transfer protocol assures that owners do not step on
      each other. That is, resources provide traditional distributed
      locking mechanism;

    - replication control. Resource owner can create a (local) copy of a
      resource. The ownership transfer protocol with the help of version
      numbers guarantees that multiple replicas are re-integrated
      correctly. That is, resources provide a cache coherency
      mechanism. Global cluster-wide cache management policy can be
      implemented on top of resources.

   A resource owner possesses rights on a particular resource. Multiple
   owners within the same domain can possess rights on the same resource,
   but no two owners in the cluster can possess conflicting rights at the
   same time. The last statement requires some qualification:

    - "time" here means the logical time in an observable history of the
      file system. It might so happen, that at a certain moment in physical
      time, data-structures (on different nodes, typically) would look as
      if conflicting rights were granted, but this is only possible when
      such rights will never affect visible system behaviour (e.g., a
      consensual decision has been made by that time to evict one of the
      nodes);

    - in a case of optimistic conflict resolution, "no conflicting rights"
      means "no rights on which conflicts cannot be resolved afterwards by
      the optimistic conflict resolution policy".

   c2_rm_owner is a generic structure, created and maintained by the
   generic resource manager code.

   Off a c2_rm_owner hang several lists and arrays of lists for rights
   book-keeping: c2_rm_owner::ro_borrowed, c2_rm_owner::ro_sublet and
   c2_rm_owner::ro_owned[], further subdivided by states.

   As rights form a lattice (see c2_rm_right_ops), it is always possible to
   represent the cumulative sum of all rights on a list as a single
   c2_rm_right. The reason the lists are needed is that rights in the lists
   have some additional state associated with them (e.g., loans for
   c2_rm_owner::ro_borrowed, c2_rm_owner::ro_sublet or pins
   (c2_rm_right::ri_pins) for c2_rm_owner::ro_owned[]) that can be manipulated
   independently.

   Owner state diagram:

   @verbatim

                              +----->FINAL<---------+
                              |        |            |
                              |        |            |
                              |        V            |
                              +---INITIALISING      |
                                       |            |
                                       |            |
                                       |            |
                                       V            |
                                     ACTIVE         |
                                       |            |
                                       |            |
                                       |            |
                                       V            |
                                   FINALISING-------+

   @endverbatim

   @invariant under ->ro_lock { // keep books balanced at all times
           join of rights on ->ro_owned[] and
                   rights on ->ro_sublet equals to
           join of rights on ->ro_borrowed           &&

           meet of (join of rights on ->ro_owned[]) and
                   (join of rights on ->ro_sublet) is empty.
   }

   @invariant under ->ro_lock {
           ->ro_owned[OWOS_HELD] is exactly the list of all held rights (ones
           with elevated user count)
   }
 */
struct c2_rm_owner {
        enum c2_rm_owner_state ro_state;
        /**
           Resource this owner possesses the rights on.
         */
        struct c2_rm_resource *ro_resource;
        /**
           A group this owner is part of.

           If this is NULL, the owner is not a member of any group (a
           "standalone" owner).
         */
        struct c2_rm_group    *ro_group;
        /**
           A list of loans, linked through c2_rm_loan::rl_right:ri_linkage that
           this owner borrowed from other owners.
         */
        struct c2_list         ro_borrowed;
        /**
           A list of loans, linked through c2_rm_loan::rl_right:ri_linkage that
           this owner extended to other owners. Rights on this list are not
           longer possessed by this owner: they are counted in
           c2_rm_owner::ro_borrowed, but not in c2_rm_owner::ro_owned.
         */
        struct c2_list         ro_sublet;
        /**
           A list of rights, linked through c2_rm_right::ri_linkage possessed
           by the owner.
         */
        struct c2_list         ro_owned[OWOS_NR];
        /**
           An array of lists, sorted by priority, of incoming requests, not yet
           satisfied. Requests are linked through
           c2_rm_incoming::rin_want::ri_linkage.
         */
        struct c2_list         ro_incoming[C2_RM_REQUEST_PRIORITY_NR][OQS_NR];
        /**
           An array of lists, of outgoing, not yet completed, requests.
         */
        struct c2_list         ro_outgoing[OQS_NR];
        struct c2_mutex        ro_lock;
};

/**
   A loan (of a right) from one owner to another.

   c2_rm_loan is always on some list (to which it is linked through
   c2_rm_loan::rl_right:ri_linkage field) in an owner structure. This owner is
   one party of the loan. Another party is c2_rm_loan::rl_other. Which party is
   creditor and which is debtor is determined by the list the loan is on.
 */
struct c2_rm_loan {
        struct c2_rm_right  rl_right;
        /**
           Other party in the loan. Either an "upward" creditor or "downward"
           debtor.
         */
        struct c2_rm_remote rl_other;
        /**
           A identifier generated by the remote end that should be passed back
           whenever operating on a loan (think loan agreement number).
         */
        uint64_t            rl_id;
};

/**
   States of incoming request. See c2_rm_incoming for description.
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
            or release of a locally held usage right. */
        RI_WAIT
};

/**
   Types of an incoming usage right request.
 */
enum c2_rm_incoming_type {
        /**
           A request for a usage right from a local user. When the request
           succeeds, the right is held by the owner.
         */
        RIT_LOCAL,
        /**
           A request to loan a usage right to a remote owner. Fulfillment of
           this request might cause further outgoing requests to be sent, e.g.,
           to revoke rights sub-let to remote owner.
         */
        RIT_LOAN,
        /**
           A request to return a usage right previously sub-let to this owner.
         */
        RIT_REVOKE
};

/**
   Some universal (i.e., not depending on a resource type) granting policies.
 */
enum c2_rm_incoming_policy {
        RIP_NONE = 1,
        /**
           If possible, don't insert a new right into the list of possessed
           rights. Instead, pin possessed rights overlapping with the requested
           right.
         */
        RIP_INPLACE,
        /**
           Insert a new right into the list of possessed rights, equal to the
           requested right.
         */
        RIP_STRICT,
        /**
           ...
         */
        RIP_JOIN,
        /**
           Grant maximal possible right, not conflicting with others.
         */
        RIP_MAX,
        RIP_RESOURCE_TYPE_BASE
};

/**
   Flags controlling incoming usage right request processing. These flags are
   stored in c2_rm_incoming::rin_flags and analysed in c2_rm_right_get().
 */
enum c2_rm_incoming_flags {
        /**
           Previously sub-let rights may be revoked, if necessary, to fulfill
           this request.
         */
        RIF_MAY_REVOKE = (1 << 0),
        /**
           More rights may be borrowed, if necessary, to fulfill this request.
         */
        RIF_MAY_BORROW = (1 << 1),
        /**
           The interaction between the request and locally possessed rights is
           the following:

               - by default, locally possessed rights are ignored. This scenario
                 is typical for a local request (RIT_LOCAL), because local users
                 resolve conflicts by some other means (usually some form of
                 concurrency control, like locking);

               - if RIF_LOCAL_WAIT is set, the request can be fulfilled only
                 once there is no locally possessed rights conflicting with the
                 wanted right. This is typical for a remote request (RIT_LOAN or
                 RIT_REVOKE);

               - if RIF_LOCAL_TRY is set, the request will be immediately
                 denied, if there are conflicting local rights. This allows to
                 implement a "try-lock" like functionality.
         */
        RIF_LOCAL_WAIT = (1 << 2),
        /**
           Fail the request if it cannot be fulfilled because of the local
           conflicts.

           @see RIF_LOCAL_WAIT
         */
        RIF_LOCAL_TRY  = (1 << 3),
};

/**
   Resource usage right request.

   The same c2_rm_incoming structure is used to track state of the incoming
   requests both "local", i.e., from the same domain where the owner resides
   and "remote".

   An incoming request is created for

       - local right request, when some user wants to use the resource;

       - remote right request from a "downward" owner which asks to sub-let
         some rights;

       - remote right request from an "upward" owner which wants to revoke some
         rights.

   These usages are differentiated by c2_rm_incoming::rin_type.

   An incoming request is a state machine, going through the following stages:

       - [CHECK]   this stage determines whether the request can be fulfilled
                   immediately. Local request can be fulfilled immediately if
                   the wanted right is possessed by the owner, that is, if
                   in->rin_want is implied by a join of owner->ro_owned[].

                   A non-local (loan or revoke) request can be fulfilled
                   immediately if the wanted right is implied by a join of
                   owner->ro_owned[OWOS_CACHED], that is, if the owner has
                   enough rights to grant the loan and the wanted right does
                   not conflict with locally held rights.

       - [POLICY]  If the request can be fulfilled immediately, the "policy" is
                   invoked which decides which right should be actually grated,
                   sublet or revoked. That right can be larger than
                   requested. A policy is, generally, resource type dependent,
                   with a few universal policies defined by enum
                   c2_rm_incoming_policy.

       - [SUCCESS] Finally, fulfilled request succeeds.

       - [ISSUE]   Otherwise, if the request can not be fulfilled immediately,
                   "pins" (c2_rm_pin) are added which will notify the request
                   when the fulfillment check might succeed.

                   Pins are added to:

                       - every conflicting right held by this owner (when
                         RIF_LOCAL_WAIT flag is set on the request and always
                         for a remote request);

                       - outgoing requests to revoke conflicting rights sub-let
                         to remote owners (when RIF_MAY_REVOKE flag is set);

                       - outgoing requests to borrow missing rights from remote
                         owners (when RIF_MAY_BORROW flag is set);

                   Outgoing requests mentioned above are created as necessary
                   in the ISSUE stage.

       - [CYCLE]   When all the pins stuck in the ISSUE state are released
                   (either when a local right is released or when an outgoing
                   request completes), go back to the CHECK state.

   Looping back to the CHECK state is necessary, because possessed rights are
   not "pinned" during wait and can go away (be revoked or sub-let). The rights
   are not pinned to avoid dependencies between rights that can lead to
   dead-locks and "cascading evictions". The alternative is to pin rights and
   issue outgoing requests synchronously one by one and in a strict order (to
   avoid dead-locks). The rationale behind current decision is that probability
   of a live-lock is low enough and the advantage of issuing concurrent
   asynchronous outgoing requests is important.

   @todo Should live-locks prove to be a practical issue, RPF_BARRIER pins can
   be used to reduce concurrency and assure state machine progress.

   It's a matter of policy how many outgoing requests are sent out in ISSUE
   state. The fewer requests are sent, the more CHECK-ISSUE-WAIT loop
   iterations would typically happen. An extreme case of sending no more than a
   single request is also possible and has some advantages: outgoing request
   can be allocated as part of incoming request, simplifying memory management.

   It is also a matter is policy, how exactly the request is satisfied after a
   successful CHECK state. Suppose, for example, that the owner possesses
   rights R0 and R1 such that wanted right W is implied by join(R0, R1), but
   neither R0 nor R1 alone imply W. Some possible CHECK outcomes are:

       - increase user counts in both R0 and R1;

       - insert a new right equal to W into owner->ro_owned[];

       - insert a new right equal to join(R0, R1) into owner->ro_owned[].

   All have their advantages and drawbacks:

       - elevating R0 and R1 user counts keeps owner->ro_owned[] smaller, but
         pins more rights than strictly necessary;

       - inserting W behaves badly in a standard use case where a thread doing
         sequential IO requests a right on each iteration;

       - inserting the join pins more rights than strictly necessary.

   All policy questions are settled by per-request flags and owner settings,
   based on access pattern analysis.

   Following is a state diagram, where are stages that are performed without
   blocking (for network communication) are lumped into a single state:

   @verbatim
                                   SUCCESS
                                      ^
               too many iterations    |
                    live-lock         |    last completion
                  +-----------------CHECK<-----------------+
                  |                   |                    |
                  |                   |                    |
                  V                   |                    |
               FAILURE                | pins placed        |
                  ^                   |                    |
                  |                   |                    |
                  |                   V                    |
                  +----------------WAITING-----------------+
                       timeout      ^   |
                                    |   | completion
                                    |   |
                                    +---+
   @endverbatim

   c2_rm_incoming fields and state transitions are protected by the owner's
   mutex.

   @note a cedent can grant a usage right larger than requested.

   @todo a new type of incoming request RIT_GRANT (RIT_FOIEGRAS?) can be added
   to forcibly grant new rights to the owner, for example, as part of a
   coordinated global distributed resource usage balancing between
   owners. Processing of requests of this type would be very simple, because
   adding new rights never blocks. Similarly, a new outgoing request type
   ROT_TAKE could be added.
 */
struct c2_rm_incoming {
        enum c2_rm_incoming_type         rin_type;
        enum c2_rm_incoming_state        rin_state;
        enum c2_rm_incoming_policy       rin_policy;
        enum c2_rm_incoming_flags        rin_flags;
        struct c2_rm_owner              *rin_owner;
        /** The right requested. */
        struct c2_rm_right               rin_want;
        /**
           List of pins, linked through c2_rm_pin::rp_incoming_linkage, for all
           rights held to satisfy this request.

           @invariant meaning of this list depends on the request state:

               - RI_CHECK, RI_SUCCESS: a list of pins on rights in
                 ->rin_owner->ro_owned[];

               - RI_ISSUE, RI_WAIT: a list of pins on outgoing requests
                 (through c2_rm_outgoing::rog_want::rl_right::ri_pins) and held
                 rights in ->rin_owner->ro_owned[OWOS_HELD];

               - other states: empty.
         */
        struct c2_list                   rin_pins;
        /**
           Request priority from 0 to C2_RM_REQUEST_PRIORITY_MAX.
         */
        int                              rin_priority;
        const struct c2_rm_incoming_ops *rin_ops;
        struct c2_chan                   rin_signal;
};

/**
   Operations assigned by a resource manager user to an incoming
   request. Resource manager calls methods in this operation vector when events
   related to the request happen.
 */
struct c2_rm_incoming_ops {
        /**
           This is called when incoming request processing completes either
           successfully (rc == 0) or with an error (-ve rc).
         */
        void (*rio_complete)(struct c2_rm_incoming *in, int32_t rc);
        /**
           This is called when a request arrives that conflicts with the right
           held by this incoming request.
         */
        void (*rio_conflict)(struct c2_rm_incoming *in);
};

/**
   Types of outgoing requests sent by the request manager.
 */
enum c2_rm_outgoing_type {
	/**
	   A request to borrow a right from an upward resource owner. This
	   translates into a RIT_LOAN incoming request there.
	 */
	ROT_BORROW = 1,
	/**
	   A request returning a previously borrowed right. This is sent in
	   response to an incoming RIT_REVOKE request.
	 */
	ROT_CANCEL,
	/**
	   A request to return previously borrowed right. This translates into
	   a RIT_REVOKE incoming request on the remote owner.
	 */
	ROT_REVOKE
};

/**
   An outgoing request is created on behalf of some incoming request to track
   the state of right transfer with some remote domain.

   An outgoing request is created to:

       - borrow a new right from some remote owner (an "upward" request) or

       - revoke a right sublet to some remote owner (a "downward" request) or

       - cancel this owner's right and return it to an upward owner.

   Before a new outgoing request is created, a list of already existing
   outgoing requests (c2_rm_owner::ro_outgoing) is scanned. If an outgoing
   request of a matching type for a greater or equal right exists, new request
   is not created. Instead, the incoming request pins existing outgoing
   request.

   c2_rm_outgoing fields and state transitions are protected by the owner's
   mutex.
 */
struct c2_rm_outgoing {
        enum c2_rm_outgoing_type rog_type;
        struct c2_rm_owner      *rog_owner;
        /** a right that is to be transferred. */
        struct c2_rm_loan        rog_want;
};

enum c2_rm_pin_flags {
        RPF_TRACK   = (1 << 0),
        RPF_PROTECT = (1 << 1),
        RPF_BARRIER = (1 << 2)
};

/**
   A pin is used to

       - RPF_TRACK: track when a right changes its state;

       - RPF_PROTECT: to protect a right from revocation;

       - RPF_BARRIER: to prohibit RPF_PROTECT pins from being added to the
         right.

   Fields of this struct are protected by the owner's lock.

   Abstractly speaking, pins allow N:M (many to many) relationships between
   incoming requests and rights: an incoming request has a list of pins "from"
   it and a right has a list of pins "to" it. Let's look at the typical use
   cases.

   <b>Protection.</b>

   While a right is actively used, it cannot be revoked. For example, while file
   write is going on, the right to write in the target file extent must be
   held. A right is held (or pinned) from the return from c2_rm_right_get()
   until the matching call to c2_rm_right_put(). To mark the right as pinned,
   c2_rm_right_get() adds a RPF_PROTECT pin from the incoming request to the
   returned right (generally, more than one right can be pinned as result on
   c2_rm_right_get()). This pin is removed by the call to
   c2_rm_right_put(). Multiple incoming requests can pin the same right.

   <b>Tracking.</b>

   An incoming request with a RIF_LOCAL_WAIT flag might need to wait until a
   conflicting pinned right becomes unpinned. To this end, an RPF_TRACK pin is
   added from the incoming request to the right.

   When the last RPF_PROTECT pin is removed from a right, the right becomes
   "cached" and the list of pins to the right is scanned. For each RPF_TRACK pin
   on the list, its incoming request is checked to see whether this was the
   last tracking pin the request is waiting for.

   An incoming request might also issue an outgoing request to borrow or revoke
   some rights, necessary to fulfill the request. An RPF_TRACK pin is added from
   the incoming request to the right embedded in the outgoing request
   (c2_rm_outgoing::rog_want::rl_right). Multiple incoming requests can pin the
   same outgoing request. When the outgoing request completes, the incoming
   requests waiting for it are checked as above.

   <b>Barrier.</b>

   Not currently used. The idea is to avoid live-locks and guarantee progress of
   incoming request processing by pinning the rights with a RPF_BARRIER pin.

   @verbatim


        ->ro_owned[]--->R------>R        R<------R<----------+
                        |       |        |       |           |
 ->ro_incoming[]        |       |        |       |           |
        |               |       |        |       |           |
        |               |       |        |       |    ->ro_outgoing[]
        V               |       |        |       |
    INC[CHECK]----------T-------T--------T-------T
        |                       |                |
        |                       |                |
        V                       |                |
    INC[SUCCESS]----------------P                |
        |                                        |
        |                                        |
        V                                        |
    INC[CHECK]-----------------------------------T

   @endverbatim

   On this diagram, INC[S] is an incoming request in a state S, R is a right, T
   is an RPF_TRACK pin and P is an RPF_PROTECT pin.

   The incoming request in the middle has been processed successfully and now
   protects its right.

   The topmost incoming request waits for 2 possessed rights to become unpinned
   and also waiting for completion of 2 outgoing requests. The incoming request
   an the bottom waits for completion of the same outgoing request.

   c2_rm_right_put() scans the request's pin list (horizontal direction) and
   removes all pins. If the last pin was removed from a right, right's pin list
   is scanned (vertical direction), checking incoming requests for possible
   state transitions.
 */
struct c2_rm_pin {
        uint32_t               rp_flags;
        struct c2_rm_right    *rp_right;
        /** An incoming request that stuck this pin. */
        struct c2_rm_incoming *rp_incoming;
        /** Linkage into a list of all pins for a right, hanging off
            c2_rm_right::ri_pins. */
        struct c2_list_link    rp_right_linkage;
        /** Linkage into a list of all pins, held to satisfy an incoming
            request. This list hangs off c2_rm_incoming::rin_pins. */
        struct c2_list_link    rp_incoming_linkage;
};

struct c2_rm_lease {
};

void c2_rm_domain_init(struct c2_rm_domain *dom);
void c2_rm_domain_fini(struct c2_rm_domain *dom);

/**
   Register a resource type with a domain.

   @pre  rtype->rt_id == C2_RM_RESOURCE_TYPE_ID_INVALID &&
         rtype->rt_dom == NULL
   @post IS_IN_ARRAY(rtype->rt_id, dom->rd_types) && rtype->rt_dom == dom
 */
void c2_rm_type_register(struct c2_rm_domain *dom,
                         struct c2_rm_resource_type *rt);

/**
   Deregister a resource type.

   @pre  IS_IN_ARRAY(rtype->rt_id, dom->rd_types) && rtype->rt_dom != NULL
   @post rtype->rt_id == C2_RM_RESOURCE_TYPE_ID_INVALID &&
         rtype->rt_dom == NULL
 */
void c2_rm_type_deregister(struct c2_rm_resource_type *rtype);

/**
   Adds a resource to the list of resources and increment resource type
   reference count.

   @pre c2_list_is_empty(res->r_linkage) && res->r_ref == 0
   @pre rtype->rt_resources does not contain a resource equal (in the
        c2_rm_resource_type_ops::rto_eq() sense) to res

   @post res->r_ref > 0
   @post res->r_type == rtype
   @post c2_list_contains(&rtype->rt_resources, &res->r_linkage)
 */
void c2_rm_resource_add(struct c2_rm_resource_type *rtype,
                        struct c2_rm_resource *res);
/**
   Removes a resource from the list of resources. Dual to c2_rm_resource_add().

   @pre res->r_type->rt_nr_resources > 0
   @pre c2_list_contains(&rtype->rt_resources, &res->r_linkage)

   @post !c2_list_contains(&rtype->rt_resources, &res->r_linkage)
 */
void c2_rm_resource_del(struct c2_rm_resource *res);

/**
   Initialises owner fields and increments resource reference counter.

   The owner's right lists are initially empty.

   @pre owner->ro_state == ROS_FINAL

   @post owner->ro_state == ROS_INITIALISING ||
         owner->ro_state == ROS_ACTIVE) &&
         owner->ro_resource == res)
 */
void c2_rm_owner_init(struct c2_rm_owner *owner, struct c2_rm_resource *res);

/**
   Initialises owner so that it initially possesses @r.

   @see c2_rm_owner_init()

   @post owner->ro_state == ROS_INITIALISING ||
         owner->ro_state == ROS_ACTIVE) &&
         owner->ro_resource == res)
   @post c2_list_contains(&owner->ro_owned[OWOS_CACHED],
                          &r->ri_linkage))
 */
void c2_rm_owner_init_with(struct c2_rm_owner *owner,
                          struct c2_rm_resource *res, struct c2_rm_right *r);
/**
   Finalises the owner. Dual to c2_rm_owner_init().

   @pre owner->ro_state == ROS_FINAL
   @pre c2_list_is_empty(owner->ro_borrowed) &&
   c2_list_is_empty(owner->ro_sublet) &&
                           c2_list_is_empty(owner->ro_owned[*]) &&
                           c2_list_is_empty(owner->ro_incoming[*][*]) &&
                           c2_list_is_empty(owner->ro_outgoing[*]) &&

 */
void c2_rm_owner_fini(struct c2_rm_owner *owner);

/**
   Initialises generic fields in @right.
 */
void c2_rm_right_init(struct c2_rm_right *right);
/**
   Finalised generic fields in @right. Dual to c2_rm_right_init().
 */
void c2_rm_right_fini(struct c2_rm_right *right);

/**
   Initialises the fields of @in.
 */
void c2_rm_incoming_init(struct c2_rm_incoming *in);
/**
   Finalises the fields of @in. Dual to c2_rm_incoming_init().
 */
void c2_rm_incoming_fini(struct c2_rm_incoming *in);

/**
   Starts a state machine for a resource usage right request. Will add pins for
   this request. Asynchronous operation - the right will not generally be held
   at exit.

   @pre IS_IN_ARRAY(in->rin_priority, owner->ro_incoming)
   @pre in->rin_state == RI_INITIALISED
   @pre c2_list_is_empty(&in->rin_want.ri_linkage)

 */
void c2_rm_right_get(struct c2_rm_owner *owner, struct c2_rm_incoming *in);

/**
   Waits until @in enters RI_SUCCESS or RI_FAILURE state or deadline expires.

   @post ergo(result == 0, in->rin_state == RI_SUCCESS ||
                           in->rin_state == RU_FAILURE)
 */
int c2_rm_right_timedwait(struct c2_rm_incoming *in,
			  const c2_time_t deadline);

/**
   A helper function combining c2_rm_right_get() and c2_rm_right_timedwait()
   with an infinite timeout.
 */
int c2_rm_right_get_wait(struct c2_rm_incoming *in);

/**
   Releases the right pinned by @in.

   @pre in->rin_state == RI_SUCCESS
   @post c2_list_empty(&in->rin_pins)
 */
void c2_rm_right_put(struct c2_rm_incoming *in);

/**
   Called when an outgoing request completes (possibly with an error, like a
   timeout).
*/
void c2_rm_outgoing_complete(struct c2_rm_outgoing *og, int rc);

/**
   @name Resource type interface
 */
/** @{ */

/** @} end of Resource type interface */

/**
   @name Resource manager networking
 */
/** @{ */

/**
   Constructs a remote owner associated with @right.

   After this function returns, @other is in the process of locating the remote
   service and remote owner, as described in the comment on c2_rm_remote.

   @post ergo(result == 0, other->rem_resource == right->ri_resource)
 */
int c2_rm_net_locate(struct c2_rm_right *right, struct c2_rm_remote *other);

/** @} end of Resource manager networking */

/** @} end of rm group */

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
