/* -*- C -*- */

#ifndef __COLIBRI_RM_RM_H__
#define __COLIBRI_RM_RM_H__

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

   <b>Resource identification and location.</b>

   @see https://docs.google.com/a/xyratex.com/Doc?docid=0AQaCw6YRYSVSZGZmMzV6NzJfN2NiNXM1dHF3&hl=en

   @{
*/

struct c2_rm_domain;
struct c2_rm_resource;
struct c2_rm_resource_ops;
struct c2_rm_resource_type;
struct c2_rm_resource_type_ops;
struct c2_rm_owner;
struct c2_rm_owner_proxy;
struct c2_rm_loan;
struct c2_rm_group;
struct c2_rm_right;
struct c2_rm_right_ops;
struct c2_rm_request;
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

enum c2_rm_resource_state {
	RES_FREE = 1,
	RES_LOCATING,
	RES_CACHED,
	RES_USED
};

/**
   A representation of a resource owner from another domain.

   This is a generic structure.

   c2_rm_resource as a portal through which interaction with the remote
   resource owners is transacted.
 */
struct c2_rm_remote {
	struct c2_service_id rem_service;
	uint64_t             rem_cookie;
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
	struct c2_chan                   r_signal;
	enum c2_rm_resource_state        r_state;
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
			  struct c2_resource **resource);
	/**
	   Called when a new right is allocated for the resource. The resource
	   specific code should parse the right description stored in the
	   buffer and fill c2_rm_right::ri_datum appropriately.
	 */
	int (*rop_right_decode)(struct c2_rm_resource *resource,
				struct c2_rm_right *right,
				struct c2_vec_cursor *bufvec);
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
	bool (*rto_eq)(const struct c2_rm_resource_type *rt0,
		       const struct c2_rm_resource_type *rt1);
	int  (*rto_decode)(struct c2_vec_cursor *bufvec,
			   struct c2_resource **resource);
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
 */
struct c2_rm_right {
	struct c2_rm_resource        *ri_resource;
	const struct c2_rm_right_ops *ri_ops;
	uint64_t                      ri_datum;
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
              that implies both original rights and

	    - their union ("join") is defined as a smallest right implied
              by both original rights.

	    Rights A and B are equal of each implies the other.
	 */
	/** @{ */
	/** intersection. This method updates r0 in place. */
	void (*rro_meet)   (struct c2_rm_right *r0,
			    const struct c2_rm_right *r1);
	/** union. This method updates r0 in place. */
	void (*rro_join)   (struct c2_rm_right *r0,
			    const struct c2_rm_right *r1);
	/** true, iff r0 is "less than or equal to" r1. */
	bool (*rro_implies)(const struct c2_rm_right *r0,
			    const struct c2_rm_right *r1);
	/** @} end of Rights ordering */
};

/**
 */
struct c2_rm_loan {
	/**
	   A linkage to the list of all loads given out by an owner and hanging
	   off c2_rm_owner::ro_sublet.
	 */
	struct c2_list_link rl_linkage;
	struct c2_rm_right  rl_right;
	/**
	   Other party in the loan. Either an "upward" creditor or "downward"
	   debtor.
	 */
	struct c2_rm_remote rl_other;
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

   @invariant under ->ro_lock, the union of ->ro_owned and all rights in
   ->ro_sublet equals ->ro_borrowed.
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
	   A list of loans, linked through c2_rm_load::rl_linkage that this
	   owner borrowed from other owners.
	 */
	struct c2_list         ro_borrowed;
	/**
	   Right that is possessed by the owner.
	 */
	struct c2_rm_right     ro_owned;
	/**
	   A list of loans, linked through c2_rm_load::rl_linkage that this
	   owner extended to other owners. Rights on this list are not longer
	   possessed by this owner: they are counted in
	   c2_rm_owner::ro_granted, but not in c2_rm_owner::ro_owned.
	 */
	struct c2_list         ro_sublet;
	/**
	   A list of incoming requests, not yet satisfied. Requests are linked
	   through c2_rm_request::rq_want::rl_linkage.
	 */
	struct c2_list         ro_pending;
	struct c2_mutex        ro_lock;
};

enum c2_rm_request_state {
	RRS_GRANTED
};

/**
   @note a cedent can grant a usage right larger than requested.
 */
struct c2_rm_request {
	enum c2_rm_request_state  rq_state;
	struct c2_rm_loan         rq_want;
	struct c2_rm_right        rq_have;
	struct c2_chan            rq_signal;
};

struct c2_rm_lease {
};

void c2_rm_domain_init(struct c2_rm_domain_init *dom);
void c2_rm_domain_fini(struct c2_rm_domain_init *dom);

/**
   Register a resource type with a domain.

   @pre  rtype->rt_id == C2_RM_RESOURCE_TYPE_ID_INVALID &&
         rtype->rt_dom == NULL
   @post ergo(result == 0, IS_IN_ARRAY(rtype->rt_id, dom->rd_types) &&
                           rtype->rt_dom == dom)
 */
int  c2_rm_register  (struct c2_rm_domain_init *dom,
		      struct c2_rm_resource_type *rtype);
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
