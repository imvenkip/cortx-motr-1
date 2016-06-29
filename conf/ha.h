/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <max.medved@seagate.com>
 * Original creation date: 20-Jun-2016
 */

#pragma once

#ifndef __MERO_CONF_HA_H__
#define __MERO_CONF_HA_H__

/**
 * @defgroup conf-ha
 *
 * @{
 */
#include "lib/types.h"          /* uint64_t */
#include "xcode/xcode_attr.h"   /* M0_XCA_RECORD */

struct m0_fid;
struct m0_ha;
struct m0_ha_link;

enum m0_conf_ha_process_event {
	M0_CONF_HA_PROCESS_STARTING,
	M0_CONF_HA_PROCESS_STARTED,
	M0_CONF_HA_PROCESS_STOPPING,
	M0_CONF_HA_PROCESS_STOPPED,
};

struct m0_conf_ha_process {
	/** @see m0_conf_ha_process_event for values */
	uint64_t chp_event;
	/**
	 * PID of the current process.
	 * 0 if it's a kernel mode "process" (m0t1fs mount, for example).
	 */
	uint64_t chp_pid;
} M0_XCA_RECORD;

/** Send notification about process state to HA */
M0_INTERNAL void
m0_conf_ha_process_event_post(struct m0_ha                  *ha,
                              struct m0_ha_link             *hl,
                              const struct m0_fid           *process_fid,
                              uint64_t                       pid,
                              enum m0_conf_ha_process_event  event);


/** @} end of conf-ha group */
#endif /* __MERO_CONF_HA_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */