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
 * Original author: Mikhail Antropov <mikhail.v.antropov@seagate.com>
 * Original creation date: 08/06/2015
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_IOSERVICE
#include "lib/trace.h"

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/finject.h"            /* M0_FI_ENABLED */
#include "lib/string.h"             /* m0_strdup */
#include "balloc/balloc.h"          /* b2m0 */
#include "conf/obj.h"               /* m0_conf_sdev */
#include "conf/confc.h"             /* m0_confc_from_obj */
#include "conf/helpers.h"           /* m0_conf_disk_get */
#include "conf/diter.h"             /* m9_conf_diter */
#include "conf/obj_ops.h"           /* M0_CONF_DIRNEXT */
#include "stob/ad.h"                /* m0_stob_ad_type */
#include "stob/linux.h"             /* m0_stob_linux */
#include "ioservice/fid_convert.h"  /* m0_fid_validate_linuxstob */
#include "ioservice/storage_dev.h"
#include "reqh/reqh.h"              /* m0_reqh */
#include <unistd.h>                 /* fdatasync */
#ifndef __KERNEL__
#  include "pool/pool.h"            /* m0_pools_common */
#endif

/**
   @addtogroup sdev

   @{
 */

/**
 * tlist descriptor for list of m0_storage_dev objects placed
 * in m0_storage_devs::sds_devices list using isd_linkage.
 */
M0_TL_DESCR_DEFINE(storage_dev, "storage_dev", M0_INTERNAL,
		   struct m0_storage_dev, isd_linkage, isd_magic,
		   M0_STORAGE_DEV_MAGIC, M0_STORAGE_DEV_HEAD_MAGIC);

M0_TL_DEFINE(storage_dev, M0_INTERNAL, struct m0_storage_dev);

static bool storage_dev_state_update_cb(struct m0_clink *link);
static bool storage_devs_conf_expired_cb(struct m0_clink *link);
static bool storage_devs_conf_ready_async_cb(struct m0_clink *link);

static bool storage_devs_is_locked(const struct m0_storage_devs *devs)
{
	return m0_mutex_is_locked(&devs->sds_lock);
}

M0_INTERNAL void m0_storage_devs_lock(struct m0_storage_devs *devs)
{
	m0_mutex_lock(&devs->sds_lock);
}

M0_INTERNAL void m0_storage_devs_unlock(struct m0_storage_devs *devs)
{
	m0_mutex_unlock(&devs->sds_lock);
}

M0_INTERNAL int m0_storage_devs_init(struct m0_storage_devs *devs,
				     struct m0_be_seg       *be_seg,
				     struct m0_stob_domain  *bstore_dom,
				     struct m0_reqh         *reqh)
{
	M0_ENTRY();
	M0_PRE(bstore_dom != NULL);
	devs->sds_back_domain = bstore_dom;
	storage_dev_tlist_init(&devs->sds_devices);
	devs->sds_be_seg = be_seg;
	m0_mutex_init(&devs->sds_lock);
	m0_clink_init(&devs->sds_conf_ready_async,
		      storage_devs_conf_ready_async_cb);
	m0_clink_init(&devs->sds_conf_exp, storage_devs_conf_expired_cb);
	m0_clink_add_lock(&reqh->rh_conf_cache_exp, &devs->sds_conf_exp);
	m0_clink_add_lock(&reqh->rh_conf_cache_ready_async,
			  &devs->sds_conf_ready_async);
	return M0_RC(m0_parallel_pool_init(&devs->sds_pool, 10, 20));
}

