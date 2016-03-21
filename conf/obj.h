/* -*- c -*- */
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 30-Jan-2012
 */
#pragma once
#ifndef __MERO_CONF_OBJ_H__
#define __MERO_CONF_OBJ_H__

#include "conf/schema.h"    /* m0_conf_service_type */
#include "ha/note.h"        /* m0_ha_obj_state */
#include "lib/chan.h"       /* m0_chan */
#include "lib/tlist.h"      /* m0_tl, m0_tlink */
#include "lib/bob.h"        /* m0_bob_type */
#include "lib/types.h"
#include "layout/pdclust.h" /* m0_pdclust_attr */
#include "fid/fid.h"        /* m0_fid */

struct m0_conf_obj_ops;
struct m0_confx_obj;
struct m0_xcode_type;

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
 * filesystems, services, nodes, storage devices, etc.  Configuration data is
 * contained in configuration objects of which there are following predefined
 * types:
 *
 * - m0_conf_dir (a container for configuration objects),
 * - m0_conf_root
 * - m0_conf_profile,
 * - m0_conf_filesystem,
 * - m0_conf_pool,
 * - m0_conf_pver,
 * - m0_conf_objv,
 * - m0_conf_node,
 * - m0_conf_process,
 * - m0_conf_service,
 * - m0_conf_rack,
 * - m0_conf_enclosure,
 * - m0_conf_controller,
 * - m0_conf_sdev,
 * - m0_conf_disk.
 *
 * Some attributes are applicable to any type of configuration object.
 * Such common attributes are put together into m0_conf_obj structure,
 * which is embedded into concrete configuration objects.
 *
 * DAG of configuration objects:
 *
 * @dot
 * digraph x {
 *   edge [arrowhead=open, fontsize=11];
 *
 *   root -> profile [label=profiles];
 *   profile -> filesystem;
 *
 *   filesystem -> "node" [label=nodes];
 *   filesystem -> rack [label=racks];
 *   filesystem -> pool [label=pools];
 *   "node" -> process [label=processes];
 *   process -> service [label=services];
 *   service -> sdev [label=sdevs];
 *   rack -> enclosure [label=encls];
 *   enclosure -> controller [label=ctrls];
 *   controller -> disk [label=disks];
 *   "node" -> controller [dir=back, weight=0, style=dashed];
 *
 *   pool -> "pool version" [label=pvers];
 *   "pool version" -> "rack-v" [label=rackvs];
 *   "rack-v" -> "enclosure-v" [label=children];
 *   "enclosure-v" -> "controller-v" [label=children];
 *   "controller-v" -> "disk-v" [label=children];
 *   rack -> "rack-v" [dir=back, weight=0, style=dashed];
 *   enclosure -> "enclosure-v" [dir=back, weight=0, style=dashed];
 *   controller -> "controller-v" [dir=back, style=dashed, weight=0];
 *   disk -> "disk-v" [dir=back, style=dashed, weight=0];
 *   sdev -> disk [dir=back, style=dashed, weight=0];
 * }
 * @enddot
 *
 * <hr> <!------------------------------------------------------------>
 * @section conf-fspec-obj-enum Enumerations
 *
 * - m0_conf_status --- readiness of object's configuration data.
 *
 * @subsection conf-fspec-obj-enum-status Configuration Object Status
 *
 * A configuration object exists in one of three states:
 *   - M0_CS_MISSING --- configuration data is absent and is not being
 *     retrieved; the object is a stub.
 *   - M0_CS_LOADING --- retrieval of configuration is in progress; the
 *     object is a stub.
 *   - M0_CS_READY --- configuration is available; this is a
 *     fully-fledged object, not a stub.
 *
 * These values make up @ref m0_conf_status enumeration.
 *
 * Status field of a configuration object -- m0_conf_obj::co_status --
 * is accessed and modified by object's owner (confc or confd).
 * Initial status is M0_CS_MISSING. Possible transitions are shown on
 * the diagram below:
 *
 * @dot
 * digraph obj_status {
 *     M0_CS_MISSING -> M0_CS_LOADING [label="loading started"];
 *     M0_CS_LOADING -> M0_CS_MISSING [label="loading failed"];
 *     M0_CS_LOADING -> M0_CS_READY [label="loading succeeded"];
 *     M0_CS_MISSING -> M0_CS_READY [label=
 *  "configuration data is filled\nby some loading operation"];
 * }
 * @enddot
 *
 * <hr> <!------------------------------------------------------------>
 * @section conf-fspec-obj-pinned Pinned Objects
 *
 * If object's reference counter -- m0_conf_obj::co_nrefs -- is
 * non-zero, the object is said to be @em pinned. Stubs cannot be
 * pinned, only M0_CS_READY objects can.  Object's reference counter
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

