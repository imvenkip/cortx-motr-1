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

#include "lib/chan.h"     /* m0_chan */
#include "lib/tlist.h"    /* m0_tl, m0_tlink */
#include "lib/bob.h"      /* m0_bob_type */
#include "lib/types.h"
#include "fid/fid.h"      /* m0_fid */
#include "conf/schema.h"  /* m0_conf_service_type */

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
 * - m0_conf_dir (a container for configuration objects),
 * - m0_conf_profile,
 * - m0_conf_filesystem,
 * - m0_conf_service,
 * - m0_conf_node,
 * - m0_conf_nic,
 * - m0_conf_sdev.
 *
 * Some attributes are applicable to any type of configuration object.
 * Such common attributes are put together into m0_conf_obj structure,
 * which is embedded into concrete configuration objects.
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
	M0_CS_MISSING, /*< Configuration is absent; no retrieval in progress. */
	M0_CS_LOADING, /*< Retrieval of configuration is in progress. */
	M0_CS_READY    /*< Configuration is available. */
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
	 * Channel on which "configuration loading completed" and
	 * "object unpinned" events are announced.
	 */
	struct m0_chan                co_chan;

	/** Configuration cache this object belongs to. */
	struct m0_conf_cache         *co_cache;

	/** Linkage to m0_conf_cache::ca_registry. */
	struct m0_tlink               co_cache_link;

	/** Linkage to m0_conf_dir::cd_items. */
	struct m0_tlink               co_dir_link;

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
};

struct m0_conf_obj_type {
	const struct m0_fid_type    cot_ftype;
	struct m0_conf_obj       *(*cot_ctor)(void);
	const char                 *cot_table_name;
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

struct m0_conf_profile {
	/*
	 * ->cp_obj.co_parent == NULL: m0_conf_profile is the top-most
	 * object in a DAG of configuration objects.
	 */
	struct m0_conf_obj         cp_obj;
	struct m0_conf_filesystem *cp_filesystem;
};

struct m0_conf_filesystem {
	struct m0_conf_obj  cf_obj;
	struct m0_conf_dir *cf_services;
/* configuration data (for the application) */
	struct m0_fid       cf_rootfid;
	/**
	 * Filesystem parameters.
	 * NULL terminated array of C strings.
	 * XXX @todo Make it an array of name-value pairs (attributes).
	 */
	const char        **cf_params;
};

struct m0_conf_service {
	struct m0_conf_obj        cs_obj;
	/** The node this service is hosted at. */
	struct m0_conf_node      *cs_node;
/* configuration data (for the application) */
	enum m0_conf_service_type cs_type;
	/**
	 * Service end points.
	 * NULL terminated array of C strings.
	 */
	const char              **cs_endpoints;
};

struct m0_conf_node {
	/*
	 * Note that a node can host several services, so there may be no single
	 * parent. This is indicated by setting ->co_parent to point to the
	 * object itself.
	 */
	struct m0_conf_obj  cn_obj;
	struct m0_conf_dir *cn_nics;
	struct m0_conf_dir *cn_sdevs;
/* configuration data (for the application) */
	uint32_t            cn_memsize;
	uint32_t            cn_nr_cpu;
	uint64_t            cn_last_state;
	uint64_t            cn_flags;
	uint64_t            cn_pool_id;
};

/** Network interface controller. */
struct m0_conf_nic {
	struct m0_conf_obj ni_obj;
/* configuration data (for the application) */
	uint32_t           ni_iface;
	uint32_t           ni_mtu;
	uint64_t           ni_speed;
	const char        *ni_filename;
	uint64_t           ni_last_state;
};

/** Storage device. */
struct m0_conf_sdev {
	struct m0_conf_obj  sd_obj;
/* configuration data (for the application) */
	uint32_t            sd_iface;
	uint32_t            sd_media;
	uint64_t            sd_size;
	uint64_t            sd_last_state;
	uint64_t            sd_flags;
	const char         *sd_filename;
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
#define m0_conf_profile_cast_field    cp_obj
#define m0_conf_filesystem_cast_field cf_obj
#define m0_conf_service_cast_field    cs_obj
#define m0_conf_node_cast_field       cn_obj
#define m0_conf_nic_cast_field        ni_obj
#define m0_conf_sdev_cast_field       sd_obj

extern const struct m0_bob_type m0_conf_dir_bob;
extern const struct m0_bob_type m0_conf_profile_bob;
extern const struct m0_bob_type m0_conf_filesystem_bob;
extern const struct m0_bob_type m0_conf_service_bob;
extern const struct m0_bob_type m0_conf_node_bob;
extern const struct m0_bob_type m0_conf_nic_bob;
extern const struct m0_bob_type m0_conf_sdev_bob;

/* relation fids */

extern const struct m0_fid M0_CONF_FILESYSTEM_SERVICES_FID;
extern const struct m0_fid M0_CONF_PROFILE_FILESYSTEM_FID;
extern const struct m0_fid M0_CONF_SERVICE_NODE_FID;
extern const struct m0_fid M0_CONF_NODE_NICS_FID;
extern const struct m0_fid M0_CONF_NODE_SDEVS_FID;
extern const struct m0_fid_type M0_CONF_RELFID_TYPE;

extern const struct m0_conf_obj_type M0_CONF_PROFILE_TYPE;
extern const struct m0_conf_obj_type M0_CONF_FILESYSTEM_TYPE;
extern const struct m0_conf_obj_type M0_CONF_SERVICE_TYPE;
extern const struct m0_conf_obj_type M0_CONF_NODE_TYPE;
extern const struct m0_conf_obj_type M0_CONF_NIC_TYPE;
extern const struct m0_conf_obj_type M0_CONF_SDEV_TYPE;
extern const struct m0_conf_obj_type M0_CONF_DIR_TYPE;

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
