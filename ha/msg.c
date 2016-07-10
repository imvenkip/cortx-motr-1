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

M0_INTERNAL void m0_ha_msg_debug_print(const struct m0_ha_msg *msg,
                                       const char             *prefix)
{
	const struct m0_ha_msg_data *data = &msg->hm_data;
	int                          i;

	M0_LOG(M0_DEBUG, "%s: hm_fid="FID_F" hm_source_process="FID_F" "
	       "hm_source_service="FID_F" hm_time=%"PRIu64" hm_tag=%"PRIu64,
	       prefix, FID_P(&msg->hm_fid), FID_P(&msg->hm_source_process),
	       FID_P(&msg->hm_source_service), msg->hm_time, msg->hm_tag);

	switch ((enum m0_ha_msg_type)data->hed_type) {
	case M0_HA_MSG_INVALID:
		M0_LOG(M0_WARN, "message has INVALID type");
		return;
	case M0_HA_MSG_STOB_IOQ:
		/* TODO */
		return;
	case M0_HA_MSG_NVEC:
	case M0_HA_MSG_NVEC_HACK:
		M0_LOG(M0_DEBUG, "NVEC hmnv_type=%"PRIu64" hmnv_nr=%"PRIu64" "
		       "hmnv_id_of_get=%"PRIu64,
		       data->u.hed_nvec.hmnv_type, data->u.hed_nvec.hmnv_nr,
		       data->u.hed_nvec.hmnv_id_of_get);
		for (i = 0; i < data->u.hed_nvec.hmnv_nr; ++i) {
			M0_LOG(M0_DEBUG, "hmnv_vec[%d]=(no_id="FID_F" "
			       "no_state=%"PRIu32")", i,
			       FID_P(&data->u.hed_nvec.hmnv_vec[i].no_id),
			       data->u.hed_nvec.hmnv_vec[i].no_state);
			if (data->u.hed_nvec.hmnv_vec[i].no_id.f_container <
			    0x10000000000UL) {
				M0_LOG(M0_ERROR, "invalid note: no_id="FID_F" "
				       "no_state=%"PRIu32,
				     FID_P(&data->u.hed_nvec.hmnv_vec[i].no_id),
				     data->u.hed_nvec.hmnv_vec[i].no_state);
			}

		}
		return;
	case M0_HA_MSG_FAILURE_VEC_REQ:
		M0_LOG(M0_DEBUG, "FAILURE_VEC_REQ mvq_pool="FID_F,
		       FID_P(&data->u.hed_fvec_req.mfq_pool));
		return;
	case M0_HA_MSG_FAILURE_VEC_REP:
		M0_LOG(M0_DEBUG, "FAILURE_VEC_REP mvp_pool="FID_F" "
		       "mvp_nr=%"PRIu64, FID_P(&data->u.hed_fvec_rep.mfp_pool),
		       data->u.hed_fvec_rep.mfp_nr);
		for (i = 0; i < data->u.hed_fvec_rep.mfp_nr; ++i) {
			M0_LOG(M0_DEBUG, "mvf_vec[%d]=(no_id="FID_F")", i,
			       FID_P(&data->u.hed_fvec_rep.mfp_vec.mfa_vec[i]));
		}
		return;
	case M0_HA_MSG_KEEPALIVE_REQ:
		M0_LOG(M0_DEBUG, "KEEPALIVE_REQ kaq_id="U128X_F,
		       U128_P(&data->u.hed_keepalive_req.kaq_id));
		return;
	case M0_HA_MSG_KEEPALIVE_REP:
		M0_LOG(M0_DEBUG, "KEEPALIVE_REP kap_id="U128X_F" "
		       "kap_counter=%"PRIu64,
		       U128_P(&data->u.hed_keepalive_rep.kap_id),
		       data->u.hed_keepalive_rep.kap_counter);
		return;
	case M0_HA_MSG_EVENT_PROCESS:
		M0_LOG(M0_DEBUG, "EVENT_PROCESS chp_event=%"PRIu64" "
		       "chp_type=%"PRIu64" chp_pid=%"PRIu64,
		       data->u.hed_event_process.chp_event,
		       data->u.hed_event_process.chp_type,
		       data->u.hed_event_process.chp_pid);
		return;
	case M0_HA_MSG_EVENT_SERVICE:
		M0_LOG(M0_DEBUG, "EVENT_SERVICE chs_event=%"PRIu64" "
		       "chs_type=%"PRIu64,
		       data->u.hed_event_service.chs_event,
		       data->u.hed_event_service.chs_type);
		return;
	case M0_HA_MSG_NR:
		M0_LOG(M0_WARN, "invalid M0_HA_MSG_NR type");
		return;
	}
	M0_LOG(M0_WARN, "unknown m0_ha_msg type %"PRIu64, data->hed_type);
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