/**
 * Status of configuration object.
 * Configuration object is a stub unless its status is M0_CS_READY.
 *
 * @see m0_conf_obj_is_stub()
 */
enum m0_conf_status {
	M0_CS_MISSING, /**< Configuration is absent; no retrieval in progress.*/
	M0_CS_LOADING, /**< Retrieval of configuration is in progress. */
	M0_CS_READY    /**< Configuration is available. */
};

/**
 * Generic configuration object.
 *
 * The fields of struct m0_conf_obj are common to all configuration
 * objects.  m0_conf_obj is embedded into each concrete configuration
 * object.
 */
struct m0_conf_obj {
	/** Unique object identifier. */
	struct m0_fid                 co_id;

	enum m0_conf_status           co_status;

	const struct m0_conf_obj_ops *co_ops;

	/**
	 * Pointer to the parent object.
	 *
	 * For objects that may have several parents (e.g., m0_conf_node) this
	 * points to the object itself.
	 *
	 * @note  Do not use obj->co_parent->co_parent chain of pointers.
	 *        m0_conf_obj_grandparent() is a safer alternative.
	 */
	struct m0_conf_obj           *co_parent;

	/**
	 * Reference counter.
	 * The object is "pinned" if this value is non-zero.
	 *
	 * @see m0_conf_obj_get(), m0_conf_obj_put()
	 */
	uint64_t                      co_nrefs;

	/**
	 * Channel on which following events are announced:
	 * - configuration loading completed;
	 * - object unpinned;
	 */
	struct m0_chan                co_chan;

	/**
	 * Channel on which following events are announced:
	 * - HA state changed (only if co_status == M0_CS_READY).
	 * Protected by configuration cache lock (co_cache->ca_lock).
	 */
	struct m0_chan                co_ha_chan;

	/** Configuration cache this object belongs to. */
	struct m0_conf_cache         *co_cache;

	/** Linkage to m0_conf_cache::ca_registry. */
	struct m0_tlink               co_cache_link;

	/** Linkage to m0_conf_dir::cd_items. */
	struct m0_tlink               co_dir_link;

	/** Linkage to m0t1fs_sb::csb_failure_set. */
	struct m0_tlink               co_fs_link;

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
	 * (see M0_CONF_CAST()).
	 */
	uint64_t                      co_con_magic;

	/** HA-related state of this configuration object. */
	enum m0_ha_obj_state          co_ha_state;
};

struct m0_conf_obj_type {
	const struct m0_fid_type    cot_ftype;
	struct m0_conf_obj       *(*cot_create)(void);
	uint64_t                    cot_magic;
	/**
	 * xcode type of m0_confx_foo. Double indirect, because m0_confx_foo_xc
	 * is not a constant (its address is).
	 */
	struct m0_xcode_type      **cot_xt;
	/**
	 * Name of the field in m0_confx_obj_xc union. This union xcode type is
	 * dynamically built as new conf object types are registered.
	 */
	const char                 *cot_branch;
	void                      (*cot_xc_init)(void);
};

void m0_conf_obj_type_register(const struct m0_conf_obj_type *otype);
void m0_conf_obj_type_unregister(const struct m0_conf_obj_type *otype);

const struct m0_conf_obj_type *m0_conf_obj_type(const struct m0_conf_obj *obj);
const struct m0_conf_obj_type *m0_conf_fid_type(const struct m0_fid *id);
const struct m0_fid           *m0_conf_objx_fid(const struct m0_confx_obj *obj);
const struct m0_conf_obj_type *
m0_conf_objx_type(const struct m0_confx_obj *obj);

bool m0_conf_fid_is_valid(const struct m0_fid *fid);

/**
 * Returns true iff obj->co_status != M0_CS_READY.
 *
 * @pre M0_IN(obj->co_status, (M0_CS_MISSING, M0_CS_LOADING, M0_CS_READY))
 */
