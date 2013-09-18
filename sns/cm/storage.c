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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/trace.h"
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

static int ivec_prepare(struct m0_indexvec *iv, m0_bindex_t idx,
			uint32_t seg_nr, size_t seg_size,
			struct m0_addb_ctx *ctx, uint32_t bshift)
{
	int rc;
	int i;

	M0_PRE(iv != NULL);

	rc = m0_indexvec_alloc(iv, seg_nr, ctx, M0_ADDB_CTXID_SNS_CM);
	if (rc != 0)
		return rc;

	for (i = 0; i < seg_nr; ++i) {
		iv->iv_vec.v_count[i] = seg_size >> bshift;
		iv->iv_index[i] = idx >> bshift;
		idx += seg_size;
	}

	return 0;
}

static int bufvec_prepare(struct m0_bufvec *obuf, struct m0_tl *cp_buffers_head,
			  uint32_t data_seg_nr, size_t seg_size, uint32_t bshift)
{
	struct m0_net_buffer *nbuf;
	struct m0_bufvec     *ibuf;
	int                  i;
	int                  j = 0;

	M0_PRE(obuf != NULL);
	M0_PRE(!cp_data_buf_tlist_is_empty(cp_buffers_head));

	obuf->ov_vec.v_nr = data_seg_nr;
	SNS_ALLOC_ARR(obuf->ov_vec.v_count, data_seg_nr, &m0_sns_cp_addb_ctx,
		      CP_STORAGE_OV_VEC);
	if (obuf->ov_vec.v_count == NULL)
		return -ENOMEM;

	SNS_ALLOC_ARR(obuf->ov_buf, data_seg_nr, &m0_sns_cp_addb_ctx,
		      CP_STORAGE_OV_BUF);
	if (obuf->ov_buf == NULL) {
		m0_free(obuf->ov_vec.v_count);
		return -ENOMEM;
	}

	m0_tl_for(cp_data_buf, cp_buffers_head, nbuf) {
		ibuf = &nbuf->nb_buffer;
		for (i = 0; i < nbuf->nb_pool->nbp_seg_nr && j < data_seg_nr;
		     ++i, ++j) {
			obuf->ov_vec.v_count[j] = seg_size >> bshift;
			obuf->ov_buf[j] = m0_stob_addr_pack(ibuf->ov_buf[i],
							    bshift);
		}
	} m0_tl_endfor;

	M0_POST(j == data_seg_nr);

	return 0;
}

static void bufvec_free(struct m0_bufvec *bv)
{
	m0_free(bv->ov_vec.v_count);
	m0_free(bv->ov_buf);
}

static int cp_prepare(struct m0_cm_cp *cp,
		      struct m0_indexvec *dst_ivec,
		      struct m0_bufvec *dst_bvec,
		      m0_bindex_t start_idx,
		      struct m0_addb_ctx *ctx, uint32_t bshift)
{
	struct m0_net_buffer *nbuf;
	uint32_t              data_seg_nr;
	size_t                seg_size;
	int                   rc;

	M0_PRE(m0_cm_cp_invariant(cp));
	M0_PRE(!cp_data_buf_tlist_is_empty(&cp->c_buffers));

	nbuf = cp_data_buf_tlist_head(&cp->c_buffers);
	data_seg_nr = cp->c_data_seg_nr;
	seg_size = nbuf->nb_pool->nbp_seg_size;

	rc = ivec_prepare(dst_ivec, start_idx, data_seg_nr, seg_size, ctx,
			  bshift);
	if (rc != 0)
		return rc;

	rc = bufvec_prepare(dst_bvec, &cp->c_buffers, data_seg_nr, seg_size,
			    bshift);
	if (rc != 0)
		m0_indexvec_free(dst_ivec);

	return rc;
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

	M0_ENTRY("cp=%p op=%d", cp, op);

	addb_ctx = &cp->c_ag->cag_cm->cm_service.rs_addb_ctx;
	sns_cp = cp2snscp(cp);
	cp_fom = &cp->c_fom;
	reqh = m0_fom_reqh(cp_fom);
	stobid = &sns_cp->sc_sid;
	stio = &sns_cp->sc_stio;
	dom = m0_cs_stob_domain_find(reqh, stobid);
	m0_sns_cm_cp_addb_log(cp);

