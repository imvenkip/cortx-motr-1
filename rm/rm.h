/* -*- C -*- */

#ifndef __COLIBRI_RM_RM_H__
#define __COLIBRI_RM_RM_H__

#include "net/net.h"         /* c2_service_id */
#include "lib/list.h"
#include "lib/mutex.h"
#include "lib/chan.h"

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

   To use a resource, an incoming resource request is created (c2_rm_incoming),
   that describes a wanted usage right. Sometimes the request can be fulfilled
   immediately, sometimes it takes a network communication to gather the wanted
   usage right at the owner. When incoming request processing is complete, it
   "pins" the wanted right. This right can be used until the request structure
   is destroyed and the pin is released.

   See the documentation for individual resource management data-types and
   interfaces for more detailed description of their behaviour.

   <b>Resource identification and location.</b>

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

   Domains are needed to support multiple independent services within the
   same address space.
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
	int (*rto_encode)(struct c2_vec_cursor *bufvec,
			  struct c2_rm_resource **resource);
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
	uint32_t                              rt_ref;
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

   A right is said to be "pinned" or "held" when its c2_rm_right::ri_users
   count is greater than 0.

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

   c2_rm_remote as a portal through which interaction with the remote resource
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
	|           	      |
	|    	       	      | query the resource data-base
	|    	       	      |
       	|    TIMEOUT   	      V
       	+--------------SERVICE_LOCATING<----+
	|	       	      |		    |
	|	       	      | reply: OK   |
	|	       	      |		    |
	V    fini      	      V		    |
      FREED<-----------SERVICE_LOCATED	    | reply: moved
	^	       	      |		    |
	|	       	      |	get id	    |
	|      	       	      |		    |
	|    TIMEOUT   	      V		    |
	+--------------RESOURCE_LOCATING----+
	|	       	      |
	|	       	      | reply: id
	|	       	      |
	|    fini      	      V
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

enum {
	C2_RM_REQUEST_PRIORITY_MAX = 3,
	C2_RM_REQUEST_PRIORITY_NR
};

enum c2_rm_owner_owned_state {
	OWOS_HELD,
	OWOS_CACHED,
	OWOS_NR
};

enum c2_rm_owner_queue_state {
	OQS_GROUND,
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

   Owners of rights for a particular resource are arranged in a cluster-wide
   hierarchy. Originally, all rights belong to a single owner (or a set of
   owners), residing on some well-known servers. Proxy servers request and
   cache rights from there. Lower level proxies and client request rights in
   turn. According to the order in this hierarchy, one distinguishes "upward"
   and "downward" owners relative to a given one.

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
	   c2_rm_owner::ro_granted, but not in c2_rm_owner::ro_owned.
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
	   c2_rm_incoming::rin_want::rl_right:ri_linkage.
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
	   Don't insert a new right into the list of possessed rights. Instead,
	   pin possessed rights overlapping with the requested right.
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

enum c2_rm_incoming_flags {
	RIF_MAY_REVOKE = (1 << 0),
	RIF_MAY_BORROW = (1 << 1),
	RIF_WAIT_LOCAL = (1 << 2)
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
                         RIF_WAIT_LOCAL flag is set on the request and always
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
		    live-lock         |	   last completion
       	          +-----------------CHECK<-----------------+
		  |    	       	      |                    |
		  |    	      	      |	                   |
		  V    	      	      |	                   |
	       FAILURE                | pins placed        |
	       	  ^    	      	      |                    |
	       	  |    	      	      |	  		   |
	       	  |    	      	      V	       	       	   |
	          +----------------WAITING-----------------+
		       timeout	    ^  	|
       	       	       	       	    |  	| completion
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
	enum c2_rm_incoming_type   rin_type;
	enum c2_rm_incoming_state  rin_state;
	enum c2_rm_incoming_policy rin_policy;
	enum c2_rm_incoming_flags  rin_flags;
	struct c2_rm_owner        *rin_owner;
	/** The right requested. */
	struct c2_rm_right         rin_want;
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
	struct c2_list             rin_pins;
	/**
	   Request priority from 0 to C2_RM_REQUEST_PRIORITY_MAX.
	 */
	int                        rin_priority;
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
}

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

       - track when a right (or an object such as a loan or outgoing request
         which the right is embedded into) changes its state;

       - to protect a right from revocation;

       - to prohibit RPF_PROTECT pins from being added to the right.

   Fields of this struct are protected by the owner's lock.
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
   @post ergo(result == 0, IS_IN_ARRAY(rtype->rt_id, dom->rd_types) &&
                           rtype->rt_dom == dom)
 */
int c2_rm_register(struct c2_rm_domain *dom, struct c2_rm_resource_type *rt);

/**
   Deregister a resource type.

   @pre  IS_IN_ARRAY(rtype->rt_id, dom->rd_types) && rtype->rt_dom != NULL
   @post rtype->rt_id == C2_RM_RESOURCE_TYPE_ID_INVALID &&
         rtype->rt_dom == NULL
 */
void c2_rm_deregister(struct c2_rm_resource_type *rtype);

/**
   @name Resource type interface
 */
/** @{ */

/** @} end of Resource type interface */

/**
   @name Resource manager networking
 */
/** @{ */

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
