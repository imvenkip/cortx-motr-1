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
 * Original authors: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>,
 *		     Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>
 * Original creation date: 20-Aug-2012
 */
#pragma once
#ifndef __MERO_CONF_ONWIRE_H__
#define __MERO_CONF_ONWIRE_H__

#include "xcode/xcode.h"
#include "lib/buf_xc.h"
#include "fid/fid.h"
#include "fid/fid_xc.h"

/* export */
struct m0_conf_fetch;
struct m0_conf_fetch_resp;
struct m0_conf_update;
struct m0_conf_update_resp;

struct arr_fid {
	uint32_t       af_count;
	struct m0_fid *af_elems;
} M0_XCA_SEQUENCE;

struct arr_u32 {
	uint32_t  au_count;
	uint32_t *au_elems;
} M0_XCA_SEQUENCE;

/* ------------------------------------------------------------------
 * Configuration objects
 * ------------------------------------------------------------------ */

/* Note that m0_confx_dir does not exist. */

/** Common header of all confx objects. */
struct m0_confx_header {
	struct m0_fid ch_id;
} M0_XCA_RECORD;

struct m0_confx_profile {
	struct m0_confx_header xp_header;
	/* Name of profile's filesystem. */
	struct m0_fid          xp_filesystem;
} M0_XCA_RECORD;

struct m0_confx_filesystem {
	struct m0_confx_header xf_header;
	/* Rood fid. */
	struct m0_fid          xf_rootfid;
	/* Redundancy for this filesystem. */
	uint32_t               xf_redundancy;
	/* Filesystem parameters. */
	struct m0_bufs         xf_params;
	/* Pool to locate mata-data. */
	struct m0_fid          xf_mdpool;
	/* Nodes of this filesystem. */
	struct arr_fid         xf_nodes;
	/* Pools this filesystem resides on. */
	struct arr_fid         xf_pools;
	/* Racks this filesystem resides on. */
	struct arr_fid         xf_racks;
} M0_XCA_RECORD;

struct m0_confx_pool {
	struct m0_confx_header xp_header;
	/* Order in set of pool. */
	uint32_t               xp_order;
	/* Pool versions for this pool. */
	struct arr_fid         xp_pvers;
} M0_XCA_RECORD;

struct m0_confx_pver {
	struct m0_confx_header xv_header;
	/* Version number. */
	uint32_t               xv_ver;
	/* Data units. */
	uint32_t               xv_N;
	/* Parity units. */
	uint32_t               xv_K;
	/* Pool width. */
	uint32_t               xv_P;
	struct arr_u32         xv_permutations;
	/* The number of allowed failures for each failure domain. */
	struct arr_u32         xv_nr_failures;
	/* Rack versions associated with this pool version. */
	struct arr_fid         xv_rackvs;
} M0_XCA_RECORD;

struct m0_confx_objv {
	struct m0_confx_header xj_header;
	/* Identifier of real device associated with this version. */
	struct m0_fid          xj_real;
	struct arr_fid         xj_children;
} M0_XCA_RECORD;

struct m0_confx_node {
	struct m0_confx_header xn_header;
	/* Memory size in MB. */
	uint32_t               xn_memsize;
	/* Number of processors. */
	uint32_t               xn_nr_cpu;
	/* Last known state.  See m0_cfg_state_bit. */
	uint64_t               xn_last_state;
	/* Property flags.  See m0_cfg_flag_bit. */
	uint64_t               xn_flags;
	struct m0_fid          xn_pool_id;
	struct arr_fid         xn_processes;
} M0_XCA_RECORD;

struct m0_confx_process {
	struct m0_confx_header xr_header;
	uint32_t               xr_mem_limit;
	uint32_t               xr_cores;
	/* Services being run by this process. */
	struct arr_fid         xr_services;
} M0_XCA_RECORD;

struct m0_confx_service {
	struct m0_confx_header xs_header;
	/* Service type.  See m0_conf_service_type. */
	uint32_t               xs_type;
	/* End-points from which this service is reachable. */
	struct m0_bufs         xs_endpoints;
	/* Devices associated with service. */
	struct arr_fid         xs_sdevs;
} M0_XCA_RECORD;

