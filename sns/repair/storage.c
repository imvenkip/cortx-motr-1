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
#include "mero/mero_setup.h"

#include "sns/repair/ag.h"
#include "sns/repair/cp.h"

/**
 * @addtogroup SNSRepairCP
 * @{
 */

enum {
	CP_BUF_NR = 1
};

static int indexvec_prepare(struct m0_indexvec *iv, m0_bindex_t idx,
			    uint32_t bshift)
{
	M0_PRE(iv != NULL);

	/* It is assumed that each copy packet will have single unit. */
	iv->iv_vec.v_nr = CP_BUF_NR;

	M0_ALLOC_ARR(iv->iv_vec.v_count, CP_BUF_NR);
	if (iv->iv_vec.v_count == NULL)
		return -ENOMEM;

	M0_ALLOC_ARR(iv->iv_index, CP_BUF_NR);
	if (iv->iv_index == NULL) {
		m0_free(iv->iv_vec.v_count);
		return -ENOMEM;
	}

	iv->iv_index[0] = idx >> bshift;
	iv->iv_vec.v_count[0] = M0_CP_SIZE >> bshift;

	return 0;
}

static void indexvec_free(struct m0_indexvec *iv)
{
	m0_free(iv->iv_vec.v_count);
	m0_free(iv->iv_index);
}

static int bufvec_prepare(struct m0_bufvec *obuf, struct m0_bufvec *ibuf,
			  uint32_t bshift)
{
	M0_PRE(obuf != NULL);
	M0_PRE(ibuf != NULL);

	obuf->ov_vec.v_nr = CP_BUF_NR;
	M0_ALLOC_ARR(obuf->ov_vec.v_count, CP_BUF_NR);
	if (obuf->ov_vec.v_count == NULL)
		return -ENOMEM;

	M0_ALLOC_ARR(obuf->ov_buf, CP_BUF_NR);
	if (obuf->ov_buf == NULL) {
		m0_free(obuf->ov_vec.v_count);
		return -ENOMEM;
	}

	obuf->ov_vec.v_count[0] = M0_CP_SIZE >> bshift;
	obuf->ov_buf[0] = m0_stob_addr_pack(ibuf->ov_buf[0], bshift);

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
	struct m0_sns_repair_cp *sns_cp;
	struct m0_stob          *stob;
	struct m0_stob_id       *stobid;
	struct m0_stob_io       *stio;
	uint32_t                 bshift;
	int                      rc;
	bool                     result;

	sns_cp = cp2snscp(cp);
	cp_fom = &cp->c_fom;
	reqh = m0_fom_reqh(cp_fom);
	stobid = &sns_cp->rc_sid;
	stio = &sns_cp->rc_stio;
	dom = m0_cs_stob_domain_find(reqh, stobid);

	if (dom == NULL) {
		rc = -EINVAL;
		goto out;
	}

	rc = m0_stob_find(dom, stobid, &sns_cp->rc_stob);
	if (rc != 0)
		goto out;

	stob = sns_cp->rc_stob;
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

	rc = indexvec_prepare(&stio->si_stob, sns_cp->rc_index, bshift);
	if (rc != 0)
		goto err_stio;

	rc = bufvec_prepare(&stio->si_user, cp->c_data, bshift);
	if (rc != 0) {
		indexvec_free(&stio->si_stob);
		goto err_stio;
	}

	m0_fom_wait_on(cp_fom, &stio->si_wait, &cp_fom->fo_cb);

	rc = m0_stob_io_launch(stio, stob, &cp_fom->fo_tx, NULL);
	if (rc != 0) {
		result = m0_fom_callback_cancel(&cp_fom->fo_cb);
		M0_ASSERT(result);
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
		m0_db_tx_abort(&cp_fom->fo_tx.tx_dbtx);
		return M0_FSO_WAIT;
	} else {
		cp->c_ops->co_phase_next(cp);
		return M0_FSO_WAIT;
	}
}

M0_INTERNAL int m0_sns_repair_cp_read(struct m0_cm_cp *cp)
{
	cp->c_io_op = M0_CM_CP_READ;
	return cp_io(cp, SIO_READ);
}

static void spare_stobid_fill(struct m0_cm_cp *cp)
{
	struct m0_sns_repair_ag *sns_ag = ag2snsag(cp->c_ag);
	struct m0_sns_repair_cp *sns_cp = cp2snscp(cp);

	sns_cp->rc_sid.si_bits.u_hi = sns_ag->sag_spare_cobfid.f_container;
	sns_cp->rc_sid.si_bits.u_lo = sns_ag->sag_spare_cobfid.f_key;
	sns_cp->rc_index            = sns_ag->sag_spare_cob_index;
}

M0_INTERNAL int m0_sns_repair_cp_write(struct m0_cm_cp *cp)
{
	cp->c_io_op = M0_CM_CP_WRITE;
	spare_stobid_fill(cp);

	return cp_io(cp, SIO_WRITE);
}

M0_INTERNAL int m0_sns_repair_cp_io_wait(struct m0_cm_cp *cp)
{
	struct m0_sns_repair_cp *sns_cp = cp2snscp(cp);
	int                      rc = sns_cp->rc_stio.si_rc;

	if (sns_cp->rc_stio.si_opcode == SIO_WRITE)
		cp->c_ops->co_complete(cp);

	/* Cleanup before proceeding to next phase. */
	indexvec_free(&sns_cp->rc_stio.si_stob);
	bufvec_free(&sns_cp->rc_stio.si_user);
	m0_stob_io_fini(&sns_cp->rc_stio);
	m0_stob_put(sns_cp->rc_stob);

	if (rc != 0) {
		m0_fom_phase_move(&cp->c_fom, rc, M0_CCP_FINI);
		m0_db_tx_abort(&cp->c_fom.fo_tx.tx_dbtx);
		return M0_FSO_WAIT;
	} else
		m0_dtx_done(&cp->c_fom.fo_tx);

	return cp->c_ops->co_phase_next(cp);
}

/** @} SNSRepairCP */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
