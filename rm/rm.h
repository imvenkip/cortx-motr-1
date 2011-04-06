/* -*- C -*- */

#ifndef __COLIBRI_RM_RM_H__
#define __COLIBRI_RM_RM_H__

#include "lib/list.h"

/**
   @defgroup rm Resource management

   A resource is an entity in Colibri for which a notion of ownership can be
   well-defined. See the HLD referenced below for more details.

   Resource management is split into two parts:

   - generic functionality, implemented by the code in rm/ directory and

   - request type specific functionality.

   These parts interacts by the operation vectors (c2_rm_resource_ops,
   c2_rm_resource_type_ops and c2_rm_right_ops) provided by a resource type
   and called by the generic code.

   @see https://docs.google.com/a/xyratex.com/Doc?docid=0AQaCw6YRYSVSZGZmMzV6NzJfN2NiNXM1dHF3&hl=en

   @{
*/

struct c2_rm_resource;
struct c2_rm_resource_ops;
struct c2_rm_resource_type;
struct c2_rm_resource_type_ops;
struct c2_rm_owner;
struct c2_rm_right;
struct c2_rm_right_ops;
struct c2_rm_request;
struct c2_rm_lease;

/**
   A resource is part of system or its environment from which a notion of
   ownership is well-defined.

   In Colibri almost everything is a resource, except for the low-level
   types that are used to implement the resource framework.

   An instance of c2_rm_resource is embedded into other structures (files,
   quotas, fid name-spaces, &c.) that require resource-like semantics.
 */
struct c2_rm_resource {
	struct c2_rm_resource_type      *r_type;
	const struct c2_rm_resource_ops *r_ops;
};

struct c2_rm_resource_ops {
};

/**
   Resources are classified into disjoint types.

   Resource type determines how its instances interact with the resource
   management generic core:

   - it determines what resource rights are defined on the resources of
     this type;

   - how rights are ordered;

   - how right conflicts are resolved.
 */
struct c2_rm_resource_type {
	const struct c2_rm_resource_type_ops *rt_ops;
	const char                           *rt_name;
};

struct c2_rm_resource_type_ops {
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
 */
struct c2_rm_owner {
	struct c2_rm_right *ro_my;
	struct c2_list      ro_incoming;
	struct c2_list      ro_outgoing;
	struct c2_list      ro_sublet;
};

/**
   A resource owner uses the resource via a usage right (also called
   resource right or simply right as context permits). E.g., a client might
   have a right of a read-only or write-only or read-write access to a
   certain extent in a file. An owner is granted a right to use a resource.

   Rights on a given resource can be partially ordered by the "implies"
   relation. For example, a right to read [0, 1000] extent of some file,
   implies a right to read [0, 100] extent of the same file.

   The meaning of a resource right is determined by the resource
   type. Typically, an instance of c2_rm_right is embedded into some larger
   resource type specific structure describing the right. The resource type
   controls creation and destruction of rights.
 */
struct c2_rm_right {
	struct c2_rm_resource *ri_resource;
	uint64_t               ri_cookie;
};

struct c2_rm_right_ops {
	void (*rro_free)    (struct c2_rm_right *droit);
	bool (*rro_implies) (const struct c2_rm_right *have,
			     const struct c2_rm_right *want);
	bool (*rro_overlaps)(const struct c2_rm_right *dexter,
			     const struct c2_rm_right *sinister);
};

enum c2_rm_request_state {
	RRS_GRANTED
};

/**
   @note a cedent can grant a usage right larger than requested.
 */
struct c2_rm_request {
	enum c2_rm_request_state  rq_state;
	struct c2_rm_right       *rq_want;
	struct c2_rm_right       *rq_have;
};

struct c2_rm_lease {
};

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