M0_INTERNAL void m0_storage_devs_fini(struct m0_storage_devs *devs)
{
	struct m0_storage_dev  *dev;

	m0_parallel_pool_terminate_wait(&devs->sds_pool);
	m0_parallel_pool_fini(&devs->sds_pool);
	m0_clink_cleanup(&devs->sds_conf_exp);
	m0_clink_cleanup(&devs->sds_conf_ready_async);
	m0_clink_fini(&devs->sds_conf_exp);
	m0_clink_fini(&devs->sds_conf_ready_async);

	m0_tl_for(storage_dev, &devs->sds_devices, dev) {
		M0_LOG(M0_DEBUG, "fini: dev=%p, ref=%" PRIi64
		       "state=%d type=%d, %"PRIu64,
		       dev,
		       m0_ref_read(&dev->isd_ref),
		       dev->isd_ha_state,
		       dev->isd_srv_type,
		       dev->isd_cid);
	} m0_tl_endfor;

	storage_dev_tlist_fini(&devs->sds_devices);
	m0_mutex_fini(&devs->sds_lock);
}

M0_INTERNAL struct m0_storage_dev *
m0_storage_devs_find_by_cid(struct m0_storage_devs *devs,
			    uint64_t                cid)
{
	M0_PRE(storage_devs_is_locked(devs));
	return m0_tl_find(storage_dev, dev, &devs->sds_devices,
			  dev->isd_cid == cid);
}

M0_INTERNAL struct m0_storage_dev *
m0_storage_devs_find_by_dom(struct m0_storage_devs *devs,
			    struct m0_stob_domain  *dom)
{
	M0_PRE(storage_devs_is_locked(devs));
	return m0_tl_find(storage_dev, dev, &devs->sds_devices,
			  dev->isd_domain == dom);
}

M0_INTERNAL void m0_storage_dev_clink_add(struct m0_clink *link,
					  struct m0_chan *chan)
{
	m0_clink_init(link, storage_dev_state_update_cb);
	m0_clink_add_lock(chan, link);
}

M0_INTERNAL void m0_storage_dev_clink_del(struct m0_clink *link)
{
	m0_clink_cleanup(link);
	m0_clink_fini(link);
}

static bool storage_dev_state_update_cb(struct m0_clink *link)
{
	struct m0_storage_dev *dev =
		container_of(link, struct m0_storage_dev, isd_clink);
	struct m0_conf_obj *obj =
		container_of(link->cl_chan, struct m0_conf_obj, co_ha_chan);
	M0_PRE(m0_conf_fid_type(&obj->co_id) == &M0_CONF_SDEV_TYPE);
	dev->isd_ha_state = obj->co_ha_state;
	return true;
}

static void dev_filename_update(struct m0_storage_dev    *dev,
				const struct m0_conf_obj *obj)
{
	m0_free0(&dev->isd_filename);
	dev->isd_filename = m0_strdup(
		M0_CONF_CAST(obj, m0_conf_sdev)->sd_filename);
	if (dev->isd_filename == NULL)
		M0_ERR_INFO(-ENOMEM, "Unable to duplicate sd_filename %s for "
			    FID_F, M0_CONF_CAST(obj, m0_conf_sdev)->sd_filename,
			    FID_P(&obj->co_id));
}

static bool storage_devs_conf_expired_cb(struct m0_clink *clink)
{
	struct m0_storage_dev  *dev;
	struct m0_conf_obj     *obj;
	struct m0_storage_devs *storage_devs = M0_AMB(storage_devs, clink,
						      sds_conf_exp);
	struct m0_reqh         *reqh = M0_AMB(reqh, clink->cl_chan,
					      rh_conf_cache_exp);
	struct m0_pools_common *pc = reqh->rh_pools;
	struct m0_conf_cache   *cache = &m0_reqh2confc(reqh)->cc_cache;
	struct m0_fid           sdev_fid;

	m0_storage_devs_lock(storage_devs);
	m0_tl_for (storage_dev, &storage_devs->sds_devices, dev) {
		/*
		 * Step 1. Need to save current sdev filename attribute in
		 * order to use it later in storage_dev_update_by_conf() when
		 * new conf is ready.
		 */
		sdev_fid = pc->pc_dev2ios[dev->isd_cid].pds_sdev_fid;
		if (m0_fid_is_set(&sdev_fid) &&
		    /*
		     * Not all storage devices have a corresponding m0_conf_sdev
		     * object.
		     */
		    (obj = m0_conf_cache_lookup(cache, &sdev_fid)) != NULL)
			dev_filename_update(dev, obj);
		/*
		 * Step 2. Un-link from HA chan to let conf cache be ultimately
		 * drained by rconfc.
		 */
		if (!m0_clink_is_armed(&dev->isd_clink))
			continue;
		obj = M0_AMB(obj, dev->isd_clink.cl_chan, co_ha_chan);
		M0_ASSERT(m0_conf_obj_invariant(obj));
		if (!m0_fid_is_set(&sdev_fid))
			/* Step 1, second chance to catch up with filename. */
			dev_filename_update(dev, obj);
		/* Un-link from HA chan and un-pin the conf object. */
		m0_storage_dev_clink_del(&dev->isd_clink);
		m0_confc_close(obj);
		dev->isd_ha_state = M0_NC_UNKNOWN;
	} m0_tl_endfor;
	m0_storage_devs_unlock(storage_devs);
	return true;
}

