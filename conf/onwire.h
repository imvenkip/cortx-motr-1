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
 * Original authors: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>,
 *		     Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>
 * Original creation date: 20-Aug-2012
 */
#pragma once
#ifndef __COLIBRI_CONF_ONWIRE_H__
#define __COLIBRI_CONF_ONWIRE_H__

#include "xcode/xcode.h"
#include "lib/buf_xc.h"

/* export */
struct c2_conf_fetch;
struct c2_conf_fetch_resp;
struct c2_conf_update;
struct c2_conf_update_resp;

struct arr_buf {
	uint32_t       ab_count;
	struct c2_buf *ab_elems;
} C2_XCA_SEQUENCE;

/** XXX @todo s/objid/objiden/ ? */
struct objid {
	uint32_t      oi_type; /* see c2_conf_objtype for values */
	struct c2_buf oi_id;
} C2_XCA_RECORD;

/** XXX @todo Use c2_fid? */
struct fid {
	uint64_t f_container;
	uint64_t f_key;
} C2_XCA_RECORD;

/* ------------------------------------------------------------------
 * Configuration objects
 * ------------------------------------------------------------------ */
struct confx_profile {
	/* Name of profile's filesystem. */
	struct c2_buf xp_filesystem;
} C2_XCA_RECORD;

struct confx_filesystem {
	/* Rood fid. */
	struct fid     xf_rootfid;
	/* Filesystem parameters. */
	struct arr_buf xf_params;
	/* Services of this filesystem. */
	struct arr_buf xf_services;
} C2_XCA_RECORD;

struct confx_service {
	/* Service type.  See c2_cfg_service_type. */
	uint32_t       xs_type;
	/* End-points from which this service is reachable. */
	struct arr_buf xs_endpoints;
	/* Hosting node. */
	struct c2_buf  xs_node;
} C2_XCA_RECORD;

struct confx_node {
	/* Memory size in MB. */
	uint32_t       xn_memsize;
	/* Number of processors. */
	uint32_t       xn_nr_cpu;
	/* Last known state.  See c2_cfg_state_bit. */
	uint64_t       xn_last_state;
	/* Property flags.  See c2_cfg_flag_bit. */
	uint64_t       xn_flags;
	/* Pool id. */
	uint64_t       xn_pool_id;
	/* Network interfaces. */
	struct arr_buf xn_nics;
	/* Storage devices. */
	struct arr_buf xn_sdevs;
} C2_XCA_RECORD;

struct confx_nic {
	/* Type of network interface.  See c2_cfg_nic_type. */
	uint32_t      xi_iface;
	/* Maximum transmission unit. */
	uint32_t      xi_mtu;
	/* Speed in Mb/sec. */
	uint64_t      xi_speed;
	/* Filename in host OS. */
	struct c2_buf xi_filename;
	/* Last known state.  See c2_cfg_state_bit. */
	uint64_t      xi_last_state;
} C2_XCA_RECORD;

struct confx_sdev {
	/* Interface type.  See c2_cfg_storage_device_interface_type. */
	uint32_t       xd_iface;
	/* Media type.  See c2_cfg_storage_device_media_type. */
	uint32_t       xd_media;
	/* Size in bytes. */
	uint64_t       xd_size;
	/* Last known state.  See c2_cfg_state_bit. */
	uint64_t       xd_last_state;
	/* Property flags.  See c2_cfg_flag_bit. */
	uint64_t       xd_flags;
	/* Filename in host OS. */
	struct c2_buf  xd_filename;
	/* Partitions of this storage device. */
	struct arr_buf xd_partitions;
} C2_XCA_RECORD;

struct confx_partition {
	/* Start offset in bytes. */
	uint64_t      xa_start;
	/* Size in bytes. */
	uint64_t      xa_size;
	/* Partition index. */
	uint32_t      xa_index;
	/* Partition type.  See c2_cfg_storage_device_partition_type. */
	uint32_t      xa_type;
	/* Filename in host OS. */
	struct c2_buf xa_file;
} C2_XCA_RECORD;

struct confx_u {
	uint32_t u_type; /* see c2_conf_objtype for values */
	union {
		/*
		 * Note that there is no such thing as `confx_dir'.
		 * One-to-many relations are represented by a list of
		 * identifiers --- `arr_buf'.
		 */
		struct confx_profile    u_profile    C2_XCA_TAG("1");
		struct confx_filesystem u_filesystem C2_XCA_TAG("2");
		struct confx_service    u_service    C2_XCA_TAG("3");
		struct confx_node       u_node       C2_XCA_TAG("4");
		struct confx_nic        u_nic        C2_XCA_TAG("5");
		struct confx_sdev       u_sdev       C2_XCA_TAG("6");
		struct confx_partition  u_partition  C2_XCA_TAG("7");
	} u;
} C2_XCA_UNION;

/** Configuration object descriptor. */
struct confx_object {
	struct c2_buf  o_id;   /*< Object identifier. */
	struct confx_u o_conf; /*< Configuration data. */
} C2_XCA_RECORD;

struct enconf {
	uint32_t             ec_nr;
	struct confx_object *ec_objs;
} C2_XCA_SEQUENCE;

/* ------------------------------------------------------------------
 * Configuration fops
 * ------------------------------------------------------------------ */

/** Configuration request. */
struct c2_conf_fetch {
	/** Configuration object the path originates from. */
	struct objid   f_origin;
	/** Path components. */
	struct arr_buf f_path;
} C2_XCA_RECORD;

/** Confd's response to c2_conf_fetch. */
struct c2_conf_fetch_resp {
	/** Result of configuration retrieval (-Exxx = failure, 0 = success). */
	uint32_t      fr_rc;
	/** A sequence of configuration object descriptors. */
	struct enconf fr_data;
} C2_XCA_RECORD;

/** Update request. */
struct c2_conf_update {
	/** Configuration object the path originates from. */
	struct objid  f_origin;
	/** A sequence of configuration object descriptors. */
	struct enconf fr_data;
} C2_XCA_RECORD;

/** Confd's response to c2_conf_update. */
struct c2_conf_update_resp {
	/** Result of update request (-Exxx = failure, 0 = success). */
	uint32_t fr_rc;
} C2_XCA_RECORD;

#endif /* __COLIBRI_CONF_ONWIRE_H__ */
