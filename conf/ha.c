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


/**
 * @addtogroup conf-ha
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CONF
#include "lib/trace.h"

#include "conf/ha.h"

#include "lib/memory.h"         /* M0_ALLOC_PTR */
#include "lib/time.h"           /* m0_time_now */
#include "lib/types.h"          /* PRIu64 */

#include "fid/fid.h"            /* FID_P */
#include "module/instance.h"    /* m0_get */
#include "ha/msg.h"             /* m0_ha_msg */
#include "ha/ha.h"              /* m0_ha_send */

M0_INTERNAL void
m0_conf_ha_process_event_post(struct m0_ha                  *ha,
                              struct m0_ha_link             *hl,
                              const struct m0_fid           *process_fid,
                              uint64_t                       pid,
                              enum m0_conf_ha_process_event  event)
{
	struct m0_ha_msg *msg;
	uint64_t          tag;

	M0_ENTRY("process_fid="FID_F" event=%d", FID_P(process_fid), event);
	M0_ALLOC_PTR(msg);
	if (msg == NULL) {
		M0_LOG(M0_ERROR, "can't allocate memory for msg");
		M0_LEAVE();
		return;
	}
	*msg = (struct m0_ha_msg){
		.hm_fid  = *process_fid,
		.hm_time = m0_time_now(),
		.hm_data = {
			.hed_type            = M0_HA_MSG_EVENT_PROCESS,
			.u.hed_event_process = {
				.chp_event = event,
				.chp_pid   = pid,
			},
		},
	};
	m0_ha_send(ha, hl, msg, &tag);
	m0_free(msg);
	M0_LEAVE("tag=%"PRIu64, tag);
}

#undef M0_TRACE_SUBSYSTEM

/** @} end of conf-ha group */

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