M0_INTERNAL bool m0_conf_obj_is_stub(const struct m0_conf_obj *obj);

struct m0_conf_obj *m0_conf_obj_grandparent(const struct m0_conf_obj *obj);

enum { M0_CONF_OBJ_TYPE_MAX = 256 };

/* ------------------------------------------------------------------
 * Concrete configuration objects
 * ------------------------------------------------------------------ */

/** Directory object --- a container for configuration objects. */
struct m0_conf_dir {
	struct m0_conf_obj             cd_obj;
	/** List of m0_conf_obj-s, linked through m0_conf_obj::co_dir_link. */
	struct m0_tl                   cd_items;
	/** Type of items. */
	const struct m0_conf_obj_type *cd_item_type;
	/** "Relation" represented by this directory. */
	struct m0_fid                  cd_relfid;
};

/** Root object. Top-level configuration object */
struct m0_conf_root {
	/**
	 * ->rt_obj.co_parent == NULL: m0_conf_root is the topmost
	 * object in a DAG of configuration objects.
	 */
	struct m0_conf_obj  rt_obj;
	struct m0_conf_dir *rt_profiles;
/* configuration data (for the application) */
	/**
	 * Version of the configuration database.
	 * Incremented after every configuration database update.
	 *
	 * @note Value 0 is reserved and must not be used.
	 */
	uint64_t            rt_verno;
};

struct m0_conf_profile {
	struct m0_conf_obj         cp_obj;
	struct m0_conf_filesystem *cp_filesystem;
};

struct m0_conf_filesystem {
	struct m0_conf_obj  cf_obj;
	struct m0_conf_dir *cf_nodes;
	struct m0_conf_dir *cf_pools;
	struct m0_conf_dir *cf_racks;
/* configuration data (for the application) */
	struct m0_fid       cf_rootfid;
	/** Meta-data pool. */
	struct m0_fid       cf_mdpool;
	uint32_t            cf_redundancy;
	/**
	 * Filesystem parameters.
	 * NULL-terminated array of C strings.
	 * XXX @todo Make it an array of name-value pairs (attributes).
	 */
	const char        **cf_params;
};

/** Pools are used to partition hardware resources (devices, services, etc.). */
struct m0_conf_pool {
	struct m0_conf_obj  pl_obj;
	struct m0_conf_dir *pl_pvers;
/* configuration data (for the application) */
	/** Rank of this pool in the filesystem. */
	uint32_t            pl_order;
};

/** Pool version state. */
enum m0_conf_pver_state {
	M0_CONF_PVER_ONLINE,
	M0_CONF_PVER_FAILED
};

/**
 * Pool version.
 *
 * Pool versions are used to track changes in pool membership.
 */
struct m0_conf_pver {
	struct m0_conf_obj      pv_obj;
	struct m0_conf_dir     *pv_rackvs;
/* configuration data (for the application) */
	/** Version number. */
	uint32_t                pv_ver;
	/** Pool version failed devices. */
	uint32_t                pv_nfailed;
	/** Layout attributes. */
	struct m0_pdclust_attr  pv_attr;
	/** Allowed failures for each failure domain. */
	uint32_t               *pv_nr_failures;
	uint32_t                pv_nr_failures_nr;
};

/**
 * Represents virtual objects corresponding to real devices e.g. racks,
 * controllers, etc.
 */
struct m0_conf_objv {
	struct m0_conf_obj  cv_obj;
	struct m0_conf_dir *cv_children;
	/**
	 * Real device (rack, enclosure, controller, etc.) associated with
	 * this configuration object version.
	 */
	struct m0_conf_obj *cv_real;
};

struct m0_conf_node {
	struct m0_conf_obj   cn_obj;
	struct m0_conf_dir  *cn_processes;
	/*
	 * XXX OBSOLETE DELETEME
	 * ->cn_pool is a remnant of old configuration schema.
	 */
	struct m0_conf_pool *cn_pool;
/* configuration data (for the application) */
	/** Memory size in MB. */
	uint32_t             cn_memsize;
	/** Number of processors. */
	uint32_t             cn_nr_cpu;
	/** Last known state. See m0_cfg_state_bit. */
	uint64_t             cn_last_state;
	/** Property flags. See m0_cfg_flag_bit. */
	uint64_t             cn_flags;
};

/**
 * Process configuration.
 *
 * @see m0_processors_online(), m0_proc_attr
 */