static int storage_dev_update_by_conf(struct m0_storage_dev  *dev,
				      struct m0_conf_sdev    *sdev,
				      struct m0_storage_devs *storage_devs)
{
	struct m0_storage_dev *dev_new;
	int                    rc;

	M0_ENTRY("dev cid:%"PRIx64", fid: "FID_F", old: %s, new: %s",
		 dev->isd_cid, FID_P(&sdev->sd_obj.co_id), dev->isd_filename,
		 M0_MEMBER(sdev, sd_filename));
	M0_PRE(sdev != NULL);
	M0_PRE(storage_devs_is_locked(storage_devs));

	if (m0_streq(dev->isd_filename, sdev->sd_filename))
		return M0_RC(0);  /* the dev did not change */

	M0_PRE(m0_ref_read(&dev->isd_ref) == 1);
	m0_storage_dev_detach(dev);
	rc = m0_storage_dev_new_by_conf(storage_devs, sdev, &dev_new);
	if (rc != 0)
		return M0_ERR(rc);
	M0_ASSERT(dev_new != NULL);
	m0_storage_dev_attach(dev_new, storage_devs);
	return M0_RC(0);
}

static void storage_devs_conf_refresh(struct m0_storage_devs *storage_devs,
				      struct m0_reqh         *reqh)
{
	struct m0_confc       *confc = m0_reqh2confc(reqh);
	struct m0_storage_dev *dev;
	struct m0_fid          sdev_fid;
	struct m0_conf_sdev   *conf_sdev;
	int                    rc;

	M0_PRE(storage_devs_is_locked(storage_devs));

	m0_tl_for(storage_dev, &storage_devs->sds_devices, dev) {
		rc = m0_conf_device_cid_to_fid(confc, dev->isd_cid,
					       &reqh->rh_profile, &sdev_fid);
		if (rc != 0)
			/* Not all storage devices have a corresponding
			 * m0_conf_sdev object.
			 */
			continue;
		conf_sdev = NULL;
		rc = m0_conf_sdev_get(confc, &sdev_fid, &conf_sdev) ?:
			storage_dev_update_by_conf(dev, conf_sdev,
						   storage_devs);
		if (rc != 0)
			M0_ERR(rc);
		if (conf_sdev != NULL)
			m0_confc_close(&conf_sdev->sd_obj);
	} m0_tl_endfor;
}

