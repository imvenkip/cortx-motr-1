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
 * Original author: Nachiket Sahasrabudhe <nachiket_sahasrabuddhe@xyratex.com>
 * Original creation date: 12-Mar-15
 */

#pragma once

#ifndef __MERO_FD_FLSETS_H__
#define __MERO_FD_FLSETS_H__

#include "lib/tlist.h"

/**
 * Failure set module.
 *
 * The responsibility of failure set module is to track failed HW resources
 * through HA interface and take appropriate actions on HW resource failure.
 * HW resources are: rack, enclosure, controller. Note, that individual storage
 * devices are not tracked by failure sets.
 *
 * All failed resources are collected in m0_flset::fls_objs list.
 *
 * User is responsible to provide concurrency protection for m0_flset::fls_objs
 * list. According to the current design any access to this list is protected
 * by corresponding confc cache lock.
 */
struct m0_conf_filesystem;
struct m0_rpc_session;
struct m0_conf_obj;

struct m0_flset_clink {
	struct m0_clink       fcl_link;
	struct m0_flset      *fcl_parent;
	struct m0_fid         fcl_fid;
	enum m0_ha_obj_state  fcl_state;
};

struct m0_flset {
	/** Array of clinks to track state change of conf objects. */
	struct m0_flset_clink *fls_links;
	/** Number of elements in fls_links array */
	int                    fls_links_nr;
	/** Listener for configuration expiration event from rconfc. */
	struct m0_clink        fls_conf_expired;
	/** Listener for configuration ready event from rconfc. */
	struct m0_clink        fls_conf_ready;
};

/**
 * Build failure set of resources by scanning ha state.
 */
M0_INTERNAL int m0_flset_build(struct m0_flset           *flset,
			       struct m0_conf_filesystem *fs);

/**
 * Destroy failure set.
 */
M0_INTERNAL void m0_flset_destroy(struct m0_flset *flset);

/**
 * Check whether provided pool version contains device that is present in
 * failure set.
 */
M0_INTERNAL bool m0_flset_pver_has_failed_dev(struct m0_flset     *flset,
					      struct m0_conf_pver *pver);

#endif /* __MERO_FD_FLSETS_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