struct m0_conf_process {
	struct m0_conf_obj  pc_obj;
	struct m0_conf_dir *pc_services;
/* configuration data (for the application) */
	struct m0_bitmap    pc_cores; /**< Available cores (mask). */
	uint64_t            pc_memlimit_as;
	uint64_t            pc_memlimit_rss;
	uint64_t            pc_memlimit_stack;
	uint64_t            pc_memlimit_memlock;
	const char         *pc_endpoint;
};

struct m0_conf_service {
	struct m0_conf_obj        cs_obj;
	struct m0_conf_dir       *cs_sdevs;
/* configuration data (for the application) */
	enum m0_conf_service_type cs_type;
	/**
	 * End-points from which this service is reachable.
	 * NULL-terminated array of C strings.
	 */
	const char              **cs_endpoints;
	union {
		uint32_t      repair_limits;
		struct m0_fid addb_stobid;
		const char   *confdb_path;
	} cs_u;
};

/** Hardware resource --- rack. */
struct m0_conf_rack {
	struct m0_conf_obj    cr_obj;
	/** Enclosures on this rack. */
	struct m0_conf_dir   *cr_encls;
/* configuration data (for the application) */
	/**
	 * Pool versions this rack is part of.
	 * NULL-terminated array.
	 */
	struct m0_conf_pver **cr_pvers;
};

/** Hardware resource --- enclosure. */
struct m0_conf_enclosure {
	struct m0_conf_obj    ce_obj;
	/** Controllers in this enclosure. */
	struct m0_conf_dir   *ce_ctrls;
/* configuration data (for the application) */
	/**
	 * Pool versions this enclosure is part of.
	 * NULL-terminated array.
	 */
	struct m0_conf_pver **ce_pvers;
};

/** Hardware resource --- controller. */
struct m0_conf_controller {
	struct m0_conf_obj    cc_obj;
	/** The node this controller is associated with. */
	struct m0_conf_node  *cc_node;
	/** Storage disks attached to this controller. */
	struct m0_conf_dir   *cc_disks;
/* configuration data (for the application) */
	/**
	 * Pool versions this controller is part of.
	 * NULL-terminated array.
	 */
	struct m0_conf_pver **cc_pvers;
};

/** Hardware resource - storage device. */
struct m0_conf_sdev {
	struct m0_conf_obj sd_obj;
/* configuration data (for the application) */
	/**
	 * Pointer to fid of corresponding disk object.
	 * @note This can ne NULL if no disk object defined
	 *       for this object.
	 */
	struct m0_fid     *sd_disk;
	/**
	 * Device index.
	 * The value should be unique and belong [0, P) range,
	 * where P is total number of devices under ioservices.
	 */
	uint32_t           sd_dev_idx;
	/** Interface type. See m0_cfg_storage_device_interface_type. */
	uint32_t           sd_iface;
	/** Media type. See m0_cfg_storage_device_media_type. */
	uint32_t           sd_media;
	/** Block size in bytes. */
	uint32_t           sd_bsize;
	/** Size in bytes. */
	uint64_t           sd_size;
	/** Last known state.  See m0_cfg_state_bit. */
	uint64_t           sd_last_state;
	/** Property flags.  See m0_cfg_flag_bit. */
	uint64_t           sd_flags;
	/** Filename in host OS. */
	const char        *sd_filename;
};

struct m0_conf_disk {
	struct m0_conf_obj    ck_obj;
	/** Pointer to the storage device associated with this disk. */
	struct m0_conf_sdev  *ck_dev;
	/**
	 * Pool versions this disk is part of.
	 * NULL-terminated array.
	 */
	struct m0_conf_pver **ck_pvers;
};

/* ------------------------------------------------------------------
 * Cast
 * ------------------------------------------------------------------ */

/**
 * Casts m0_conf_obj to the ambient concrete configuration object.
 *
 * @param ptr   Pointer to m0_conf_obj member.
 * @param type  Type of concrete configuration object (without `struct').
 *
 * Example:
 * @code
 * struct m0_conf_service *svc = M0_CONF_CAST(svc_obj, m0_conf_service);
 * @endcode
 */
