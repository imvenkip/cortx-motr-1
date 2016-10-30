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
 *                  Rajanikant Chirmade <rajanikant.chirmade@seagate.com>
 * Original creation date: 05-Dec-2014
 */
#pragma once
#ifndef __MERO_CONF_HELPERS_H__
#define __MERO_CONF_HELPERS_H__

#include "conf/obj.h"

struct m0_confc;
struct m0_rconfc;
struct m0_fid;
struct m0_reqh;

struct m0_confc_args {
	/** Cofiguration profile. */
	const char            *ca_profile;
	/** Cofiguration string. */
	char                  *ca_confstr;
	/** Configuration retrieval state machine. */
	struct m0_sm_group    *ca_group;
	/** Configuration retrieval rpc machine. */
	struct m0_rpc_machine *ca_rmach;
};

/** Finds a device object using device index as a key. */
M0_INTERNAL int m0_conf_device_cid_to_fid(struct m0_confc *confc, uint64_t cid,
					  const struct m0_fid *conf_profile,
					  struct m0_fid *out);

/**
 * Obtains filesystem object associated with given profile.
 *
 * @note The resulting conf object must be m0_confc_close()d eventually.
 *
 * @pre  m0_conf_fid_type(profile) == &M0_CONF_PROFILE_TYPE
 */
M0_INTERNAL int m0_conf_fs_get(const struct m0_fid        *profile,
			       struct m0_confc            *confc,
			       struct m0_conf_filesystem **result);

/**
 * Obtains a pool version that consists of online elements only.
 *
 * @note The resulting conf object should be m0_confc_close()d eventually.
 *
 * @see m0_conf_pver_find()
 */
M0_INTERNAL int m0_conf_pver_get(const struct m0_fid  *profile,
				 struct m0_confc      *confc,
				 struct m0_conf_pver **out);

/*
 * m0_conf_service_get() / m0_conf_sdev_get() / m0_conf_disk_get()
 * finds conf object by its fid and returns this object opened.
 *
 * Caller is responsible for m0_confc_close()ing the returned object.
 */
#define M0_CONF_OBJ_GETTERS \
	X(m0_conf_service); \
	X(m0_conf_sdev);    \
	X(m0_conf_disk)

#define X(type)                                          \
M0_INTERNAL int type ## _get(struct m0_confc     *confc, \
			     const struct m0_fid *fid,   \
			     struct type        **out)
M0_CONF_OBJ_GETTERS;
#undef X

/** Loads full configuration tree. */
M0_INTERNAL int m0_conf_full_load(struct m0_conf_filesystem *fs);

/**
 * Locates conf service object identified by fid (optional), service type and
 * endpoint address. In case fid is NULL, service type and endpoint address are
 * used to locate the object in configuration.
 *
 * The object is looked up in confc belonging to REQH instance.
 *
 * In case service object not found in cache immediately by fid, a service
 * object of the given type and endpoint is looked up in configuration under
 * current profile.
 *
 * The found service object is ultimately tested for having the provided
 * endpoint address.
 */
M0_INTERNAL int m0_conf_service_find(struct m0_reqh            *reqh,
				     const struct m0_fid       *fid,
				     enum m0_conf_service_type  type,
				     const char                *ep_addr,
				     struct m0_conf_obj       **svc_obj);

M0_INTERNAL int m0_conf_service_open(struct m0_confc            *confc,
				     const struct m0_fid        *profile,
				     const char                 *ep,
				     enum m0_conf_service_type   svc_type,
				     struct m0_conf_service    **svc);

/**
 * Opens root configuration object.
 *
 * @param confc  Initialised confc instance.
 * @param root   Output parameter. Should be m0_confc_close()d by user.
 */
M0_INTERNAL int m0_conf_root_open(struct m0_confc      *confc,
				  struct m0_conf_root **root);

/** Get service name by service configuration object */
M0_INTERNAL char *m0_conf_service_name_dup(const struct m0_conf_service *svc);

M0_INTERNAL struct m0_reqh *m0_conf_obj2reqh(const struct m0_conf_obj *obj);

M0_INTERNAL struct m0_reqh *m0_confc2reqh(const struct m0_confc *confc);

M0_INTERNAL bool m0_conf_obj_is_pool(const struct m0_conf_obj *obj);

/** Obtains m0_conf_pver array from rack/enclousure/controller. */
M0_INTERNAL struct m0_conf_pver **m0_conf_pvers(const struct m0_conf_obj *obj);

/* XXX TODO: Move to mero/ha.c as a static function. */
M0_INTERNAL bool m0_conf_service_is_top_rms(const struct m0_conf_service *svc);

/**
 * Checks that 'obj' is of type M0_CONF_DISK_TYPE and this disk is attached to
 * a service of one of types specified in 'svc_types' bitmask.
 *
 * Example:
 * @code
 * m0_is_disk_of_type(conf_obj, M0_BITS(M0_CST_IOS, M0_CST_CAS));
 * @encode
 */
M0_INTERNAL bool m0_disk_is_of_type(const struct m0_conf_obj *obj,
				    uint64_t                  svc_types);
M0_INTERNAL bool m0_is_cas_disk(const struct m0_conf_obj *obj);
M0_INTERNAL bool m0_is_ios_disk(const struct m0_conf_obj *obj);

/**
 * Counts number of devices in configuration that are attached to services of
 * types provided by 'svc_types' bitmask.
 *
 * Example:
 * @code
 * rc = m0_conf_devices_count(profile, confc, M0_BITS(M0_CST_IOS), &nr);
 * @endcode
 */
M0_INTERNAL int m0_conf_devices_count(struct m0_fid   *profile,
				      struct m0_confc *confc,
				      uint64_t         svc_types,
				      uint32_t        *nr_devices);

M0_INTERNAL void m0_confc_expired_cb(struct m0_rconfc *rconfc);
M0_INTERNAL void m0_confc_ready_cb(struct m0_rconfc *rconfc);

/**
 * Finds out if service configuration includes the specified endpoint address,
 * i.e. endpoint is known to service configuration.
 */
M0_INTERNAL bool m0_conf_service_ep_is_known(const struct m0_conf_obj *svc_obj,
					     const char               *ep_addr);

/**
 * Gets service fid of type stype from process with process_fid.
 */
M0_INTERNAL int m0_conf_process2service_get(struct m0_confc *confc,
					    const struct m0_fid *process_fid,
					    enum m0_conf_service_type stype,
					    struct m0_fid *sfid);

/* --------------------------------- >8 --------------------------------- */

/**
 * @todo XXX RELOCATEME: This function belongs ha subsystem, not conf.
 *
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
 *
 */
M0_INTERNAL int m0_conf_objs_ha_update(struct m0_ha_nvec *nvec);

/**
 * @todo XXX RELOCATEME: This function belongs ha subsystem, not conf.
 */
M0_INTERNAL int m0_conf_obj_ha_update(const struct m0_fid *obj_fid);

/**
 * @todo XXX RELOCATEME: This function belongs ha subsystem, not conf.
 *
 * Update configuration objects ha state from ha service.
 * Fetches HA state of configuration objects from HA service and
 * updates local configuration cache.
 */
M0_INTERNAL int m0_conf_confc_ha_update(struct m0_confc *confc);

#endif /* __MERO_CONF_HELPERS_H__ */
