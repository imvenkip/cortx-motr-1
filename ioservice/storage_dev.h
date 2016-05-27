/* -*- C -*- */
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

#pragma once

#ifndef __MERO_IOSERVICE_STORAGE_DEV_H__
#define __MERO_IOSERVICE_STORAGE_DEV_H__

#include "lib/tlist.h"
#include "lib/mutex.h"
#include "lib/thread_pool.h"
#include "lib/chan.h"
#include "ha/note.h"
#include "conf/obj.h"

/* import */
struct m0_stob;
struct m0_stob_domain;
struct m0_be_seg;
struct m0_conf_sdev;
struct m0_reqh;

/**
 * @defgroup sdev Storage devices.
 *
 * This module provides functionality to work with storage devices used by IO
 * service. It is intended to be used to attach/detach storage devices
 * dynamically and query them for some information (i.e. used/free space).
 *
 * Backing store domain and BE segment for storage devices are provided by user.
 *
 * Structure m0_storage_devs contains list of storage devices used by IO
 * service and intended to be the one and only place to hold such a list for
 * IO service.
 *
 * Functions provided by this module do not take any locks internally. User
 * should lock m0_storage_devs structure on demand using
 * m0_storage_devs_lock(), m0_storage_devs_unlock() functions.
 *
 * @{
 */

/**
 * Data structure represents storage device object.
 */
struct m0_storage_dev {
	/** linux stob of device */
	struct m0_stob            *isd_stob;
	/** AD stob domain for AD stobs of device */
	struct m0_stob_domain     *isd_domain;
	/** Storage device ID */
	uint64_t                   isd_cid;
	/** Linkage into list of storage devices. */
	struct m0_tlink            isd_linkage;
	/**
	 * A link to receive HA state change notification. It waits
	 * on disk obj's wait channel, m0_conf_obj::co_ha_chan.
	 */
	struct m0_clink            isd_clink;
	/** HA state associated with the disk corresponding to the
	 * storage device.
	 */
	enum m0_ha_obj_state       isd_ha_state;
	/* Type of the parent service. */
	enum m0_conf_service_type  isd_srv_type;
	/* * Magic for isd_linkage */
	uint64_t                   isd_magic;
};

M0_TL_DESCR_DECLARE(storage_dev, M0_EXTERN);
M0_TL_DECLARE(storage_dev, M0_EXTERN, struct m0_storage_dev);

/**
 * Structure contains list of storage device
 * and their common additional data.
 */
struct m0_storage_devs {
	/** Mutex to protect sds_devices list */
	struct m0_mutex          sds_lock;
	/** Linkage into list of storage devices. */
	struct m0_tl             sds_devices;
	/** Backing store stob domain. One per all storage devices. */
	struct m0_stob_domain   *sds_back_domain;
	/** Backend segment. One per all storage devices. */
	struct m0_be_seg        *sds_be_seg;
	/** Parallel pool processing list of storage devs */
	struct m0_parallel_pool  sds_pool;

	/* Links to detect expiration and availability of conf. */
	struct m0_clink          sds_conf_exp;
	struct m0_clink          sds_conf_ready;
};

/**
 * Initialises storage devices structure.
 *
 * Backing store domain should be already created and initialised.
 */
M0_INTERNAL int m0_storage_devs_init(struct m0_storage_devs *devs,
				     struct m0_be_seg       *be_seg,
				     struct m0_stob_domain  *bstore_dom,
				     struct m0_reqh         *reqh);
/**
 * Finalises storage devices structure.
 */
M0_INTERNAL void m0_storage_devs_fini(struct m0_storage_devs *devs);

/**
 * Locks storage devices structure.
 */
M0_INTERNAL void m0_storage_devs_lock(struct m0_storage_devs *devs);

/**
 * Unlocks storage devices structure.
 */
M0_INTERNAL void m0_storage_devs_unlock(struct m0_storage_devs *devs);

/**
 * Finds storage device by its cid.
 */
M0_INTERNAL struct m0_storage_dev *
m0_storage_devs_find_by_cid(struct m0_storage_devs *devs,
			    uint64_t                cid);

/**
 * Attaches device using information from configuration object.
 *
 * Extracts device parameters and executes m0_storage_dev_attach.
 */
M0_INTERNAL int m0_storage_dev_attach_by_conf(struct m0_storage_devs *devs,
					      struct m0_conf_sdev    *sdev);

/**
 * Attaches storage device.
 *
 * Creates backing store stob in domain provided in m0_storage_devs_init.
 * Also creates AD stob domain for storage device.
 */
M0_INTERNAL int m0_storage_dev_attach(struct m0_storage_devs *devs,
				      uint64_t                cid,
				      const char             *path,
				      uint64_t                size,
				      struct m0_conf_sdev    *sdev);

/**
 * Detaches storage device.
 *
 * Finalises (but not destroys) underlying AD stob domain and backing store
 * stob.
 */
M0_INTERNAL void m0_storage_dev_detach(struct m0_storage_dev *dev);

/**
 * Detaches all storage devices.
 */
M0_INTERNAL void m0_storage_devs_detach_all(struct m0_storage_devs *devs);

/**
 * Used/free space of storage device.
 *
 * @see m0_storage_dev_space()
 */
struct m0_storage_space {
	m0_bcount_t sds_free_blocks;
	m0_bcount_t sds_block_size;
	m0_bcount_t sds_total_size;
};

/**
 * Obtains information about free space of the device.
 */
M0_INTERNAL void m0_storage_dev_space(struct m0_storage_dev   *dev,
				      struct m0_storage_space *space);

/**
 * Formats storage device.
 *
 * Not implemented yet.
 */
M0_INTERNAL int m0_storage_dev_format(struct m0_storage_dev *dev,
				      uint64_t               cid);

/**
 * Does fdatasync on all stobs in storage devices.
 */
M0_INTERNAL int m0_storage_devs_fdatasync(struct m0_storage_devs *devs);

/** @} end of sdev group */
#endif /* __MERO_IOSERVICE_STORAGE_DEV_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
