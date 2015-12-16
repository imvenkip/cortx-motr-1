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
#include "stob/ad.h"                /* m0_stob_ad_type */
#include "stob/linux.h"             /* m0_stob_linux */
#include "ioservice/fid_convert.h"  /* m0_fid_validate_linuxstob */
#include "ioservice/storage_dev.h"
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
				     struct m0_stob_domain  *bstore_dom)
{
	M0_ENTRY();
	M0_PRE(bstore_dom != NULL);
	devs->sds_back_domain = bstore_dom;
	storage_dev_tlist_init(&devs->sds_devices);
	devs->sds_be_seg = be_seg;
	m0_mutex_init(&devs->sds_lock);
	return M0_RC(m0_parallel_pool_init(&devs->sds_pool, 10, 20));
}

M0_INTERNAL void m0_storage_devs_fini(struct m0_storage_devs *devs)
{
	m0_parallel_pool_terminate_wait(&devs->sds_pool);
	m0_parallel_pool_fini(&devs->sds_pool);
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

static void
conf_sdev_associate(struct m0_stob_linux *lstob, const struct m0_fid *conf_sdev)
{
	M0_PRE(_0C(m0_conf_fid_is_valid(conf_sdev)) &&
	       _0C(m0_conf_fid_type(conf_sdev) == &M0_CONF_SDEV_TYPE));
	M0_PRE(ergo(m0_fid_is_set(&lstob->sl_conf_sdev),
		    m0_fid_eq(&lstob->sl_conf_sdev, conf_sdev)));

	lstob->sl_conf_sdev = *conf_sdev;
}

static int storage_dev_attach(struct m0_storage_devs    *devs,
			      uint64_t                   cid,
			      const char                *path,
			      uint64_t                   size,
			      const struct m0_conf_sdev *conf_sdev)
{
	struct m0_storage_dev *device;
	struct m0_stob_id      stob_id;
	struct m0_stob        *stob;
	int                    rc;

	M0_ENTRY("cid=%llu", (unsigned long long)cid);
	M0_PRE(m0_storage_devs_find_by_cid(devs, cid) == NULL);

	M0_ALLOC_PTR(device);
	if (device == NULL)
		return M0_ERR(-ENOMEM);

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
			rc = -EINVAL;
		} else if (conf_sdev != NULL) {
			/* Ensure that we are dealing with a linux stob. */
			M0_ASSERT(m0_fid_validate_linuxstob(&stob_id));
			conf_sdev_associate(m0_stob_linux_container(stob),
					    &conf_sdev->sd_obj.co_id);
		}
	}
stob_put:
	/* Decrement stob reference counter, incremented by m0_stob_find() */
	m0_stob_put(stob);
end:
	if (rc == 0)
		storage_dev_tlink_init_at_tail(device, &devs->sds_devices);
	else
		m0_free(device);
	return M0_RC(rc);
}

M0_INTERNAL int m0_storage_dev_attach(struct m0_storage_devs *devs,
				      uint64_t                cid,
				      const char             *path,
				      uint64_t                size)
{
	return storage_dev_attach(devs, cid,
				  M0_FI_ENABLED("no_real_dev") ? NULL : path,
				  size, NULL);
}

M0_INTERNAL int m0_storage_dev_attach_by_conf(struct m0_storage_devs    *devs,
					      const struct m0_conf_sdev *sdev)
{
	return storage_dev_attach(
		devs, sdev->sd_obj.co_id.f_key,
		M0_FI_ENABLED("no_real_dev") ? NULL : sdev->sd_filename,
		sdev->sd_size, sdev);
}

M0_INTERNAL void m0_storage_dev_detach(struct m0_storage_dev *dev)
{
	m0_stob_domain_fini(dev->isd_domain);
	storage_dev_tlink_del_fini(dev);
	m0_free(dev);
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
