/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 10/09/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "mero/setup.h"

#include "sns/sns_addb.h"
#include "sns/cm/ag.h"
#include "sns/cm/cp.h"

/**
 * @addtogroup SNSCMCP
 * @{
 */

static int indexvec_prepare(struct m0_indexvec *iv, m0_bindex_t idx,
			    uint32_t seg_nr, m0_bcount_t seg_size,
			    struct m0_addb_ctx *ctx,
			    uint32_t bshift)
{
	int rc;
	int i;

	M0_PRE(iv != NULL);

	rc = m0_indexvec_alloc(iv, seg_nr, ctx, M0_ADDB_CTXID_SNS_REPAIR_SERV);

	for (i = 0; i < seg_nr; ++i) {
		iv->iv_vec.v_count[i] = seg_size >> bshift;
		iv->iv_index[i] = idx >> bshift;
		idx += seg_size;
	}

	return 0;
}

static void indexvec_free(struct m0_indexvec *iv)
{
	m0_free(iv->iv_vec.v_count);
	m0_free(iv->iv_index);
}

static int bufvec_prepare(struct m0_bufvec *obuf, struct m0_bufvec *ibuf,
			  uint32_t seg_nr, m0_bcount_t seg_size,
			  uint32_t bshift)
{
	int i;

	M0_PRE(obuf != NULL);
	M0_PRE(ibuf != NULL);

	obuf->ov_vec.v_nr = seg_nr;
	M0_ALLOC_ARR(obuf->ov_vec.v_count, seg_nr);
	if (obuf->ov_vec.v_count == NULL)
		return -ENOMEM;

	M0_ALLOC_ARR(obuf->ov_buf, seg_nr);
	if (obuf->ov_buf == NULL) {
		m0_free(obuf->ov_vec.v_count);
		return -ENOMEM;
	}

	for (i = 0; i < seg_nr; ++i) {
		obuf->ov_vec.v_count[i] = seg_size >> bshift;
		obuf->ov_buf[i] = m0_stob_addr_pack(ibuf->ov_buf[i], bshift);
	}

	return 0;
}

static void bufvec_free(struct m0_bufvec *bv)
{
	m0_free(bv->ov_vec.v_count);
	m0_free(bv->ov_buf);
}

static int cp_io(struct m0_cm_cp *cp, const enum m0_stob_io_opcode op)
{
	struct m0_fom           *cp_fom;
	struct m0_reqh          *reqh;
	struct m0_stob_domain   *dom;
	struct m0_sns_cm_cp     *sns_cp;
	struct m0_stob          *stob;
	struct m0_stob_id       *stobid;
	struct m0_stob_io       *stio;
	struct m0_addb_ctx      *addb_ctx;
	uint32_t                 bshift;
	int                      rc;

	addb_ctx = &cp->c_ag->cag_cm->cm_service.rs_addb_ctx;
	sns_cp = cp2snscp(cp);
	cp_fom = &cp->c_fom;
	reqh = m0_fom_reqh(cp_fom);
	stobid = &sns_cp->sc_sid;
	stio = &sns_cp->sc_stio;
	dom = m0_cs_stob_domain_find(reqh, stobid);

	if (dom == NULL) {
		rc = -EINVAL;
		goto out;
	}

	rc = m0_stob_find(dom, stobid, &sns_cp->sc_stob);
	if (rc != 0)
		goto out;

	stob = sns_cp->sc_stob;
	m0_dtx_init(&cp_fom->fo_tx);
	rc = dom->sd_ops->sdo_tx_make(dom, &cp_fom->fo_tx);
	if (rc != 0)
		goto out;

	rc = m0_stob_locate(stob, &cp_fom->fo_tx);
	if (rc != 0) {
		m0_stob_put(stob);
		goto out;
	}

	m0_stob_io_init(stio);
	stio->si_flags = 0;
	stio->si_opcode = op;

	bshift = stob->so_op->sop_block_shift(stob);

	rc = indexvec_prepare(&stio->si_stob, sns_cp->sc_index,
			      cp->c_seg_nr, cp->c_seg_size, addb_ctx, bshift);
	if (rc != 0)
		goto err_stio;

	rc = bufvec_prepare(&stio->si_user, cp->c_data, cp->c_seg_nr,
			    cp->c_seg_size, bshift);
	if (rc != 0) {
		indexvec_free(&stio->si_stob);
		goto err_stio;
	}

	m0_mutex_lock(&stio->si_mutex);
	m0_fom_wait_on(cp_fom, &stio->si_wait, &cp_fom->fo_cb);
	m0_mutex_unlock(&stio->si_mutex);

	rc = m0_stob_io_launch(stio, stob, &cp_fom->fo_tx, NULL);
	if (rc != 0) {
		m0_fom_callback_cancel(&cp_fom->fo_cb);
		indexvec_free(&stio->si_stob);
		bufvec_free(&stio->si_user);
		goto err_stio;
	} else
		goto out;

err_stio:
	m0_stob_io_fini(stio);
	m0_stob_put(stob);
out:
	if (rc != 0) {
		m0_fom_phase_move(cp_fom, rc, M0_CCP_FINI);
		m0_dtx_done(&cp_fom->fo_tx);
		rc = M0_FSO_WAIT;
	} else
		rc = cp->c_ops->co_phase_next(cp);

	return rc;
}