static bool storage_devs_conf_ready_async_cb(struct m0_clink *clink)
{
	struct m0_storage_devs *storage_devs = M0_AMB(storage_devs, clink,
						      sds_conf_ready_async);
	struct m0_reqh         *reqh = M0_AMB(reqh, clink->cl_chan,
					      rh_conf_cache_ready_async);
	struct m0_storage_dev  *dev;
	struct m0_confc        *confc = m0_reqh2confc(reqh);
	struct m0_fid          *profile = &reqh->rh_profile;
	struct m0_fid           sdev_fid;
	struct m0_conf_sdev    *conf_sdev = NULL;
	struct m0_conf_service *conf_service;
	struct m0_conf_obj     *srv_obj;
	int                     rc;

	M0_ENTRY();
	m0_storage_devs_lock(storage_devs);
	storage_devs_conf_refresh(storage_devs, reqh);

	m0_tl_for (storage_dev, &storage_devs->sds_devices, dev) {
		rc = m0_conf_device_cid_to_fid(confc, dev->isd_cid,
					       profile, &sdev_fid);
		if (rc != 0)
			/* Not all storage devices have a corresponding
			 * m0_conf_sdev object.
			 */
			continue;
		rc = m0_conf_sdev_get(confc, &sdev_fid, &conf_sdev);
		M0_ASSERT_INFO(rc == 0, "No sdev: "FID_F, FID_P(&sdev_fid));
		M0_ASSERT(conf_sdev != NULL);
		M0_LOG(M0_DEBUG, "cid:0x%"PRIx64" -> sdev_fid:"FID_F" idx:0x%x",
		       dev->isd_cid, FID_P(&sdev_fid), conf_sdev->sd_dev_idx);
		if (!m0_clink_is_armed(&dev->isd_clink))
			m0_storage_dev_clink_add(&dev->isd_clink,
						 &conf_sdev->sd_obj.co_ha_chan);
		dev->isd_ha_state = conf_sdev->sd_obj.co_ha_state;
		srv_obj = m0_conf_obj_grandparent(&conf_sdev->sd_obj);
		conf_service = M0_CONF_CAST(srv_obj, m0_conf_service);
		dev->isd_srv_type = conf_service->cs_type;
	} m0_tl_endfor;

	m0_storage_devs_unlock(storage_devs);
	M0_LEAVE();
	return true;
}

static int stob_domain_create_or_init(uint64_t                 cid,
				      const struct m0_be_seg  *be_seg,
				      const struct m0_stob_id *bstore_id,
				      m0_bcount_t              size,
				      struct m0_stob_domain  **out)
{
	char  location[64];
	char *cfg;
	int   rc;

	rc = snprintf(location, sizeof(location),
		      "adstob:%llu", (unsigned long long)cid);
	if (rc < 0)
		return M0_ERR(rc);
	M0_ASSERT(rc < sizeof(location));

	m0_stob_ad_cfg_make(&cfg, be_seg, bstore_id, size);
	if (cfg == NULL)
		return M0_ERR(-ENOMEM);
	rc = m0_stob_domain_create_or_init(location, NULL, cid, cfg, out);
	m0_free(cfg);
	return M0_RC(rc);
}

static void storage_dev_release(struct m0_ref *ref)
{
	struct m0_storage_dev *dev =
		container_of(ref, struct m0_storage_dev, isd_ref);

	M0_ENTRY("dev=%p", dev);

	storage_dev_tlink_del_fini(dev);
	m0_chan_broadcast_lock(&dev->isd_detached_chan);
	m0_storage_dev_destroy(dev);

	M0_LEAVE();
}

M0_INTERNAL void m0_storage_dev_get(struct m0_storage_dev *dev)
{
	m0_ref_get(&dev->isd_ref);
}

M0_INTERNAL void m0_storage_dev_put(struct m0_storage_dev *dev)
{
	m0_ref_put(&dev->isd_ref);
}

static int storage_dev_new(struct m0_storage_devs *devs,
			   uint64_t                cid,
			   bool                    fi_no_dev,
			   const char             *path_orig,
			   uint64_t                size,
			   struct m0_conf_sdev    *conf_sdev,
			   struct m0_storage_dev **out)
{
	struct m0_storage_dev  *device;
	struct m0_conf_service *conf_service;
	struct m0_conf_obj     *srv_obj;
	struct m0_stob_id       stob_id;
	struct m0_stob         *stob;
	const char             *path = fi_no_dev ? NULL : path_orig;
	int                     rc;

	M0_ENTRY("cid=%"PRIu64, cid);

	M0_ALLOC_PTR(device);
	if (device == NULL)
		return M0_ERR(-ENOMEM);

	if (path_orig != NULL) {
		device->isd_filename = m0_strdup(path_orig);
		if (device->isd_filename == NULL) {
			m0_free(device);
			return M0_ERR(-ENOMEM);
		}
	}
	m0_ref_init(&device->isd_ref, 0, storage_dev_release);
	device->isd_cid = cid;