struct m0_confx_sdev {
	struct m0_confx_header xd_header;
	/* Interface type.  See m0_cfg_storage_device_interface_type. */
	uint32_t               xd_iface;
	/* Media type.  See m0_cfg_storage_device_media_type. */
	uint32_t               xd_media;
	/* Size in bytes. */
	uint64_t               xd_bsize;
	/* Size in bytes. */
	uint64_t               xd_size;
	/* Last known state.  See m0_cfg_state_bit. */
	uint64_t               xd_last_state;
	/* Property flags.  See m0_cfg_flag_bit. */
	uint64_t               xd_flags;
	/* Filename in host OS. */
	struct m0_buf          xd_filename;
} M0_XCA_RECORD;

struct m0_confx_rack {
	struct m0_confx_header xr_header;
	/* Enclosures on this rack. */
	struct arr_fid         xr_encls;
	/* Pool versions for this pool. */
	struct arr_fid         xr_pvers;
} M0_XCA_RECORD;

struct m0_confx_enclosure {
	struct m0_confx_header xe_header;
	/* Controllers in this enclosure. */
	struct arr_fid         xe_ctrls;
	/* Pool versions for this pool. */
	struct arr_fid         xe_pvers;
} M0_XCA_RECORD;

struct m0_confx_controller {
	struct m0_confx_header xc_header;
	/* Associated note for this controller. */
	struct m0_fid          xc_node;
	/* Storage disks attached to this controller. */
	struct arr_fid         xc_disks;
	/* Pool versions for this controller. */
	struct arr_fid         xc_pvers;
} M0_XCA_RECORD;

struct m0_confx_disk {
	struct m0_confx_header xk_header;
	/* Storage device associated with this disk. */
	struct m0_fid          xk_dev;
} M0_XCA_RECORD;

struct m0_confx_obj {
	uint64_t xo_type; /* see m0_fid_type::ft_id for values */
	union {
		/**
		 * Allows to access the header of concrete m0_confx_* objects.
		 */
		struct m0_confx_header u_header;
	} xo_u;
};

/**
 * xcode type of the union above.
 *
 * This type is build dynamically, when new conf object types are
 * registered. See m0_conf_obj_type_register().
 */
M0_EXTERN struct m0_xcode_type *m0_confx_obj_xc;
M0_INTERNAL void m0_xc_m0_confx_obj_struct_init(void);
M0_INTERNAL void m0_xc_m0_confx_obj_struct_fini(void);

/** Encoded configuration --- a sequence of m0_confx_objs. */
struct m0_confx {
	uint32_t             cx_nr;
	/**
	 * Objects in the configuration.
	 *
	 * @note Do not access this field directly, because actual in-memory
	 * size of object is larger than sizeof(struct m0_confx_obj). Use
	 * M0_CONFX_AT() instead.
	 */
	struct m0_confx_obj *cx__objs;
} M0_XCA_SEQUENCE;

/** Returns specific element of m0_confx::cx__objs. */
#define M0_CONFX_AT(cx, idx)                                    \
({                                                              \
	typeof(cx)   __cx  = (cx);                              \
	uint32_t     __idx = (idx);                             \
	M0_ASSERT(__idx <= __cx->cx_nr);                        \
	(typeof(&(cx)->cx__objs[0]))(((char *)__cx->cx__objs) + \
				    __idx * m0_confx_sizeof()); \
})

M0_INTERNAL size_t m0_confx_sizeof(void);

/* ------------------------------------------------------------------
 * Configuration fops
 * ------------------------------------------------------------------ */

/** Configuration fetch request. */
struct m0_conf_fetch {
	/** Configuration object the path originates from. */
	struct m0_fid  f_origin;
	/** Path components. */
	struct arr_fid f_path;
} M0_XCA_RECORD;

/** Confd's response to m0_conf_fetch. */
struct m0_conf_fetch_resp {
	/** Result of configuration retrieval (-Exxx = failure, 0 = success). */
	int32_t         fr_rc;
	/** A sequence of configuration object descriptors. */
	struct m0_confx fr_data;
} M0_XCA_RECORD;

/** XXX FUTURE: Configuration update request. */
struct m0_conf_update {
	/** Configuration object the path originates from. */
	struct m0_fid   u_origin;
	/** A sequence of configuration object descriptors. */
	struct m0_confx u_data;
} M0_XCA_RECORD;

/** XXX FUTURE: Confd's response to m0_conf_update. */
struct m0_conf_update_resp {
	/** Result of update request (-Exxx = failure, 0 = success). */
	int32_t ur_rc;
} M0_XCA_RECORD;

#endif /* __MERO_CONF_ONWIRE_H__ */
