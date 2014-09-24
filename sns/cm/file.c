/* -*- C -*- */
/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Subhash Arya <subhash_arya@xyratex.com>
 *                  Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 12/09/2014
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/trace.h"
#include "lib/finject.h"

#include "sns/cm/cm.h"
#include "sns/cm/cm_utils.h"
#include "sns/cm/file.h"

/**
   @addtogroup SNSCMFILE

   @{
 */

const struct m0_uint128 m0_rm_sns_cm_group = M0_UINT128(0, 2);
extern const struct m0_rm_resource_type_ops file_lock_type_ops;

static uint64_t sns_cm_fctx_hash_func(const struct m0_htable *htable,
				      const void *k)
{
	const struct m0_fid *fid = (struct m0_fid *)k;
	const uint64_t       key = fid->f_key;

	return key % htable->h_bucket_nr;
}

static bool sns_cm_fctx_key_eq(const void *key1, const void *key2)
{
	return m0_fid_eq((struct m0_fid *)key1, (struct m0_fid *)key2);
}

M0_HT_DESCR_DEFINE(m0_scmfctx, "Hash of files used by sns cm", M0_INTERNAL,
		   struct m0_sns_cm_file_ctx, sf_sc_link, sf_magic,
		   M0_SNS_CM_FILE_CTX_MAGIC, M0_SNS_CM_MAGIC,
		   sf_fid, sns_cm_fctx_hash_func,
		   sns_cm_fctx_key_eq);

M0_HT_DEFINE(m0_scmfctx, M0_INTERNAL, struct m0_sns_cm_file_ctx,
	     struct m0_fid);

static void __sns_cm_fctx_cleanup(struct m0_sns_cm_file_ctx *fctx)
{
	m0_file_unlock(&fctx->sf_rin);
	if (fctx->sf_layout != NULL)
		m0_layout_put(fctx->sf_layout);
	m0_scmfctx_htable_del(&fctx->sf_scm->sc_file_ctx, fctx);
	m0_sns_cm_fctx_fini(fctx);
	m0_free(fctx);
}

M0_INTERNAL void m0_sns_cm_fctx_cleanup(struct m0_sns_cm *scm)
{
	struct m0_sns_cm_file_ctx *fctx;

	m0_mutex_lock(&scm->sc_file_ctx_mutex);
	m0_htable_for(m0_scmfctx, fctx, &scm->sc_file_ctx) {
		__sns_cm_fctx_cleanup(fctx);
	} m0_htable_endfor;
	m0_scmfctx_htable_fini(&scm->sc_file_ctx);
	m0_mutex_unlock(&scm->sc_file_ctx_mutex);
}

static void sns_cm_fctx_release(struct m0_ref *ref)
{
	struct m0_sns_cm_file_ctx *fctx;

	M0_PRE(ref != NULL);

	fctx = container_of(ref, struct m0_sns_cm_file_ctx, sf_ref);
	__sns_cm_fctx_cleanup(fctx);
}

M0_INTERNAL void m0_sns_cm_flock_resource_set(struct m0_sns_cm *scm)
{
        scm->sc_rm_ctx.rc_rt.rt_id = M0_RM_FLOCK_RT;
        scm->sc_rm_ctx.rc_rt.rt_ops = &file_lock_type_ops;
}

M0_INTERNAL int m0_sns_cm_fctx_init(struct m0_sns_cm *scm, struct m0_fid *fid,
				    struct m0_sns_cm_file_ctx **sc_fctx)
{
	struct m0_sns_cm_file_ctx *fctx;

	M0_ENTRY();
	M0_PRE(sc_fctx != NULL || scm != NULL || fid!= NULL);
	M0_PRE(!m0_fid_eq(fid, &M0_COB_ROOT_FID) ||
	       !m0_fid_eq(fid, &M0_COB_ROOT_FID) ||
	       !m0_fid_eq(fid, &M0_MDSERVICE_SLASH_FID));
	M0_PRE(m0_mutex_is_locked(&scm->sc_file_ctx_mutex));
	M0_PRE(m0_sns_cm_fid_is_valid(fid));

	M0_ALLOC_PTR(fctx);
	if (fctx == NULL)
		return -ENOMEM;

	m0_fid_set(&fctx->sf_fid, fid->f_container, fid->f_key);
	m0_file_init(&fctx->sf_file, &fctx->sf_fid, &scm->sc_rm_ctx.rc_dom, M0_DI_NONE);
	m0_rm_remote_init(&fctx->sf_creditor, &fctx->sf_file.fi_res);
	m0_file_owner_init(&fctx->sf_owner, &m0_rm_sns_cm_group, &fctx->sf_file,
			   NULL);
	m0_rm_incoming_init(&fctx->sf_rin, &fctx->sf_owner, M0_RIT_LOCAL,
			    RIP_NONE,
			    RIF_MAY_BORROW | RIF_MAY_REVOKE);