#define M0_CONF_CAST(ptr, type) \
	bob_of(ptr, struct type, type ## _cast_field, &type ## _bob)

#define m0_conf_dir_cast_field        cd_obj
#define m0_conf_root_cast_field       rt_obj
#define m0_conf_profile_cast_field    cp_obj
#define m0_conf_filesystem_cast_field cf_obj
#define m0_conf_pool_cast_field       pl_obj
#define m0_conf_pver_cast_field       pv_obj
#define m0_conf_objv_cast_field       cv_obj
#define m0_conf_node_cast_field       cn_obj
#define m0_conf_process_cast_field    pc_obj
#define m0_conf_service_cast_field    cs_obj
#define m0_conf_sdev_cast_field       sd_obj
#define m0_conf_rack_cast_field       cr_obj
#define m0_conf_enclosure_cast_field  ce_obj
#define m0_conf_controller_cast_field cc_obj
#define m0_conf_disk_cast_field       ck_obj

#define M0_CONF_OBJ_TYPES               \
	X_CONF(root,       ROOT);       \
	X_CONF(dir,        DIR);        \
	X_CONF(profile,    PROFILE);    \
	X_CONF(filesystem, FILESYSTEM); \
	X_CONF(pool,       POOL);       \
	X_CONF(pver,       PVER);       \
	X_CONF(objv,       OBJV);       \
	X_CONF(node,       NODE);       \
	X_CONF(process,    PROCESS);    \
	X_CONF(service,    SERVICE);    \
	X_CONF(sdev,       SDEV);       \
	X_CONF(rack,       RACK);       \
	X_CONF(enclosure,  ENCLOSURE);  \
	X_CONF(controller, CONTROLLER); \
	X_CONF(disk,       DISK)

#define X_CONF(name, NAME)                                        \
	extern const struct m0_bob_type m0_conf_ ## name ## _bob; \
	extern const struct m0_conf_obj_type M0_CONF_ ## NAME ## _TYPE
M0_CONF_OBJ_TYPES;
#undef X_CONF

/* Relation fids. */
#define M0_CONF_REL_FIDS               \
	X_CONF(ANY,               -1); \
	X_CONF(ROOT_PROFILES,      1); \
	X_CONF(PROFILE_FILESYSTEM, 2); \
	X_CONF(FILESYSTEM_NODES,   3); \
	X_CONF(FILESYSTEM_POOLS,   4); \
	X_CONF(FILESYSTEM_RACKS,   5); \
	X_CONF(POOL_PVERS,         6); \
	X_CONF(PVER_RACKVS,        7); \
	X_CONF(RACKV_ENCLVS,       8); \
	X_CONF(ENCLV_CTRLVS,       9); \
	X_CONF(CTRLV_DISKVS,      10); \
	X_CONF(NODE_PROCESSES,    11); \
	X_CONF(PROCESS_SERVICES,  12); \
	X_CONF(SERVICE_SDEVS,     13); \
	X_CONF(RACK_ENCLS,        14); \
	X_CONF(RACK_PVERS,        15); \
	X_CONF(ENCLOSURE_CTRLS,   16); \
	X_CONF(ENCLOSURE_PVERS,   17); \
	X_CONF(CONTROLLER_DISKS,  18); \
	X_CONF(CONTROLLER_PVERS,  19); \
	X_CONF(DISK_SDEV,         20)

#define X_CONF(name, _) \
	extern const struct m0_fid M0_CONF_ ## name ## _FID
M0_CONF_REL_FIDS;
#undef X_CONF
extern const struct m0_fid_type M0_CONF_RELFID_TYPE;

/**
 * Root configuration object FID.
 * The same in any configuration database.
 */
extern const struct m0_fid M0_CONF_ROOT_FID;

/**
 * Iterates over registered conf object types.
 *
 * To start iteration call m0_conf_obj_type_next(NULL). Returns NULL when the
 * iteration is over.
 *
 * @code
 * const struct m0_conf_obj_type *t = NULL;
 *
 * while ((t = m0_conf_obj_type_next(t)) != NULL) {
 *         ... do something with t ...
 * }
 * @endcode
 */
M0_INTERNAL const struct m0_conf_obj_type *
m0_conf_obj_type_next(const struct m0_conf_obj_type *otype);

M0_INTERNAL int m0_conf_obj_init(void);
M0_INTERNAL void m0_conf_obj_fini(void);

/** @} conf_dfspec_obj */
#endif /* __MERO_CONF_OBJ_H__ */
