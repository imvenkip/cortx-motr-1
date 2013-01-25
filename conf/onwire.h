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

/* XXX TODO: rename to `arrbuf' */
struct arr_buf {
	uint32_t       ab_count;
	struct m0_buf *ab_elems;
} M0_XCA_SEQUENCE;

/** XXX @todo s/objid/objiden/ ? */
struct objid {
	uint32_t      oi_type; /* see m0_conf_objtype for values */
	struct m0_buf oi_id;
} M0_XCA_RECORD;

/* ------------------------------------------------------------------
 * Configuration objects
 * ------------------------------------------------------------------ */

/* Note that m0_confx_dir does not exist. */

struct m0_confx_profile {
	/* Name of profile's filesystem. */
	struct m0_buf xp_filesystem;
} M0_XCA_RECORD;

struct m0_confx_filesystem {
	/* Rood fid. */
	struct m0_fid  xf_rootfid;
	/* Filesystem parameters. */
	struct arr_buf xf_params;
	/* Services of this filesystem. */
	struct arr_buf xf_services;
} M0_XCA_RECORD;

struct m0_confx_service {
	/* Service type.  See m0_conf_service_type. */
	uint32_t       xs_type;
	/* End-points from which this service is reachable. */
	struct arr_buf xs_endpoints;
	/* Hosting node. */
	struct m0_buf  xs_node;
} M0_XCA_RECORD;

struct m0_confx_node {
	/* Memory size in MB. */
	uint32_t       xn_memsize;
	/* Number of processors. */
	uint32_t       xn_nr_cpu;
	/* Last known state.  See m0_cfg_state_bit. */
	uint64_t       xn_last_state;
	/* Property flags.  See m0_cfg_flag_bit. */
	uint64_t       xn_flags;
	/* Pool id. */
	uint64_t       xn_pool_id;
	/* Network interfaces. */
	struct arr_buf xn_nics;
	/* Storage devices. */
	struct arr_buf xn_sdevs;
} M0_XCA_RECORD;

struct m0_confx_nic {
	/* Type of network interface.  See m0_cfg_nic_type. */
	uint32_t      xi_iface;
	/* Maximum transmission unit. */
	uint32_t      xi_mtu;
	/* Speed in Mb/sec. */
	uint64_t      xi_speed;
	/* Filename in host OS. */
	struct m0_buf xi_filename;
	/* Last known state.  See m0_cfg_state_bit. */
	uint64_t      xi_last_state;
} M0_XCA_RECORD;

struct m0_confx_sdev {
	/* Interface type.  See m0_cfg_storage_device_interface_type. */
	uint32_t       xd_iface;
	/* Media type.  See m0_cfg_storage_device_media_type. */
	uint32_t       xd_media;
	/* Size in bytes. */
	uint64_t       xd_size;
	/* Last known state.  See m0_cfg_state_bit. */
	uint64_t       xd_last_state;
	/* Property flags.  See m0_cfg_flag_bit. */
	uint64_t       xd_flags;
	/* Filename in host OS. */
	struct m0_buf  xd_filename;
	/* Partitions of this storage device. */
	struct arr_buf xd_partitions;
} M0_XCA_RECORD;

struct m0_confx_partition {
	/* Start offset in bytes. */
	uint64_t      xa_start;
	/* Size in bytes. */
	uint64_t      xa_size;
	/* Partition index. */
	uint32_t      xa_index;
	/* Partition type.  See m0_cfg_storage_device_partition_type. */
	uint32_t      xa_type;
	/* Filename in host OS. */
	struct m0_buf xa_file;
} M0_XCA_RECORD;

struct m0_confx_u {
	uint32_t u_type; /* see m0_conf_objtype for values */
	union {
		/*
		 * Note that there is no such thing as `m0_confx_dir'.
		 * One-to-many relations are represented by a list of
		 * identifiers --- `arr_buf'.
		 */
		struct m0_confx_profile    u_profile    M0_XCA_TAG("1");
		struct m0_confx_filesystem u_filesystem M0_XCA_TAG("2");
		struct m0_confx_service    u_service    M0_XCA_TAG("3");
		struct m0_confx_node       u_node       M0_XCA_TAG("4");
		struct m0_confx_nic        u_nic        M0_XCA_TAG("5");
		struct m0_confx_sdev       u_sdev       M0_XCA_TAG("6");
		struct m0_confx_partition  u_partition  M0_XCA_TAG("7");
	} u;
} M0_XCA_UNION;

/** Configuration object descriptor. */
struct m0_confx_obj {
	struct m0_buf     o_id;   /*< Object identifier. */
	struct m0_confx_u o_conf; /*< Configuration data. */
} M0_XCA_RECORD;

/** Encoded configuration --- a sequence of m0_confx_objs. */
struct m0_confx {
	uint32_t             cx_nr;
	struct m0_confx_obj *cx_objs;
} M0_XCA_SEQUENCE;

/* ------------------------------------------------------------------
 * Configuration fops
 * ------------------------------------------------------------------ */

/** Configuration request. */
struct m0_conf_fetch {
	/** Configuration object the path originates from. */
	struct objid   f_origin;
	/** Path components. */
	struct arr_buf f_path;
} M0_XCA_RECORD;

/** Confd's response to m0_conf_fetch. */
struct m0_conf_fetch_resp {
	/** Result of configuration retrieval (-Exxx = failure, 0 = success). */
	uint32_t        fr_rc;
	/** A sequence of configuration object descriptors. */
	struct m0_confx fr_data;
} M0_XCA_RECORD;

/** XXX FUTURE: Update request. */
struct m0_conf_update {
	/** Configuration object the path originates from. */
	struct objid    f_origin;
	/** A sequence of configuration object descriptors. */
	struct m0_confx fr_data;
} M0_XCA_RECORD;

/** XXX FUTURE: Confd's response to m0_conf_update. */
struct m0_conf_update_resp {
	/** Result of update request (-Exxx = failure, 0 = success). */
	uint32_t fr_rc;
} M0_XCA_RECORD;

#endif /* __MERO_CONF_ONWIRE_H__ */
