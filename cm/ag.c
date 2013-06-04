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
 * Original author: Subhash Arya  <subhash_arya@xyratex.com>
 *                  Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 20/09/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CM

#include "lib/string.h" /* memcpy */
#include "lib/trace.h"
#include "lib/cdefs.h"  /* M0_UNUSED */
#include "lib/misc.h"
#include "lib/errno.h"

#include "layout/layout.h"
#include "mero/magic.h"

#include "cm/proxy.h"
#include "cm/ag.h"
#include "cm/cm.h"

/**
   @addtogroup CMAG
 */

M0_TL_DESCR_DEFINE(aggr_grps_in, "aggregation groups incoming", M0_INTERNAL,
		   struct m0_cm_aggr_group, cag_cm_in_linkage, cag_magic,
		   CM_AG_LINK_MAGIX, CM_AG_HEAD_MAGIX);

M0_TL_DEFINE(aggr_grps_in, M0_INTERNAL, struct m0_cm_aggr_group);

M0_TL_DESCR_DEFINE(aggr_grps_out, "aggregation groups outgoing", M0_INTERNAL,
		   struct m0_cm_aggr_group, cag_cm_out_linkage, cag_magic,
		   CM_AG_LINK_MAGIX, CM_AG_HEAD_MAGIX);

M0_TL_DEFINE(aggr_grps_out, M0_INTERNAL, struct m0_cm_aggr_group);

struct m0_bob_type ag_bob;

M0_BOB_DEFINE(static M0_UNUSED, &ag_bob, m0_cm_aggr_group);

M0_INTERNAL void m0_cm_ag_lock(struct m0_cm_aggr_group *ag)
{
	m0_mutex_lock(&ag->cag_mutex);
}

M0_INTERNAL void m0_cm_ag_unlock(struct m0_cm_aggr_group *ag)
{
	m0_mutex_unlock(&ag->cag_mutex);
}

M0_INTERNAL int m0_cm_ag_id_cmp(const struct m0_cm_ag_id *id0,
				const struct m0_cm_ag_id *id1)
{
	M0_PRE(id0 != NULL);
	M0_PRE(id1 != NULL);

	return m0_uint128_cmp(&id0->ai_hi, &id1->ai_hi) ?:
	       m0_uint128_cmp(&id0->ai_lo, &id1->ai_lo);
}

M0_INTERNAL bool m0_cm_ag_id_is_set(const struct m0_cm_ag_id *id)
{
	struct m0_cm_ag_id id0;

	M0_SET0(&id0);

	return m0_cm_ag_id_cmp(id, &id0) != 0;
}

