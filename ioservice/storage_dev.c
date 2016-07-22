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
static bool storage_devs_conf_ready_cb(struct m0_clink *link);

static bool storage_devs_is_locked(struct m0_storage_devs *devs)
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
	m0_clink_init(&devs->sds_conf_ready, storage_devs_conf_ready_cb);
	m0_clink_init(&devs->sds_conf_exp, storage_devs_conf_expired_cb);
	m0_clink_add_lock(&reqh->rh_conf_cache_exp, &devs->sds_conf_exp);
	m0_clink_add_lock(&reqh->rh_conf_cache_ready, &devs->sds_conf_ready);
	return M0_RC(m0_parallel_pool_init(&devs->sds_pool, 10, 20));
}

M0_INTERNAL void m0_storage_devs_fini(struct m0_storage_devs *devs)
{
	m0_parallel_pool_terminate_wait(&devs->sds_pool);
	m0_parallel_pool_fini(&devs->sds_pool);
	m0_clink_cleanup(&devs->sds_conf_exp);
	m0_clink_cleanup(&devs->sds_conf_ready);
	m0_clink_fini(&devs->sds_conf_exp);
	m0_clink_fini(&devs->sds_conf_ready);
	storage_dev_tlist_fini(&devs->sds_devices);
	m0_mutex_fini(&devs->sds_lock);
}

M0_INTERNAL struct m0_storage_dev *
m0_storage_devs_find_by_cid(struct m0_storage_devs *devs,
			    uint64_t                cid)
{
	return m0_tl_find(storage_dev, dev, &devs->sds_devices,
			  dev->isd_cid == cid);
}

M0_INTERNAL struct m0_storage_dev *
m0_storage_devs_find_by_dom(struct m0_storage_devs *devs,
			    struct m0_stob_domain  *dom)
{
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
	m0_clink_del_lock(link);
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

static bool storage_devs_conf_expired_cb(struct m0_clink *clink)
{
	struct m0_storage_dev  *dev;
	struct m0_conf_obj     *obj;
	struct m0_storage_devs *storage_devs =
				  container_of(clink,
					       struct m0_storage_devs,
					       sds_conf_exp);
	m0_tl_for (storage_dev, &storage_devs->sds_devices, dev) {
		/* Not all storage devices have a corresponding m0_conf_sdev
		   object.
		*/
		if (!m0_clink_is_armed(&dev->isd_clink))
			continue;
		obj = container_of(dev->isd_clink.cl_chan, struct m0_conf_obj,
				   co_ha_chan);
		M0_ASSERT(m0_conf_obj_invariant(obj));
		m0_storage_dev_clink_del(&dev->isd_clink);
		m0_confc_close(obj);
		dev->isd_ha_state = M0_NC_UNKNOWN;

	} m0_tl_endfor;
	return true;
}

static bool storage_devs_conf_ready_cb(struct m0_clink *clink)
{
	struct m0_storage_dev  *dev;
	struct m0_storage_devs *storage_devs =
					container_of(clink,
						     struct m0_storage_devs,
						     sds_conf_ready);
	struct m0_reqh         *reqh = container_of(clink->cl_chan,
						    struct m0_reqh,
						    rh_conf_cache_ready);
	struct m0_confc        *confc = m0_reqh2confc(reqh);
	struct m0_fid          *profile = &reqh->rh_profile;
	struct m0_fid           sdev_fid;
	struct m0_conf_sdev    *conf_sdev;
	struct m0_conf_service *conf_service;
	struct m0_conf_obj     *srv_obj;
	int                     rc;

	m0_tl_for (storage_dev, &storage_devs->sds_devices, dev) {
		rc = m0_conf_device_cid_to_fid(confc, dev->isd_cid,
					       profile, &sdev_fid);
		if (rc != 0)
			/* Not all storage devices have a corresponding
			 * m0_conf_sdev object.
			 */
			continue;
		rc = m0_conf_sdev_get(confc, &sdev_fid, &conf_sdev);
		if (rc != 0) {
			M0_LOG(M0_ERROR, "No such sdev: "FID_F,
			       FID_P(&sdev_fid));
			M0_IMPOSSIBLE(""); /* if asserts are enabled -- fail */
			break;
		}
		m0_storage_dev_clink_add(&dev->isd_clink,
					 &conf_sdev->sd_obj.co_ha_chan);
		dev->isd_ha_state = conf_sdev->sd_obj.co_ha_state;
		srv_obj = m0_conf_obj_grandparent(&conf_sdev->sd_obj);
		conf_service = M0_CONF_CAST(srv_obj, m0_conf_service);
		dev->isd_srv_type = conf_service->cs_type;
	} m0_tl_endfor;
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
	enum m0_stob_state     st;
	int                    rc;

	struct m0_storage_dev *dev =
		container_of(ref, struct m0_storage_dev, isd_ref);

	M0_ENTRY("dev=%p", dev);

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
	storage_dev_tlink_del_fini(dev);
	m0_chan_broadcast_lock(&dev->isd_detached_chan);
	m0_chan_fini_lock(&dev->isd_detached_chan);
	m0_mutex_fini(&dev->isd_detached_lock);
	m0_free(dev);

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

static int storage_dev_attach(struct m0_storage_devs    *devs,
			      uint64_t                   cid,
			      const char                *path,
			      uint64_t                   size,
			      struct m0_conf_sdev *conf_sdev)
{
	struct m0_storage_dev  *device;
	struct m0_conf_service *conf_service;
	struct m0_conf_obj     *srv_obj;
	struct m0_stob_id       stob_id;
	struct m0_stob         *stob;
	int                     rc;

	M0_ENTRY("cid=%llu", (unsigned long long)cid);
	M0_PRE(m0_storage_devs_find_by_cid(devs, cid) == NULL);

	M0_ALLOC_PTR(device);
	if (device == NULL)
		return M0_ERR(-ENOMEM);

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
		m0_storage_dev_get(device);
		m0_mutex_init(&device->isd_detached_lock);
		m0_chan_init(&device->isd_detached_chan,
			     &device->isd_detached_lock);
		storage_dev_tlink_init_at_tail(device, &devs->sds_devices);
	} else
		m0_free(device);
	return M0_RC(rc);
}

M0_INTERNAL int m0_storage_dev_attach(struct m0_storage_devs *devs,
				      uint64_t                cid,
				      const char             *path,
				      uint64_t                size,
				      struct m0_conf_sdev *conf_sdev)
{
	return storage_dev_attach(devs, cid,
				  M0_FI_ENABLED("no_real_dev") ? NULL : path,
				  size, conf_sdev);
}

M0_INTERNAL int m0_storage_dev_attach_by_conf(struct m0_storage_devs *devs,
					      struct m0_conf_sdev    *sdev)
{
	return storage_dev_attach(
		devs, sdev->sd_dev_idx,
		M0_FI_ENABLED("no_real_dev") ? NULL : sdev->sd_filename,
		sdev->sd_size, M0_FI_ENABLED("no-conf-dev") ? NULL : sdev);
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