	m0_stob_id_make(0, cid, &devs->sds_back_domain->sd_id, &stob_id);
	rc = m0_stob_find(&stob_id, &device->isd_stob);
	if (rc != 0)
		goto end;
	stob = device->isd_stob;

	if (m0_stob_state_get(stob) == CSS_UNKNOWN) {
		rc = m0_stob_locate(stob);
		if (rc != 0)
			goto stob_put;
	}
	if (m0_stob_state_get(stob) == CSS_NOENT) {
		rc = m0_stob_create(stob, NULL, path);
		if (rc != 0)
			goto stob_put;
	}

	rc = stob_domain_create_or_init(cid, devs->sds_be_seg, &stob_id, size,
					&device->isd_domain);
	if (rc == 0) {
		if (M0_FI_ENABLED("ad_domain_locate_fail")) {
			m0_stob_domain_fini(device->isd_domain);
			M0_ASSERT(conf_sdev != NULL);
			m0_confc_close(&conf_sdev->sd_obj);
			rc = -EINVAL;
		} else if (conf_sdev != NULL) {
			if (m0_fid_validate_linuxstob(&stob_id))
				m0_stob_linux_conf_sdev_associate(stob,
						&conf_sdev->sd_obj.co_id);
			m0_conf_obj_get_lock(&conf_sdev->sd_obj);
			srv_obj = m0_conf_obj_grandparent(&conf_sdev->sd_obj);
			conf_service = M0_CONF_CAST(srv_obj, m0_conf_service);
			m0_storage_dev_clink_add(&device->isd_clink,
					&conf_sdev->sd_obj.co_ha_chan);
			device->isd_srv_type = conf_service->cs_type;
		}
	}
stob_put:
	/* Decrement stob reference counter, incremented by m0_stob_find() */
	m0_stob_put(stob);
end:
	if (rc == 0) {
		m0_mutex_init(&device->isd_detached_lock);
		m0_chan_init(&device->isd_detached_chan,
			     &device->isd_detached_lock);
		*out = device;
	} else {
		m0_free(M0_MEMBER(device, isd_filename));
		m0_free(device);
	}
	return M0_RC(rc);
}

M0_INTERNAL int m0_storage_dev_new(struct m0_storage_devs *devs,
				   uint64_t                cid,
				   const char             *path,
				   uint64_t                size,
				   struct m0_conf_sdev    *conf_sdev,
				   struct m0_storage_dev **dev)
{
	return storage_dev_new(devs, cid,
			       M0_FI_ENABLED("no_real_dev"), path,
			       size, conf_sdev, dev);
}

M0_INTERNAL int m0_storage_dev_new_by_conf(struct m0_storage_devs *devs,
					   struct m0_conf_sdev    *sdev,
					   struct m0_storage_dev **dev)
{
	return storage_dev_new(devs, sdev->sd_dev_idx,
			       M0_FI_ENABLED("no_real_dev"), sdev->sd_filename,
			       sdev->sd_size,
			       M0_FI_ENABLED("no-conf-dev") ? NULL : sdev, dev);
}

M0_INTERNAL void m0_storage_dev_destroy(struct m0_storage_dev *dev)
{
	enum m0_stob_state st;
	int                rc;

	M0_PRE(m0_ref_read(&dev->isd_ref) == 0);

	/* Find the linux stob and acquire a reference. */
	rc = m0_stob_find(&dev->isd_stob->so_id, &dev->isd_stob);
	if (rc == 0) {
		/* Finalise the AD stob domain.*/
		m0_stob_domain_fini(dev->isd_domain);
		/* Destroy linux stob. */
		st = m0_stob_state_get(dev->isd_stob);
		if (st == CSS_EXISTS)
			rc = m0_stob_destroy(dev->isd_stob, NULL);
		else
			m0_stob_put(dev->isd_stob);
	}
	if (rc != 0)
		M0_LOG(M0_ERROR, "Failed to destroy linux stob rc=%d", rc);
	m0_chan_fini_lock(&dev->isd_detached_chan);
	m0_mutex_fini(&dev->isd_detached_lock);
	m0_free(dev->isd_filename);
	m0_free(dev);
}