M0_INTERNAL void m0_cm_aggr_group_init(struct m0_cm_aggr_group *ag,
				       struct m0_cm *cm,
				       const struct m0_cm_ag_id *id,
				       bool has_incoming,
				       const struct m0_cm_aggr_group_ops
				       *ag_ops)
{
	M0_ENTRY();
	M0_PRE(id != NULL);
	M0_PRE(cm != NULL);
	M0_PRE(ag != NULL);
	M0_PRE(ag_ops != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	ag->cag_cm = cm;
	m0_mutex_init(&ag->cag_mutex);
	ag->cag_id = *id;
	ag->cag_has_incoming = has_incoming;
	aggr_grps_in_tlink_init(ag);
	aggr_grps_out_tlink_init(ag);
	ag->cag_ops = ag_ops;
	ag->cag_cp_local_nr = ag->cag_ops->cago_local_cp_nr(ag);
	M0_LEAVE();
}

M0_INTERNAL void m0_cm_aggr_group_fini(struct m0_cm_aggr_group *ag)
{
	struct m0_cm       *cm;

	M0_ENTRY();
	M0_ASSERT(ag != NULL);

	cm = ag->cag_cm;
	M0_ASSERT(m0_cm_is_locked(cm));
	if (aggr_grps_in_tlink_is_in(ag)) {
		aggr_grps_in_tlist_del(ag);
		M0_CNT_DEC(cm->cm_aggr_grps_in_nr);
	}
	aggr_grps_in_tlink_fini(ag);
	if (aggr_grps_out_tlink_is_in(ag)) {
		aggr_grps_out_tlist_del(ag);
		M0_CNT_DEC(cm->cm_aggr_grps_out_nr);
	}
	aggr_grps_out_tlink_fini(ag);
	M0_POST(!aggr_grps_in_tlink_is_in(ag) &&
		!aggr_grps_out_tlink_is_in(ag));
	m0_mutex_fini(&ag->cag_mutex);
	M0_LEAVE();
}

M0_INTERNAL void m0_cm_aggr_group_fini_and_progress(struct m0_cm_aggr_group *ag)
{
	struct m0_cm             *cm;
	struct m0_cm_ag_id        id;
	struct m0_cm_aggr_group  *hi;
	struct m0_cm_aggr_group  *lo;
	bool                      has_data;

	M0_ENTRY("ag: %p", ag);
	M0_ASSERT(ag != NULL);

	cm = ag->cag_cm;
	M0_ASSERT(m0_cm_is_locked(cm));
	id = ag->cag_id;
	hi = m0_cm_ag_hi(cm);
	lo = m0_cm_ag_lo(cm);

	M0_LOG(M0_DEBUG, "id [%lu] [%lu] [%lu] [%lu] [has_incoming = %d]",
	       id.ai_hi.u_hi, id.ai_hi.u_lo, id.ai_lo.u_hi, id.ai_lo.u_lo,
	       ag->cag_has_incoming);
	if (lo != NULL && hi != NULL) {
		M0_LOG(M0_DEBUG, "lo=%p [%lu] [%lu] [%lu] [%lu]", lo,
		       lo->cag_id.ai_hi.u_hi, lo->cag_id.ai_hi.u_lo,
		       lo->cag_id.ai_lo.u_hi, lo->cag_id.ai_lo.u_lo);

		M0_LOG(M0_DEBUG, "hi=%p [%lu] [%lu] [%lu] [%lu]", hi,
		       hi->cag_id.ai_hi.u_hi, hi->cag_id.ai_hi.u_lo,
		       hi->cag_id.ai_lo.u_hi, hi->cag_id.ai_lo.u_lo);
	}

	m0_cm_sw_update(cm);
	m0_cm_aggr_group_fini(ag);
	has_data = m0_cm_has_more_data(cm);
	if (!has_data && cm->cm_aggr_grps_in_nr == 0 &&
	    cm->cm_aggr_grps_out_nr == 0)
		cm->cm_ops->cmo_complete(cm);

	M0_LOG(M0_DEBUG, "in: [%lu] %p out: [%lu] %p",
	       cm->cm_aggr_grps_in_nr, &cm->cm_aggr_grps_in,
	       cm->cm_aggr_grps_out_nr, &cm->cm_aggr_grps_out);

	M0_LEAVE();
}

static struct m0_cm_aggr_group *
__aggr_group_locate(const struct m0_cm_ag_id *id,
		    const struct m0_tl_descr *descr,
		    struct m0_tl *head)
{
	struct m0_cm_aggr_group *ag;

	m0_tlist_for(descr, head, ag) {
		if (m0_cm_ag_id_cmp(id, &ag->cag_id) == 0) {
			M0_LEAVE("Found ag: %p", ag);
			return ag;
		}
	} m0_tlist_endfor;

	M0_LEAVE("ag not found");
	return NULL;
}

M0_INTERNAL struct m0_cm_aggr_group *
m0_cm_aggr_group_locate(struct m0_cm *cm, const struct m0_cm_ag_id *id,
			bool has_incoming)
{
	struct m0_cm_aggr_group *ag;

	M0_ENTRY("cm: %p", cm);
	M0_PRE(cm != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	M0_LOG(M0_DEBUG, "id [%lu] [%lu] [%lu] [%lu] \
	       has_incoming: [%d]", id->ai_hi.u_hi, id->ai_hi.u_lo,
	       id->ai_lo.u_hi, id->ai_lo.u_lo, has_incoming);
	ag = __aggr_group_locate(id, &aggr_grps_in_tl,
			&cm->cm_aggr_grps_in);
	if (ag != NULL)
		return ag;
	/*
	 * We did not find the aggregation group for the given aggregation group
	 * identifier in the m0_cm::cm_aggr_groups_in list. So now look into
	 * m0_cm::cm_aggr_groups_out list, there's a possibility that the
	 * aggregation group has out-coming copy packets and thus was created and
	 * added to m0_cm::cm_aggr_groups_out list earlier.
	 */
	ag = __aggr_group_locate(id, &aggr_grps_out_tl, &cm->cm_aggr_grps_out);

	/*
	 * The aggregation group we found is relevant and thus has incoming
	 * copy packets. But there are also local outgoing copy packets for
	 * this aggregation group. Thus even though it is already added to
	 * the m0_cm::cm_aggr_grps_out, it should also be added to m0_cm::
	 * cm_aggr_grps_in list.
	 */
	if (ag != NULL && has_incoming)
		m0_cm_aggr_group_add(cm, ag, true);
	return ag;
}

static void __aggr_group_add(struct m0_cm_aggr_group *ag,
			     const struct m0_tl_descr *descr,
			     struct m0_tl *head)
{
	struct m0_cm_aggr_group *found;
	int                      val;

	m0_tlist_for(descr, head, found) {
		val = m0_cm_ag_id_cmp(&ag->cag_id, &found->cag_id);
		M0_ASSERT(val != 0);
		if (val < 0) {
			m0_tlist_add_before(descr, found, ag);
			M0_LEAVE();
			return;
		}
	} m0_tl_endfor;
	m0_tlist_add_tail(descr, head, ag);
}

M0_INTERNAL void m0_cm_aggr_group_add(struct m0_cm *cm,
				      struct m0_cm_aggr_group *ag,
				      bool has_incoming)
{
	struct m0_cm_ag_id id = ag->cag_id;

	M0_ENTRY("cm: %p, ag: %p", cm, ag);
	M0_PRE(cm != NULL);
	M0_PRE(ag != NULL);
	M0_PRE(m0_cm_is_locked(cm));
	M0_LEAVE();

	M0_LOG(M0_DEBUG, "id [%lu] [%lu] [%lu] [%lu] \
	       has_incoming: [%d]", id.ai_hi.u_hi, id.ai_hi.u_lo,
	       id.ai_lo.u_hi, id.ai_lo.u_lo, has_incoming);
	if (has_incoming) {
		__aggr_group_add(ag, &aggr_grps_in_tl, &cm->cm_aggr_grps_in);
		M0_CNT_INC(cm->cm_aggr_grps_in_nr);
	} else {
		__aggr_group_add(ag, &aggr_grps_out_tl, &cm->cm_aggr_grps_out);
		M0_CNT_INC(cm->cm_aggr_grps_out_nr);
	}
}

M0_INTERNAL int m0_cm_aggr_group_alloc(struct m0_cm *cm,
				       const struct m0_cm_ag_id *id,
				       bool has_incoming,
				       struct m0_cm_aggr_group **out)
{
	int                      rc;

	M0_ENTRY("cm: %p", cm);
	M0_PRE(cm != NULL && id != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	M0_LOG(M0_DEBUG, "id [%lu] [%lu] [%lu] [%lu] \
	       has_incoming:[%d]", id->ai_hi.u_hi, id->ai_hi.u_lo,
	       id->ai_lo.u_hi, id->ai_lo.u_lo, has_incoming);
	M0_LOG(M0_DEBUG, "last_saved_id [%lu] [%lu] [%lu] [%lu]",
		cm->cm_last_saved_sw_hi.ai_hi.u_hi,
		cm->cm_last_saved_sw_hi.ai_hi.u_lo,
		cm->cm_last_saved_sw_hi.ai_lo.u_hi,
		cm->cm_last_saved_sw_hi.ai_lo.u_lo);

	rc = cm->cm_ops->cmo_ag_alloc(cm, id, has_incoming, out);
	if (rc == 0 || rc == -ENOBUFS)
		m0_cm_aggr_group_add(cm, *out, has_incoming);

	/*
	 * Save the HI incoming aggregation group identifier.
	 * This is used mainly during sliding window update to advance the
	 * window starting from the highest processed incoming aggregation
	 * group identifier.
	 */
	if (has_incoming && m0_cm_ag_id_cmp(&cm->cm_last_saved_sw_hi, id) < 0)
		cm->cm_last_saved_sw_hi = *id;
	return rc;
}

M0_INTERNAL bool m0_cm_aggr_group_tlists_are_empty(struct m0_cm *cm)
{
	return aggr_grps_in_tlist_is_empty(&cm->cm_aggr_grps_in) &&
	       aggr_grps_out_tlist_is_empty(&cm->cm_aggr_grps_out);
}

M0_INTERNAL int m0_cm_ag_advance(struct m0_cm *cm)
{
	int                      rc;
	struct m0_cm_ag_id       next;
	struct m0_cm_ag_id       id;
	struct m0_cm_aggr_group *ag;

	M0_PRE(m0_cm_is_locked(cm));

	M0_SET0(&id);
	M0_SET0(&next);
	id = cm->cm_last_saved_sw_hi;
	do {
		M0_LOG(M0_DEBUG, "id [%lu] [%lu] [%lu] [%lu]",
		       id.ai_hi.u_hi, id.ai_hi.u_lo,
			id.ai_lo.u_hi, id.ai_lo.u_lo);
		rc = cm->cm_ops->cmo_ag_next(cm, id, &next);
		if (rc == 0 && m0_cm_ag_id_is_set(&next)) {
			ag = m0_cm_aggr_group_locate(cm, &next, true);
			if (ag == NULL) {
				rc = m0_cm_aggr_group_alloc(cm, &next,
							    true, &ag);
				if (rc != 0)
					break;
			}
			id = next;
			M0_SET0(&next);
		}
	} while (rc == 0);

	if (rc == -ENOSPC || rc == -ENOENT)
		rc = 0;

	return rc;
}

M0_INTERNAL struct m0_cm_aggr_group *m0_cm_ag_hi(struct m0_cm *cm)
{

	M0_PRE(cm != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	return aggr_grps_in_tlist_tail(&cm->cm_aggr_grps_in);
}

M0_INTERNAL struct m0_cm_aggr_group *m0_cm_ag_lo(struct m0_cm *cm)
{
	M0_PRE(cm != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	return aggr_grps_in_tlist_head(&cm->cm_aggr_grps_in);
}

/** @} CMAG */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
