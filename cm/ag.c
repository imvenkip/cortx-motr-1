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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CM
#include "lib/trace.h"

#include "lib/string.h" /* memcpy */
#include "lib/misc.h"
#include "lib/errno.h"

#include "mero/magic.h"

#include "cm/sw.h"
#include "cm/proxy.h"
#include "cm/ag.h"
#include "cm/cm.h"
#include "cm/cp.h"

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

M0_INTERNAL void m0_cm_ag_is_locked(struct m0_cm_aggr_group *ag)
{
	m0_mutex_is_locked(&ag->cag_mutex);
}

M0_INTERNAL int m0_cm_ag_id_cmp(const struct m0_cm_ag_id *id0,
				const struct m0_cm_ag_id *id1)
{
	M0_PRE(id0 != NULL);
	M0_PRE(id1 != NULL);

	return m0_uint128_cmp(&id0->ai_hi, &id1->ai_hi) ?:
	       m0_uint128_cmp(&id0->ai_lo, &id1->ai_lo);
}

M0_INTERNAL void m0_cm_ag_id_copy(struct m0_cm_ag_id *dst,
                                  const struct m0_cm_ag_id *src)
{
	M0_PRE(dst != NULL);
	M0_PRE(src != NULL);

	dst->ai_hi.u_hi = src->ai_hi.u_hi;
	dst->ai_hi.u_lo = src->ai_hi.u_lo;
	dst->ai_lo.u_hi = src->ai_lo.u_hi;
	dst->ai_lo.u_lo = src->ai_lo.u_lo;
}

M0_INTERNAL bool m0_cm_ag_id_is_set(const struct m0_cm_ag_id *id)
{
	struct m0_cm_ag_id id0;

	M0_SET0(&id0);

	return m0_cm_ag_id_cmp(id, &id0) != 0;
}

static void _fini_ast_cb(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct m0_cm_aggr_group *ag = M0_AMB(ag, ast, cag_fini_ast);
	struct m0_cm            *cm = ag->cag_cm;
	struct m0_cm_aggr_group *in_lo;
	struct m0_cm_aggr_group *out_lo;
	struct m0_cm_ag_id       in_id;
	struct m0_cm_ag_id       out_id;
	bool                     has_incoming;

	has_incoming = ag->cag_has_incoming;
	M0_SET0(&in_id);
	M0_SET0(&out_id);
	/*
	 * Update m0_cm::cm_store to persist lowest aggregation group in
	 * sliding window and out going groups.
	 */
	if (has_incoming) {
		in_lo = aggr_grps_in_tlist_head(&cm->cm_aggr_grps_in);
		if (in_lo != NULL) {
			if (m0_cm_ag_id_cmp(&in_lo->cag_id, &ag->cag_id) == 0)
				in_id = ag->cag_id;
		}
	} else {
		out_lo = aggr_grps_out_tlist_head(&cm->cm_aggr_grps_out);
		if (out_lo != NULL) {
			if (m0_cm_ag_id_cmp(&out_lo->cag_id, &ag->cag_id) == 0)
				out_id = ag->cag_id;
		}
	}

	/* updating in-memory ag id and cm_epoch */
	if (has_incoming && m0_cm_ag_id_is_set(&in_id)) {
		cm->cm_ag_store.s_data.d_in = in_id;
	} else if (m0_cm_ag_id_is_set(&out_id)) {
		cm->cm_ag_store.s_data.d_out = out_id;
	}
	cm->cm_ag_store.s_data.d_cm_epoch = cm->cm_epoch;
	ag->cag_ops->cago_fini(ag);
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
	ag->cag_fini_ast.sa_cb = _fini_ast_cb;
	M0_LEAVE();
}