	fctx->sf_owner.ro_creditor = &fctx->sf_creditor;
	fctx->sf_creditor.rem_session = &scm->sc_rm_ctx.rc_session;
	fctx->sf_creditor.rem_cookie = M0_COOKIE_NULL;
	m0_scmfctx_tlink_init(fctx);
	m0_ref_init(&fctx->sf_ref, 0, sns_cm_fctx_release);
	fctx->sf_scm = scm;
	*sc_fctx = fctx;

	return M0_RC(0);
}

M0_INTERNAL void m0_sns_cm_fctx_fini(struct m0_sns_cm_file_ctx *fctx)
{
	int rc;

	M0_PRE(fctx != NULL);

	m0_scmfctx_tlink_fini(fctx);
	m0_rm_owner_windup(&fctx->sf_owner);
	rc = m0_rm_owner_timedwait(&fctx->sf_owner, M0_BITS(ROS_FINAL),
				   M0_TIME_NEVER);
	M0_ASSERT(rc == 0);
	m0_file_owner_fini(&fctx->sf_owner);
	m0_rm_remote_fini(&fctx->sf_creditor);
	m0_file_fini(&fctx->sf_file);
}

extern const struct m0_rm_incoming_ops file_lock_incoming_ops;
static int __sns_cm_file_lock(struct m0_sns_cm_file_ctx *fctx)
{
	struct m0_sns_cm *scm;
	M0_ENTRY();

	M0_PRE(fctx != NULL);
	M0_PRE(m0_sns_cm_fid_is_valid(&fctx->sf_fid));
	M0_PRE(fctx->sf_owner.ro_resource == &fctx->sf_file.fi_res);

	scm = fctx->sf_scm;
	M0_ASSERT(scm != NULL);
	M0_ASSERT(m0_sns_cm_fctx_locate(scm, &fctx->sf_fid) == NULL);
	M0_ASSERT(m0_mutex_is_locked(&scm->sc_file_ctx_mutex));

	M0_LOG(M0_DEBUG, "Lock file for FID : "FID_F, FID_P(&fctx->sf_fid));
	fctx->sf_flock_status = M0_SCM_FILE_LOCK_WAIT;
	m0_scmfctx_htable_add(&scm->sc_file_ctx, fctx);
	/* XXX: Ideally m0_file_lock should be called, but
	 * it internally calls m0_rm_incoming_init(). This has already been
	 * called here in m0_sns_cm_file_lock_init().
	 * m0_file_lock(&fctx->sf_owner, &fctx->sf_rin);
	 */
	fctx->sf_rin.rin_want.cr_datum    = RM_FILE_LOCK;
	fctx->sf_rin.rin_want.cr_group_id = m0_rm_sns_cm_group;
	fctx->sf_rin.rin_ops              = &file_lock_incoming_ops;

	m0_rm_credit_get(&fctx->sf_rin);

	M0_POST(m0_sns_cm_fctx_locate(scm, &fctx->sf_fid) == fctx);
	return M0_RC(-EAGAIN);
}

static void __sns_cm_file_unlock(struct m0_sns_cm_file_ctx *fctx)
{
	M0_PRE(fctx != NULL);
	M0_PRE(m0_sns_cm_fid_is_valid(&fctx->sf_fid));
	M0_PRE(fctx->sf_owner.ro_resource == &fctx->sf_file.fi_res);
	M0_PRE(m0_mutex_is_locked(&fctx->sf_scm->sc_file_ctx_mutex));

	m0_ref_put(&fctx->sf_ref);
	M0_LOG(M0_DEBUG, "Unlock file for FID : "FID_F, FID_P(&fctx->sf_fid));
}

M0_INTERNAL int m0_sns_cm_file_lock_wait(struct m0_sns_cm_file_ctx *fctx,
					 struct m0_fom *fom)
{
	struct m0_chan *rm_chan;
	uint32_t        state;

	M0_ENTRY();

	M0_PRE(fctx != NULL || fctx->sf_scm != NULL || fom != NULL);
	M0_PRE(m0_mutex_is_locked(&fctx->sf_scm->sc_file_ctx_mutex));
	M0_PRE(m0_sns_cm_fid_is_valid(&fctx->sf_fid));
	M0_PRE(m0_sns_cm_fctx_locate(fctx->sf_scm, &fctx->sf_fid) != NULL);

	m0_rm_owner_lock(&fctx->sf_owner);
	state = fctx->sf_rin.rin_sm.sm_state;
	if (state == RI_SUCCESS ||  state == RI_FAILURE) {
		m0_rm_owner_unlock(&fctx->sf_owner);
		if (state == RI_FAILURE)
			return M0_ERR(-EFAULT, "Failed to acquire file lock");
		else {
			fctx->sf_flock_status = M0_SCM_FILE_LOCKED;
			m0_ref_get(&fctx->sf_ref);
			return M0_RC(0);
		}
	}
	rm_chan = &fctx->sf_rin.rin_sm.sm_chan;
	m0_fom_wait_on(fom, rm_chan, &fom->fo_cb);
	m0_rm_owner_unlock(&fctx->sf_owner);
	return M0_RC(-EAGAIN);
}

