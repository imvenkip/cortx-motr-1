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
#include "lib/assert.h"
#include "lib/mutex.h"
#include "lib/finject.h"     /* M0_FI_ENABLED */
#include "balloc/balloc.h"
#include "conf/obj.h"        /* m0_conf_sdev */
#include "stob/ad.h"         /* m0_stob_ad_type */
#include "stob/type.h"       /* m0_stob_type */
#include "ioservice/storage_dev.h"

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
	return M0_RC(0);
}

M0_INTERNAL void m0_storage_devs_fini(struct m0_storage_devs *devs)
{
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

M0_INTERNAL int m0_storage_dev_attach_by_conf(struct m0_storage_devs *devs,
					      struct m0_conf_sdev    *sdev)
{
	const char *dev_fname = NULL;
	int         rc;

	M0_PRE(sdev != NULL);
	if (M0_FI_ENABLED("no_real_dev")) {
		dev_fname = sdev->sd_filename;
		sdev->sd_filename = NULL;
	}
	rc = m0_storage_dev_attach(devs, sdev->sd_obj.co_id.f_key,
				   sdev->sd_filename, sdev->sd_size);
	if (dev_fname != NULL)
		/* Fault injection enabled */
		sdev->sd_filename = dev_fname;
	return M0_RC(rc);

}

M0_INTERNAL int m0_storage_dev_attach(struct m0_storage_devs *devs,
				      uint64_t                cid,
				      const char             *path,
				      uint64_t                size)
{
	struct m0_storage_dev *device;
	char                   location[64];
	char                  *dom_cfg;
	struct m0_stob_id      stob_id;
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
		goto find_fail;

	if (m0_stob_state_get(device->isd_stob) == CSS_UNKNOWN) {
		rc = m0_stob_locate(device->isd_stob);
		if (rc != 0)
			goto locate_fail;
	}

	if (m0_stob_state_get(device->isd_stob) == CSS_NOENT) {
		rc = m0_stob_create(device->isd_stob, NULL, path);
		if (rc != 0)
			goto locate_fail;
	}

	rc = snprintf(location, sizeof(location),
		      "adstob:%llu", (unsigned long long)cid);
	if (rc < 0)
		goto locate_fail;
	M0_ASSERT(rc < sizeof(location));
	m0_stob_ad_cfg_make(&dom_cfg, devs->sds_be_seg, &stob_id, size);
	if (dom_cfg == NULL)
		rc = -ENOMEM;
	else
		rc = m0_stob_domain_create_or_init(location, NULL,
						   cid, dom_cfg,
						   &device->isd_domain);
	if (rc == 0 && M0_FI_ENABLED("ad_domain_locate_fail")) {
		m0_stob_domain_fini(device->isd_domain);
		rc = -EINVAL;
	}
	m0_free(dom_cfg);

locate_fail:
	/* Decrement stob reference counter, incremented by m0_stob_find() */
	m0_stob_put(device->isd_stob);
find_fail:
	if (rc == 0)
		storage_dev_tlink_init_at_tail(device, &devs->sds_devices);
	else
		m0_free(device);
	return M0_RC(rc);
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