M0_INTERNAL void m0_cm_aggr_group_fini(struct m0_cm_aggr_group *ag)
{
	struct m0_cm *cm;

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

	M0_ENTRY("ag: %p", ag);
	M0_ASSERT(ag != NULL);

	cm = ag->cag_cm;
	M0_ASSERT(m0_cm_is_locked(cm));
	id = ag->cag_id;
	hi = m0_cm_ag_hi(cm);
	lo = m0_cm_ag_lo(cm);

	ID_INCOMING_LOG("id", &id, ag->cag_has_incoming);
	if (lo != NULL && hi != NULL) {
		ID_LOG("lo", &lo->cag_id);
		ID_LOG("hi", &hi->cag_id);
	}
	m0_cm_aggr_group_fini(ag);

	M0_LOG(M0_DEBUG, "%lu: ["M0_AG_F"] in=[%lu] %p out=[%lu] %p ",
	       cm->cm_id, M0_AG_P(&id), cm->cm_aggr_grps_in_nr, &cm->cm_aggr_grps_in,
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

	ID_INCOMING_LOG("id", id, has_incoming);
	if (has_incoming) {
		ag = __aggr_group_locate(id, &aggr_grps_in_tl,
					 &cm->cm_aggr_grps_in);
	} else {
		ag = __aggr_group_locate(id, &aggr_grps_out_tl,
					 &cm->cm_aggr_grps_out);
		//if (ag != NULL && has_incoming)
		//	m0_cm_aggr_group_add(cm, ag, true);
	}
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

	ID_INCOMING_LOG("id", &id, has_incoming);
	if (has_incoming) {
		__aggr_group_add(ag, &aggr_grps_in_tl, &cm->cm_aggr_grps_in);
		M0_CNT_INC(cm->cm_aggr_grps_in_nr);
		if (m0_cm_ag_id_cmp(&cm->cm_sw_last_updated_hi, &id) < 0)
			cm->cm_sw_last_updated_hi = id;
	} else {
		__aggr_group_add(ag, &aggr_grps_out_tl, &cm->cm_aggr_grps_out);
		M0_CNT_INC(cm->cm_aggr_grps_out_nr);
		if (m0_cm_ag_id_cmp(&cm->cm_last_out_hi, &id) < 0)
			cm->cm_last_out_hi = id;
	}

	M0_LEAVE();
}

M0_INTERNAL int m0_cm_aggr_group_alloc(struct m0_cm *cm,
				       const struct m0_cm_ag_id *id,
				       bool has_incoming,
				       struct m0_cm_aggr_group **out)
{
	int rc;

	M0_ENTRY("cm: %p", cm);
	M0_PRE(cm != NULL && id != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	ID_INCOMING_LOG("id", id, has_incoming);
	ID_LOG("last_saved_id", &cm->cm_sw_last_updated_hi);

	rc = cm->cm_ops->cmo_ag_alloc(cm, id, has_incoming, out);
	M0_ASSERT(rc <= 0);
	if (rc == 0 || rc == -ENOBUFS)
		m0_cm_aggr_group_add(cm, *out, has_incoming);
	else if (rc != 0)
		return M0_RC(rc);

	M0_ASSERT(rc <= 0);
	return M0_RC(rc);
}

M0_INTERNAL bool m0_cm_aggr_group_tlists_are_empty(struct m0_cm *cm)
{
	 return cm->cm_aggr_grps_in_nr == 0 && cm->cm_aggr_grps_out_nr == 0;
}

M0_INTERNAL int m0_cm_ag_advance(struct m0_cm *cm)
{
	struct m0_cm_ag_id next;
	struct m0_cm_ag_id id;
	int                rc;

	M0_ENTRY();

	M0_PRE(m0_cm_is_locked(cm));

	M0_SET0(&id);
	M0_SET0(&next);
	id = cm->cm_sw_last_updated_hi;
	do {
		ID_LOG("id", &id);
		rc = cm->cm_ops->cmo_ag_next(cm, &id, &next);
		M0_LOG(M0_DEBUG,  "next ["M0_AG_F"] rc=%d", M0_AG_P(&next), rc);
		if (rc == 0 && m0_cm_ag_id_is_set(&next)) {
			id = next;
			M0_SET0(&next);
		}
	} while (rc == 0);

	return M0_RC(rc);
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


M0_INTERNAL void m0_cm_ag_cp_add(struct m0_cm_aggr_group *ag, struct m0_cm_cp *cp)
{
	M0_PRE(ag != NULL);
	M0_PRE(cp != NULL);

	m0_cm_ag_lock(ag);
	cp->c_ag = ag;
	M0_CNT_INC(ag->cag_nr_cps);
	m0_cm_ag_unlock(ag);
}

M0_INTERNAL void m0_cm_ag_cp_del(struct m0_cm_aggr_group *ag, struct m0_cm_cp *cp)
{
	M0_PRE(ag != NULL);
	M0_PRE(cp != NULL);

	m0_cm_ag_lock(ag);
	cp->c_ag = NULL;
	M0_CNT_DEC(ag->cag_nr_cps);
	m0_cm_ag_unlock(ag);
}

M0_INTERNAL bool m0_cm_ag_has_pending_cps(struct m0_cm_aggr_group *ag)
{
	uint32_t nr_cps;

	M0_PRE(ag != NULL);

	m0_cm_ag_lock(ag);
	nr_cps = ag->cag_nr_cps;
	m0_cm_ag_unlock(ag);

	return nr_cps > 0;
}

M0_INTERNAL void m0_cm_ag_fini_post(struct m0_cm_aggr_group *ag)
{
	if (ag->cag_fini_ast.sa_next == NULL)
		m0_sm_ast_post(&ag->cag_cm->cm_sm_group, &ag->cag_fini_ast);
}

/** @} CMAG */
#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