M0_INTERNAL int m0_sns_cm_cp_read(struct m0_cm_cp *cp)
{
	cp->c_io_op = M0_CM_CP_READ;
	return cp_io(cp, SIO_READ);
}

static void spare_stobid_fill(struct m0_cm_cp *cp)
{
	struct m0_sns_cm_ag *sns_ag = ag2snsag(cp->c_ag);
	struct m0_sns_cm_cp *sns_cp = cp2snscp(cp);

	sns_cp->sc_sid.si_bits.u_hi = sns_ag->sag_tgt_cobfid.f_container;
	sns_cp->sc_sid.si_bits.u_lo = sns_ag->sag_tgt_cobfid.f_key;
	sns_cp->sc_index            = sns_ag->sag_tgt_cob_index;
}

M0_INTERNAL int m0_sns_cm_cp_write(struct m0_cm_cp *cp)
{
	cp->c_io_op = M0_CM_CP_WRITE;
	spare_stobid_fill(cp);
	/*
	 * Finalise the bitmap representing the transformed copy packets.
	 * It is not needed after this point.
	 * Note: Some copy packets may not have this bitmap initialised as they
	 * may not be resultant copy packets created after transformation.
	 * Hence a check is needed to see if number of indices in the bitmap
	 * is greater than 0, before finalising it.
	 */
	if (cp->c_xform_cp_indices.b_nr > 0)
		m0_bitmap_fini(&cp->c_xform_cp_indices);

	return cp_io(cp, SIO_WRITE);
}

M0_INTERNAL int m0_sns_cm_cp_io_wait(struct m0_cm_cp *cp)
{
	struct m0_sns_cm_cp *sns_cp = cp2snscp(cp);
	int                      rc = sns_cp->sc_stio.si_rc;

	if (sns_cp->sc_stio.si_opcode == SIO_WRITE)
		cp->c_ops->co_complete(cp);

	/* Cleanup before proceeding to next phase. */
	indexvec_free(&sns_cp->sc_stio.si_stob);
	bufvec_free(&sns_cp->sc_stio.si_user);
	m0_stob_io_fini(&sns_cp->sc_stio);
	m0_stob_put(sns_cp->sc_stob);

	m0_dtx_done(&cp->c_fom.fo_tx);
	if (rc != 0) {
		m0_fom_phase_move(&cp->c_fom, rc, M0_CCP_FINI);
		return M0_FSO_WAIT;
	}
	return cp->c_ops->co_phase_next(cp);
}

/** @} SNSCMCP */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
