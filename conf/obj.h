/* -*- c -*- */
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 30-Jan-2012
 */
#pragma once
#ifndef __COLIBRI_CONF_OBJ_H__
#define __COLIBRI_CONF_OBJ_H__

#include "lib/buf.h"   /* c2_buf */
#include "lib/chan.h"  /* c2_chan */
#include "fid/fid.h"   /* c2_fid */
#include "lib/tlist.h" /* c2_tl, c2_tlink */
#include "lib/bob.h"   /* c2_bob_type */
#include "lib/types.h"

struct c2_conf_obj_ops;
struct c2_confc;

/* XXX @todo Move definitions from cfg/cfg.h to conf/schema.ff */
/* #include "cfg/cfg.h"   /\* c2_cfg_service_type *\/ */
enum c2_cfg_service_type {
	/** metadata service       */
	C2_CFG_SERVICE_METADATA = 1,
	/** io/data service        */
	C2_CFG_SERVICE_IO,
	/** management service     */
	C2_CFG_SERVICE_MGMT,
	/** DLM service            */
	C2_CFG_SERVICE_DLM,
};

/**
 * @page conf-fspec-obj Configuration Objects
 *
 * - @ref conf-fspec-obj-data
 * - @ref conf-fspec-obj-enum
 *   - @ref conf-fspec-obj-enum-status
 * - @ref conf-fspec-obj-pinned
 * - @ref conf-fspec-obj-private
 *
 * <hr> <!------------------------------------------------------------>
 * @section conf-fspec-obj-data Data Structures
 *
 * There are different kinds of configuration data: configuration of
 * filesystems, services, nodes, storage devices, etc.  Configuration
 * data is contained in configuration objects of which there are 8
 * types:
 * - c2_conf_dir (a container for configuration objects),
 * - c2_conf_profile,
 * - c2_conf_filesystem,
 * - c2_conf_service,
 * - c2_conf_node,
 * - c2_conf_nic,
 * - c2_conf_sdev,
 * - c2_conf_partition.
 *
 * Some attributes are applicable to any type of configuration object.
 * Such common attributes are put together into c2_conf_obj structure,
 * which is embedded into concrete configuration objects.
 *
 * <hr> <!------------------------------------------------------------>
 * @section conf-fspec-obj-enum Enumerations
 *
 * - c2_conf_objtype --- numeric tag that corresponds to a type of
 *   concrete configuration objects.
 * - c2_conf_status --- readiness of object's configuration data.
 *
 * @subsection conf-fspec-obj-enum-status Configuration Object Status
 *
 * A configuration object exists in one of three states:
 *   - C2_CS_MISSING --- configuration data is absent and is not being
 *     retrieved; the object is a stub.
 *   - C2_CS_LOADING --- retrieval of configuration is in progress; the
 *     object is a stub.
 *   - C2_CS_READY --- configuration is available; this is a
 *     fully-fledged object, not a stub.
 *
 * These values make up @ref c2_conf_status enumeration.
 *
 * Status field of a configuration object -- c2_conf_obj::co_status --
 * is accessed and modified by object's owner (confc or confd).
 * Initial status is C2_CS_MISSING. Possible transitions are shown on
 * the diagram below:
 *
 * @dot
 * digraph obj_status {
 *     C2_CS_MISSING -> C2_CS_LOADING [label="loading started"];
 *     C2_CS_LOADING -> C2_CS_MISSING [label="loading failed"];
 *     C2_CS_LOADING -> C2_CS_READY [label="loading succeeded"];
 *     C2_CS_MISSING -> C2_CS_READY [label=
 *  "configuration data is filled\nby some loading operation"];
 * }
 * @enddot
 *
 * <hr> <!------------------------------------------------------------>
 * @section conf-fspec-obj-pinned Pinned Objects
 *
 * If object's reference counter -- c2_conf_obj::co_nrefs -- is
 * non-zero, the object is said to be @em pinned. Stubs cannot be
 * pinned, only C2_CS_READY objects can.  Object's reference counter
 * is used by confc and confd implementations and is not supposed to
 * be accessed by a configuration consumer.
 *
 * <hr> <!------------------------------------------------------------>
 * @section conf-fspec-obj-private Private Fields
 *
 * Only those fields of concrete configuration objects that follow
 * "configuration data (for the application)" comment should ever be
 * accessed by the application.  The rest of fields are private
 * property of confc and confd implementations.
 *
 * @see @ref conf_dfspec_obj "Detailed Functional Specification"
 */