	if (dom == NULL) {
		rc = -EINVAL;
		goto out;
	}

	rc = m0_stob_find(dom, stobid, &sns_cp->sc_stob);
	if (rc != 0)
		goto out;

	stob = sns_cp->sc_stob;
	m0_dtx_init(&cp_fom->fo_tx, reqh->rh_beseg->bs_domain,
		    &cp_fom->fo_loc->fl_group);
	rc = dom->sd_ops->sdo_tx_make(dom, &cp_fom->fo_tx);
	if (rc != 0)
		goto out;

	rc = m0_stob_locate(stob);
	if (rc != 0) {
		m0_stob_put(stob);
		goto out;
	}

	m0_stob_io_init(stio);
	stio->si_flags = 0;
	stio->si_opcode = op;
	stio->si_fol_rec_part = &sns_cp->sc_fol_rec_part;

	bshift = stob->so_op->sop_block_shift(stob);
	rc = cp_prepare(cp, &stio->si_stob, &stio->si_user, sns_cp->sc_index,
			addb_ctx, bshift);
	if (rc != 0)
		goto err_stio;

	m0_mutex_lock(&stio->si_mutex);
	m0_fom_wait_on(cp_fom, &stio->si_wait, &cp_fom->fo_cb);
	m0_mutex_unlock(&stio->si_mutex);

	rc = m0_stob_io_launch(stio, stob, &cp_fom->fo_tx, NULL);
	if (rc != 0) {
		m0_mutex_lock(&stio->si_mutex);
		m0_fom_callback_cancel(&cp_fom->fo_cb);
		m0_mutex_unlock(&stio->si_mutex);
		m0_indexvec_free(&stio->si_stob);
		bufvec_free(&stio->si_user);
		goto err_stio;
	} else
		goto out;

err_stio:
	m0_stob_io_fini(stio);
	m0_stob_put(stob);
out:
	if (rc != 0) {
		SNS_ADDB_FUNCFAIL(rc, &m0_sns_cp_addb_ctx, CP_IO);
		m0_fom_phase_move(cp_fom, rc, M0_CCP_FINI);
		m0_dtx_done(&cp_fom->fo_tx);
		rc = M0_FSO_WAIT;
	} else
		rc = cp->c_ops->co_phase_next(cp);

	M0_RETURN(rc);
}

M0_INTERNAL int m0_sns_cm_cp_read(struct m0_cm_cp *cp)
{
	cp->c_io_op = M0_CM_CP_READ;
	return cp_io(cp, SIO_READ);
}

M0_INTERNAL int m0_sns_cm_cp_write(struct m0_cm_cp *cp)
{
	cp->c_io_op = M0_CM_CP_WRITE;
	return cp_io(cp, SIO_WRITE);
}

M0_INTERNAL int m0_sns_cm_cp_io_wait(struct m0_cm_cp *cp)
{
	struct m0_sns_cm_cp *sns_cp = cp2snscp(cp);
	int                  rc;

	M0_ENTRY("cp=%p", cp);

	if (sns_cp->sc_stio.si_opcode == SIO_WRITE)
		cp->c_ops->co_complete(cp);

	/* Cleanup before proceeding to next phase. */
	m0_indexvec_free(&sns_cp->sc_stio.si_stob);
	bufvec_free(&sns_cp->sc_stio.si_user);
	m0_stob_io_fini(&sns_cp->sc_stio);
	m0_stob_put(sns_cp->sc_stob);

	m0_dtx_done(&cp->c_fom.fo_tx);

	rc = sns_cp->sc_stio.si_rc;
	if (rc != 0) {
		SNS_ADDB_FUNCFAIL(rc, &m0_sns_cp_addb_ctx, CP_STIO);
		m0_fom_phase_move(&cp->c_fom, rc, M0_CCP_FINI);
		rc = M0_FSO_WAIT;
	} else {
		rc = cp->c_ops->co_phase_next(cp);
	}

	M0_RETURN(rc);
}

/** @} SNSCMCP */

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