M0_INTERNAL void m0_storage_dev_attach(struct m0_storage_dev  *dev,
				       struct m0_storage_devs *devs)
{
	M0_PRE(storage_devs_is_locked(devs));
	M0_PRE(m0_storage_devs_find_by_cid(devs, dev->isd_cid) == NULL);

	M0_LOG(M0_DEBUG, "get: dev=%p, ref=%" PRIi64
	       "state=%d type=%d, %"PRIu64,
	       dev,
	       m0_ref_read(&dev->isd_ref),
	       dev->isd_ha_state,
	       dev->isd_srv_type,
	       dev->isd_cid);
	m0_storage_dev_get(dev);
	storage_dev_tlink_init_at_tail(dev, &devs->sds_devices);
}

M0_INTERNAL void m0_storage_dev_detach(struct m0_storage_dev *dev)
{
	struct m0_conf_obj *obj;

	if (m0_clink_is_armed(&dev->isd_clink)) {
		obj = container_of(dev->isd_clink.cl_chan, struct m0_conf_obj,
				   co_ha_chan);
		m0_storage_dev_clink_del(&dev->isd_clink);
		m0_confc_close(obj);
	}
	M0_LOG(M0_DEBUG, "get: dev=%p, ref=%" PRIi64
	       "state=%d type=%d, %"PRIu64,
	       dev,
	       m0_ref_read(&dev->isd_ref),
	       dev->isd_ha_state,
	       dev->isd_srv_type,
	       dev->isd_cid);
	m0_storage_dev_put(dev);
}

M0_INTERNAL void m0_storage_dev_space(struct m0_storage_dev   *dev,
				      struct m0_storage_space *space)
{
	struct m0_stob_ad_domain *ad_domain;
	struct m0_balloc         *balloc;

	M0_ENTRY();
	M0_ASSERT(dev != NULL);

	ad_domain = stob_ad_domain2ad(dev->isd_domain);
	balloc = b2m0(ad_domain->sad_ballroom);
	M0_ASSERT(balloc != NULL);

	*space = (struct m0_storage_space) {
		.sds_free_blocks = balloc->cb_sb.bsb_freeblocks,
		.sds_block_size  = balloc->cb_sb.bsb_blocksize,
		.sds_total_size  = balloc->cb_sb.bsb_totalsize
	};
}

M0_INTERNAL void m0_storage_devs_detach_all(struct m0_storage_devs *devs)
{
	struct m0_storage_dev *dev;

	m0_tl_for(storage_dev, &devs->sds_devices, dev) {
		m0_storage_dev_detach(dev);
	} m0_tl_endfor;
}

M0_INTERNAL int m0_storage_dev_format(struct m0_storage_dev *dev,
				      uint64_t               cid)
{
	/*
	 * Nothing do for Format command.
	 */
	return M0_RC(0);
}

static int sdev_stob_fsync(void *psdev)
{
	int rc;
	struct m0_storage_dev *sdev = (struct m0_storage_dev *)psdev;
	struct m0_stob_linux *lstob = m0_stob_linux_container(sdev->isd_stob);

	rc = fdatasync(lstob->sl_fd);
	M0_POST(rc == 0); /* XXX */

	M0_LOG(M0_DEBUG, "fsync on fd: %d, done", lstob->sl_fd);
	return rc;
}

M0_INTERNAL int m0_storage_devs_fdatasync(struct m0_storage_devs *sdevs)
{
	int rc;

	M0_PRE(storage_devs_is_locked(sdevs));

	rc = M0_PARALLEL_FOR(storage_dev, &sdevs->sds_pool,
			     &sdevs->sds_devices, sdev_stob_fsync);
	return rc;
}

#undef M0_TRACE_SUBSYSTEM
/** @} end group sdev */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
