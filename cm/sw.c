/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 06/07/2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CM

#include "lib/trace.h"
#include "lib/time.h"
#include "lib/misc.h"
#include "lib/memory.h"

#include "fop/fop.h"
#include "rpc/rpc.h"
#include "mero/setup.h" /* CS_MAX_EP_ADDR_LEN */

#include "cm/proxy.h"
#include "cm/sw.h"
#include "cm/cm.h"

/**
   @addtogroup CMSW

   @{
 */

M0_INTERNAL void m0_cm_sw_set(struct m0_cm_sw *dst,
			      const struct m0_cm_ag_id *lo,
			      const struct m0_cm_ag_id *hi)
{
	M0_PRE(dst != NULL && lo != NULL && hi != NULL);

	m0_cm_ag_id_copy(&dst->sw_lo, lo);
	m0_cm_ag_id_copy(&dst->sw_hi, hi);
}

M0_INTERNAL void m0_cm_sw_copy(struct m0_cm_sw *dst,
			       const struct m0_cm_sw *src)
{
	M0_PRE(dst != NULL && src != NULL);

	m0_cm_ag_id_copy(&dst->sw_lo, &src->sw_lo);
	m0_cm_ag_id_copy(&dst->sw_hi, &src->sw_hi);
}

M0_INTERNAL int m0_cm_sw_onwire_init(struct m0_cm_sw_onwire *sw_onwire,
                                     const char *ep, const struct m0_cm_sw *sw)
{
	M0_PRE(sw_onwire != NULL && ep != NULL && sw != NULL);

	m0_cm_sw_copy(&sw_onwire->swo_sw, sw);
	sw_onwire->swo_cm_ep.ep_size = CS_MAX_EP_ADDR_LEN;
	M0_ALLOC_ARR(sw_onwire->swo_cm_ep.ep, CS_MAX_EP_ADDR_LEN);
	if (sw_onwire->swo_cm_ep.ep == NULL )
		return -ENOMEM;
	strncpy(sw_onwire->swo_cm_ep.ep, ep, CS_MAX_EP_ADDR_LEN);

	return 0;
}

M0_INTERNAL int m0_cm_sw_local_update(struct m0_cm *cm)
{
        int rc = 0;

        M0_ENTRY("cm: %p", cm);
        M0_PRE(cm != NULL);
        M0_PRE(m0_cm_is_locked(cm));

        //if (m0_cm_has_more_data(cm)) {
                if (cm->cm_proxy_nr > 0) {
			if (m0_cm_is_active(cm) && !m0_cm_ag_id_is_set(&cm->cm_last_saved_sw_hi))
				return rc;
                        rc = m0_cm_ag_advance(cm);
		}
        //}

        M0_LEAVE("rc: %d", rc);
        return rc;
}

M0_INTERNAL int m0_cm_sw_remote_update(struct m0_cm *cm)
{
	struct m0_cm_proxy      *pxy;
	struct m0_cm_sw          sw;
	struct m0_cm_aggr_group *lo;
        struct m0_cm_ag_id       id_lo;
        struct m0_cm_ag_id       id_hi;
	int                      rc = 0;

	M0_PRE(cm != NULL);
	M0_PRE(m0_cm_is_locked(cm));

        M0_SET0(&id_lo);
        M0_SET0(&id_hi);
        lo = m0_cm_ag_lo(cm);
	/*
	 * lo can be NULL mainly if the copy machine operation is just started
	 * and sliding window is empty.
	 */
        if (lo != NULL) {
                id_lo = lo->cag_id;
                id_hi = cm->cm_last_saved_sw_hi;
        }
	m0_cm_sw_set(&sw, &id_lo, &id_hi);
	m0_tl_for(proxy, &cm->cm_proxies, pxy) {
		M0_LOG(M0_FATAL, "proxy last updated  hi: [%lu] [%lu] [%lu] [%lu]",
			pxy->px_last_sw_onwire_sent.sw_hi.ai_hi.u_hi,
			pxy->px_last_sw_onwire_sent.sw_hi.ai_hi.u_lo,
			pxy->px_last_sw_onwire_sent.sw_hi.ai_lo.u_hi,
			pxy->px_last_sw_onwire_sent.sw_hi.ai_lo.u_lo);
		if (m0_cm_ag_id_cmp(&id_hi,
				    &pxy->px_last_sw_onwire_sent.sw_hi) >= 0) {
			rc = m0_cm_proxy_remote_update(pxy, &sw);
			if (rc != 0)
				break;
		}
	} m0_tl_endfor;

	return rc;
}

#undef M0_TRACE_SUBSYSTEM

/** @} CMSW */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
