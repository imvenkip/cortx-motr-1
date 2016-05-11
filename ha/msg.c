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
 * Original creation date: 26-Apr-2016
 */


/**
 * @addtogroup ha
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_HA
#include "lib/trace.h"

#include "ha/msg.h"

#include "lib/misc.h"   /* memcmp */

M0_INTERNAL bool m0_ha_msg_eq(const struct m0_ha_msg *msg1,
			      const struct m0_ha_msg *msg2)
{
	return m0_fid_eq(&msg1->hm_fid, &msg2->hm_fid) &&
	       m0_fid_eq(&msg1->hm_source_process, &msg2->hm_source_process) &&
	       m0_fid_eq(&msg1->hm_source_service, &msg2->hm_source_service) &&
	       msg1->hm_time == msg2->hm_time &&
	       msg1->hm_data.hed_type == msg2->hm_data.hed_type &&
	       /*
		* Note: it's not reqired by the standard because structs can
		* have padding bytes. Please remove this memcmp if you see any
		* problem with it.
		*/
	       memcmp(&msg1->hm_data,
		      &msg2->hm_data, sizeof msg1->hm_data) == 0;
}


#undef M0_TRACE_SUBSYSTEM

/** @} end of ha group */

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