/**
 * @defgroup conf_dfspec_obj Configuration Objects
 * @brief Detailed Functional Specification.
 *
 * @see @ref conf, @ref conf-fspec-obj "Functional Specification"
 *
 * @{
 */

/** Type of configuration object. */
enum c2_conf_objtype {
	C2_CO_DIR,        /* 0 */
	C2_CO_PROFILE,    /* 1 */
	C2_CO_FILESYSTEM, /* 2 */
	C2_CO_SERVICE,    /* 3 */
	C2_CO_NODE,       /* 4 */
	C2_CO_NIC,        /* 5 */
	C2_CO_SDEV,       /* 6 */
	C2_CO_PARTITION,  /* 7 */
	C2_CO_NR
};

/**
 * Status of configuration object.
 * Configuration object is a stub unless its status is C2_CS_READY.
 */
enum c2_conf_status {
	C2_CS_MISSING, /*< Configuration is absent; no retrieval in progress. */
	C2_CS_LOADING, /*< Retrieval of configuration is in progress. */
	C2_CS_READY    /*< Configuration is available. */
};

/**
 * Generic configuration object.
 *
 * The fields of struct c2_conf_obj are common to all configuration
 * objects.  c2_conf_obj is embedded into each concrete configuration
 * object.
 */
struct c2_conf_obj {
	/** Type of the ambient (concrete) configuration object. */
	enum c2_conf_objtype          co_type;

	/**
	 * Object identifier.
	 * This value is unique among the object of given ->co_type.
	 */
	struct c2_buf                 co_id;

	enum c2_conf_status           co_status;

	const struct c2_conf_obj_ops *co_ops;

	/**
	 * Pointer to the parent object.
	 *
	 * The value is NULL for objects that may have several parents
         * (e.g., c2_conf_node).
	 */
	struct c2_conf_obj           *co_parent;

	/**
	 * Reference counter.
	 * The object is "pinned" if this value is non-zero.
	 *
	 * @see c2_conf_obj_get(), c2_conf_obj_put()
	 */
	uint64_t                      co_nrefs;

	/**
	 * Channel on which "configuration loading completed" and
	 * "object unpinned" events are announced.
	 */
	struct c2_chan                co_chan;

	/** Linkage to c2_conf_reg::r_objs. */
	struct c2_tlink               co_reg_link;

	/** Linkage to c2_conf_dir::cd_items. */
	struct c2_tlink               co_dir_link;

	/**
	 * Private data of confc implementation.
	 * NULL at confd side.
	 */
	struct c2_confc              *co_confc;

	/**
	 * Generic magic.
	 *
	 * All configuration objects have the same value of
	 * ->co_gen_magic.
	 *
	 * This magic value is used for list operations.
	 */
	uint64_t                      co_gen_magic;

	/**
	 * Concrete magic.
	 *
	 * Different concrete object types have different values of
	 * ->co_con_magic.
	 *
	 * This magic value is used for generic-to-concrete casting
	 * (see C2_CONF_CAST()).
	 */
	uint64_t                      co_con_magic;

	/**
	 * Whether the object has been inserted into the DAG.
	 *
	 * @todo XXX Property (to be verified):
	 * ergo(obj->co_mounted,
	 *      parent_check(obj) && (obj_is_stub(obj) || children_check(obj))),
	 * where
	 *   children_check(obj) verifies that `obj' has established
	 *   relations with its children.
	 *
	 * @see @ref conf-lspec-comps
	 */
	bool                          co_mounted;
};

/* ------------------------------------------------------------------
 * Concrete configuration objects
 * ------------------------------------------------------------------ */

/** Directory object --- container for configuration objects. */
struct c2_conf_dir {
	struct c2_conf_obj   cd_obj;
	/** List of c2_conf_obj-s, linked through c2_conf_obj::co_dir_link. */
	struct c2_tl         cd_items;
	/**
	 * Type of items.
	 *
	 * This field lets c2_conf_dir know which "relation" it represents.
	 */
	enum c2_conf_objtype cd_item_type;
};

