/* -*- c -*- */
/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Andriy Tkachuk <andriy_tkachuk@xyratex.com>
 *                  Mandar Sawant <mandar_sawant@seagate.com>
 * Original creation date: 05-Dec-2014
 */
#pragma once
#ifndef __MERO_CONF_HELPERS_H__
#define __MERO_CONF_HELPERS_H__

#include "lib/tlist.h"
#include "conf/schema.h" /* m0_conf_service_type */

struct m0_confc;
struct m0_conf_obj;
struct m0_conf_root;
struct m0_conf_filesystem;
struct m0_conf_pver;
struct m0_conf_process;
struct m0_conf_disk;
struct m0_conf_service;
struct m0_conf_sdev;
struct m0_fid;
struct m0_rpc_session;
struct m0_flset;
struct m0_conf_obj_type;
struct m0_ha_nvec;

/**
 * Count number of objects of type "type" from specified path.
 *
 * @param profile profile to be used from configuration.
 * @param confc   already initialised confc instance.
 * @param filter  filter for objects to be count.
 * @param count   return value for count.
 * @param path    path from which objects to be counted.
 */
#define m0_conf_obj_count(profile, confc, filter, count, ...)       \
	m0_conf__obj_count(profile, confc, filter, count,           \
			   M0_COUNT_PARAMS(__VA_ARGS__) + 1,        \
			   (const struct m0_fid []){                \
			   __VA_ARGS__, M0_FID0 })

struct m0_confc_args {
	/** Cofiguration profile. */
	const char            *ca_profile;
	/** Cofiguration string. */
	const char            *ca_confstr;
	/** Cofiguration server endpoint. */
	const char            *ca_confd;
	/** Configuration retrieval state machine. */
	struct m0_sm_group    *ca_group;
	/** Configuration retrieval rpc machine. */
	struct m0_rpc_machine *ca_rmach;
};

/**
 * Obtains filesystem object associated with given profile.
 *
 * @note m0_conf_fs_get() initialises @confc. It is user's responsibility
 *       to finalise it.
 */
M0_INTERNAL int m0_conf_fs_get(const struct m0_fid        *profile,
			       struct m0_confc            *confc,
			       struct m0_conf_filesystem **result);


/** Obtains device object associated with given fid. */
M0_INTERNAL int m0_conf_device_get(struct m0_confc      *confc,
				   struct m0_fid        *fid,
				   struct m0_conf_sdev **sdev);

/**
 * Obtains disk object associated with given profile.
 */
M0_INTERNAL int m0_conf_disk_get(struct m0_confc      *confc,
			         struct m0_fid        *fid,
				 struct m0_conf_disk **disk);

/** Finds pool version which does not intersect with the given failure set. */
M0_INTERNAL int m0_conf_poolversion_get(const struct m0_fid  *profile,
					struct m0_confc      *confc,
					struct m0_flset      *failure_set,
					struct m0_conf_pver **result);

/** Loads full configuration tree. */
M0_INTERNAL int m0_conf_full_load(struct m0_conf_filesystem *fs);

/**
 * Update configuration objects ha state from ha service according to provided
 * HA note vector.
 *
 * The difference from m0_conf_confc_ha_state_update() is dealing with an
 * arbitrary note vector. Client may fill in the vector following any logic that
 * suits its needs. All the status results which respective conf objects exist
 * in the provided confc instance cache will be applied to all HA clients
 * currently registered with HA global context.
 *
 * @pre nvec->nv_nr <= M0_HA_STATE_UPDATE_LIMIT
 */
M0_INTERNAL int m0_conf_objs_ha_update(struct m0_rpc_session *ha_sess,
				       struct m0_ha_nvec     *nvec);

M0_INTERNAL int m0_conf_obj_ha_update(struct m0_rpc_session *ha_sess,
				      const struct m0_fid   *obj_fid);

/**
 * Update configuration objects ha state from ha service.
 * Fetches HA state of configuration objects from HA service and
 * updates local configuration cache.
 */
M0_INTERNAL int m0_conf_confc_ha_update(struct m0_rpc_session *ha_sess,
					struct m0_confc       *confc);
/**
 * Update configuration objects ha state from ha service according to provided
 * HA note vector.
 *
 * The difference from m0_conf_ha_state_update() is dealing with an arbitrary
 * note vector. Client may fill in the vector following any logic that suits its
 * needs. All the status results which respective conf objects exist in the
 * provided confc instance cache will be applied to the cache.
 */
M0_INTERNAL int m0_conf_ha_state_discover(struct m0_rpc_session *ha_sess,
					  struct m0_ha_nvec     *nvec,
					  struct m0_confc       *confc);

/**
 * Opens root configuration object.
 * @param confc already initialised confc instance
 * @param root  output parameter. Should be closed by user.
 */
M0_INTERNAL int m0_conf_root_open(struct m0_confc      *confc,
				  struct m0_conf_root **root);

/** Get service name by service configuration object */
M0_INTERNAL char *m0_conf_service_name_dup(const struct m0_conf_service *svc);

M0_INTERNAL bool m0_obj_is_pver(const struct m0_conf_obj *obj);

M0_INTERNAL struct m0_reqh *m0_conf_obj2reqh(const struct m0_conf_obj *obj);

M0_INTERNAL bool m0_conf_is_pool_version_dirty(struct m0_confc     *confc,
					       const struct m0_fid *pver_fid);

/**
 * Internal function to get number of objects of type "type" from specified path.
 * @note this function is called from m0_conf_obj_count().
 */
M0_INTERNAL int m0_conf__obj_count(const struct m0_fid *profile,
				   struct m0_confc     *confc,
				   bool (*filter)(const struct m0_conf_obj *obj),
				   int                 *count,
				   int                  level,
				   const struct m0_fid *path);

/** Obtains m0_conf_pver array from rack/enclousure/controller. */
M0_INTERNAL struct m0_conf_pver **m0_conf_pvers(const struct m0_conf_obj *obj);

M0_INTERNAL int m0_conf_service_open(struct m0_confc            *confc,
				     const struct m0_fid        *profile,
				     const char                 *ep,
				     enum m0_conf_service_type   svc_type,
				     struct m0_conf_service    **svc);

#endif /* __MERO_CONF_HELPERS_H__ */
