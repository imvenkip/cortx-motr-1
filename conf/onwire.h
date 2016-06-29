/* -*- c -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
#include "lib/types.h"     /* m0_conf_verno_t */
#include "lib/bitmap.h"    /* m0_bitmap_onwire */
#include "lib/bitmap_xc.h" /* m0_bitmap_onwire */
#include "lib/buf_xc.h"
#include "fid/fid.h"
#include "fid/fid_xc.h"

/* export */
struct m0_conf_fetch;
struct m0_conf_fetch_resp;
struct m0_conf_update;
struct m0_conf_update_resp;

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

struct m0_confx_root {
	struct m0_confx_header xt_header;
	/* Configuration database version. */
	uint64_t               xt_verno;
	/* Profiles in configuration database. */
	struct m0_fid_arr      xt_profiles;
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
	/* Distributed index meta-data pool version. */
	struct m0_fid          xf_imeta_pver;
	/* Nodes of this filesystem. */
	struct m0_fid_arr      xf_nodes;
	/* Pools this filesystem resides on. */
	struct m0_fid_arr      xf_pools;
	/* Racks this filesystem resides on. */
	struct m0_fid_arr      xf_racks;
} M0_XCA_RECORD;

struct m0_confx_pool {
	struct m0_confx_header xp_header;
	/* Order in set of pool. */
	uint32_t               xp_order;
	/* Pool versions for this pool. */
	struct m0_fid_arr      xp_pvers;
} M0_XCA_RECORD;

struct m0_confx_pver_actual {
	/* Number of data units in a parity group. */
	uint32_t          xva_N;
	/* Number of parity units in a parity group. */
	uint32_t          xva_K;
	/* Pool width. */
	uint32_t          xva_P;
	/*
	 * Tolerance constraint.
	 * NOTE: The number of elements must be equal to M0_CONF_PVER_HEIGHT.
	 */
	struct arr_u32    xva_tolerance;
	/* Rack versions. */
	struct m0_fid_arr xva_rackvs;
	/*
	 * Note that "recd" attribute exists in local conf cache
	 * only and is never transferred over the wire.
	 */
} M0_XCA_RECORD;

struct m0_confx_pver_formulaic {
	/* Cluster-unique identifier of this formulaic pver. */
	uint32_t       xvf_id;
	/* Fid of the base pool version. */
	struct m0_fid  xvf_base;
	/*
	 * Allowance vector.
	 * NOTE: The number of elements must be equal to M0_CONF_PVER_HEIGHT.
	 */
	struct arr_u32 xvf_allowance;
} M0_XCA_RECORD;

enum { M0_CONFX_PVER_ACTUAL, M0_CONFX_PVER_FORMULAIC };

struct m0_confx_pver_u {
	uint8_t xpv_is_formulaic;
	union {
		struct m0_confx_pver_actual    xpv_actual
			M0_XCA_TAG("M0_CONFX_PVER_ACTUAL");
		struct m0_confx_pver_formulaic xpv_formulaic
			M0_XCA_TAG("M0_CONFX_PVER_FORMULAIC");
	} u;
} M0_XCA_UNION;

struct m0_confx_pver {
	struct m0_confx_header xv_header;
	struct m0_confx_pver_u xv_u;
} M0_XCA_RECORD;

struct m0_confx_objv {
	struct m0_confx_header xj_header;
	/* Identifier of real device associated with this version. */
	struct m0_fid          xj_real;
	struct m0_fid_arr      xj_children;
	/*
	 * Note that "ix" attribute exists in local conf cache only
	 * and is never transferred over the wire.
	 */
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
	struct m0_fid_arr      xn_processes;
} M0_XCA_RECORD;

struct m0_confx_process {
	struct m0_confx_header  xr_header;
	struct m0_bitmap_onwire xr_cores;
	uint64_t                xr_mem_limit_as;
	uint64_t                xr_mem_limit_rss;
	uint64_t                xr_mem_limit_stack;
	uint64_t                xr_mem_limit_memlock;
	struct m0_buf           xr_endpoint;
	/* Services being run by this process. */
	struct m0_fid_arr       xr_services;
} M0_XCA_RECORD;

struct m0_confx_service {
	struct m0_confx_header xs_header;
	/* Service type.  See m0_conf_service_type. */
	uint32_t               xs_type;
	/* End-points from which this service is reachable. */
	struct m0_bufs         xs_endpoints;
	/* Devices associated with service. */
	struct m0_fid_arr      xs_sdevs;
} M0_XCA_RECORD;

struct m0_confx_sdev {
	struct m0_confx_header xd_header;
	/** Device index between 1 to poolwidth. */
	uint32_t               xd_dev_idx;
	/* Interface type.  See m0_cfg_storage_device_interface_type. */
	uint32_t               xd_iface;
	/* Media type.  See m0_cfg_storage_device_media_type. */
	uint32_t               xd_media;
	/* Block size in bytes. */
	uint32_t               xd_bsize;
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
	struct m0_fid_arr      xr_encls;
	/* Pool versions this rack is part of. */
	struct m0_fid_arr      xr_pvers;
} M0_XCA_RECORD;

struct m0_confx_enclosure {
	struct m0_confx_header xe_header;
	/* Controllers in this enclosure. */
	struct m0_fid_arr      xe_ctrls;
	/* Pool versions this enclosure is part of. */
	struct m0_fid_arr      xe_pvers;
} M0_XCA_RECORD;

struct m0_confx_controller {
	struct m0_confx_header xc_header;
	/* The node this controller is associated with. */
	struct m0_fid          xc_node;
	/* Storage disks attached to this controller. */
	struct m0_fid_arr      xc_disks;
	/* Pool versions this controller is part of. */
	struct m0_fid_arr      xc_pvers;
} M0_XCA_RECORD;

struct m0_confx_disk {
	struct m0_confx_header xk_header;
	/* Storage device associated with this disk. */
	struct m0_fid          xk_dev;
	/* Pool versions this disk is part of. */
	struct m0_fid_arr      xk_pvers;
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
	struct m0_fid     f_origin;
	/** Path components. */
	struct m0_fid_arr f_path;
} M0_XCA_RECORD;

/** Confd's response to m0_conf_fetch. */
struct m0_conf_fetch_resp {
	/** Result of configuration retrieval (-Exxx = failure, 0 = success). */
	int32_t         fr_rc;
	/** configuration version number */
	uint64_t        fr_ver;
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