M0_INTERNAL int m0_sns_cm_file_lock(struct m0_sns_cm *scm, struct m0_fid *fid,
				    struct m0_sns_cm_file_ctx **out)
{
	struct m0_sns_cm_file_ctx *fctx;
	int                        rc;

	M0_ENTRY();

	M0_PRE(scm != NULL || fid != NULL);
	M0_PRE(m0_sns_cm_fid_is_valid(fid));
	M0_PRE(m0_mutex_is_locked(&scm->sc_file_ctx_mutex));

	fctx = m0_scmfctx_htable_lookup(&scm->sc_file_ctx, fid);
	if ( fctx != NULL) {
		*out = fctx;
		M0_LOG(M0_DEBUG, "fid: "FID_F, FID_P(fid));
		if (fctx->sf_flock_status == M0_SCM_FILE_LOCKED) {
			m0_ref_get(&fctx->sf_ref);
			return M0_RC(0);
		}
		if (fctx->sf_flock_status == M0_SCM_FILE_LOCK_WAIT)
			return M0_RC(-EAGAIN);
	}
	rc = m0_sns_cm_fctx_init(scm, fid, &fctx);
	if (rc == 0) {
		rc = __sns_cm_file_lock(fctx);
		*out = fctx;
	}
	return M0_RC(rc);
}

M0_INTERNAL struct m0_sns_cm_file_ctx
*m0_sns_cm_fctx_locate(struct m0_sns_cm *scm, struct m0_fid *fid)
{
	struct m0_sns_cm_file_ctx *fctx;

	M0_ENTRY("sns cm %p, fid %p="FID_F, scm, fid, FID_P(fid));
	M0_PRE(scm != NULL && fid != NULL);
	M0_PRE(m0_mutex_is_locked(&scm->sc_file_ctx_mutex));

	fctx = m0_scmfctx_htable_lookup(&scm->sc_file_ctx, fid);
	M0_ASSERT(ergo(fctx != NULL, m0_fid_cmp(fid, &fctx->sf_fid) == 0));

	M0_LEAVE();
	return fctx;
}

M0_INTERNAL struct m0_sns_cm_file_ctx *
m0_sns_cm_fctx_get(struct m0_sns_cm *scm, const struct m0_cm_ag_id *id)
{
	struct m0_fid             *fid;
	struct m0_sns_cm_file_ctx *fctx;

	if (M0_FI_ENABLED("do_nothing"))
		return NULL;

	M0_PRE(scm != NULL && id != NULL);

	m0_mutex_lock(&scm->sc_file_ctx_mutex);
	fid = (struct m0_fid *)&id->ai_hi;
	M0_ASSERT(m0_sns_cm_fid_is_valid(fid));
	fctx = m0_sns_cm_fctx_locate(scm, fid);
	M0_ASSERT(fctx != NULL);
	M0_CNT_INC(fctx->sf_ag_nr);
	M0_LOG(M0_DEBUG, "ag nr: %lu, FID :"FID_F, fctx->sf_ag_nr,
			FID_P(fid));
	m0_ref_get(&fctx->sf_ref);
	m0_mutex_unlock(&scm->sc_file_ctx_mutex);

	return fctx;
}

M0_INTERNAL void m0_sns_cm_fctx_put(struct m0_sns_cm *scm,
                                    const struct m0_cm_ag_id *id)
{
	struct m0_fid              *fid;
	struct m0_sns_cm_file_ctx  *fctx;

	if (M0_FI_ENABLED("do_nothing"))
		return;

	M0_PRE(scm != NULL && id != NULL);

	fid = (struct m0_fid *)&id->ai_hi;
	m0_mutex_lock(&scm->sc_file_ctx_mutex);
	M0_ASSERT(m0_sns_cm_fid_is_valid(fid));
	fctx = m0_sns_cm_fctx_locate(scm, fid);
	M0_ASSERT(fctx != NULL);
	M0_CNT_DEC(fctx->sf_ag_nr);
	M0_LOG(M0_DEBUG, "ag nr: %lu, FID : <%lx : %lx>", fctx->sf_ag_nr,
			FID_P(fid));
	m0_ref_put(&fctx->sf_ref);
	m0_mutex_unlock(&scm->sc_file_ctx_mutex);
}

M0_INTERNAL void m0_sns_cm_file_unlock(struct m0_sns_cm *scm,
                                      struct m0_fid *fid)
{
	struct m0_sns_cm_file_ctx *fctx;

	if (M0_FI_ENABLED("do_nothing"))
		return;

	M0_PRE(scm != NULL && fid != NULL);
	M0_PRE(m0_sns_cm_fid_is_valid(fid));
	M0_PRE(m0_mutex_is_locked(&scm->sc_file_ctx_mutex));

	fctx = m0_sns_cm_fctx_locate(scm, fid);
	M0_ASSERT(fctx != NULL);
	M0_LOG(M0_DEBUG, "File with FID : "FID_F" has %lu AGs",
			FID_P(&fctx->sf_fid), fctx->sf_ag_nr);
	__sns_cm_file_unlock(fctx);
}

/** @} SNSCMFILE */
#undef M0_TRACE_SUBSYSTEM
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