struct c2_conf_profile {
	/*
	 * ->cp_obj.co_parent == NULL: c2_conf_profile is the top-most
	 * object in a DAG of configuration objects.
	 */
	struct c2_conf_obj         cp_obj;
	struct c2_conf_filesystem *cp_filesystem;
};

struct c2_conf_filesystem {
	struct c2_conf_obj  cf_obj;
	struct c2_conf_dir *cf_services;
/* configuration data (for the application) */
	struct c2_fid       cf_rootfid;
	/**
	 * Filesystem parameters.
	 * NULL terminated array of C strings.
	 * XXX @todo Make it an array of name-value pairs (attributes).
	 */
	const char        **cf_params;
};

struct c2_conf_service {
	struct c2_conf_obj       cs_obj;
	/** The node this service is hosted at. */
	struct c2_conf_node     *cs_node;
/* configuration data (for the application) */
	enum c2_cfg_service_type cs_type;
	/**
	 * Service end points.
	 * NULL terminated array of C strings.
	 */
	const char             **cs_endpoints;
};

struct c2_conf_node {
	/*
	 * Note that ->cn_obj.co_parent == NULL: a node can host
	 * several services, so there may be no single parent.
	 */
	struct c2_conf_obj  cn_obj;
	struct c2_conf_dir *cn_nics;
	struct c2_conf_dir *cn_sdevs;
/* configuration data (for the application) */
	uint32_t            cn_memsize;
	uint32_t            cn_nr_cpu;
	uint64_t            cn_last_state;
	uint64_t            cn_flags;
	uint64_t            cn_pool_id;
};

/** Network interface controller. */
struct c2_conf_nic {
	struct c2_conf_obj ni_obj;
/* configuration data (for the application) */
	uint32_t           ni_iface;
	uint32_t           ni_mtu;
	uint64_t           ni_speed;
	const char        *ni_filename;
	uint64_t           ni_last_state;
};

/** Storage device. */
struct c2_conf_sdev {
	struct c2_conf_obj  sd_obj;
	struct c2_conf_dir *sd_partitions;
/* configuration data (for the application) */
	uint32_t            sd_iface;
	uint32_t            sd_media;
	uint64_t            sd_size;
	uint64_t            sd_last_state;
	uint64_t            sd_flags;
	const char         *sd_filename;
};

/** Storage device partition. */
struct c2_conf_partition {
	struct c2_conf_obj pa_obj;
/* configuration data (for the application) */
	uint64_t           pa_start;
	uint64_t           pa_size;
	uint32_t           pa_index;
	uint32_t           pa_type;
	const char        *pa_filename;
};

/* ------------------------------------------------------------------
 * Cast
 * ------------------------------------------------------------------ */

/**
 * Casts c2_conf_obj to the ambient concrete configuration object.
 *
 * @param ptr   Pointer to c2_conf_obj member.
 * @param type  Type of concrete configuration object (without `struct').
 *
 * Example:
 * @code
 * struct c2_conf_service *svc = C2_CONF_CAST(svc_obj, c2_conf_service);
 * @endcode
 */
#define C2_CONF_CAST(ptr, type) \
	bob_of(ptr, struct type, type ## _cast_field, &type ## _bob)

#define c2_conf_dir_cast_field        cd_obj
#define c2_conf_profile_cast_field    cp_obj
#define c2_conf_filesystem_cast_field cf_obj
#define c2_conf_service_cast_field    cs_obj
#define c2_conf_node_cast_field       cn_obj
#define c2_conf_nic_cast_field        ni_obj
#define c2_conf_sdev_cast_field       sd_obj
#define c2_conf_partition_cast_field  pa_obj

extern const struct c2_bob_type c2_conf_dir_bob;
extern const struct c2_bob_type c2_conf_profile_bob;
extern const struct c2_bob_type c2_conf_filesystem_bob;
extern const struct c2_bob_type c2_conf_service_bob;
extern const struct c2_bob_type c2_conf_node_bob;
extern const struct c2_bob_type c2_conf_nic_bob;
extern const struct c2_bob_type c2_conf_sdev_bob;
extern const struct c2_bob_type c2_conf_partition_bob;

/** @} conf_dfspec_obj */
#endif /* __COLIBRI_CONF_OBJ_H__ */
