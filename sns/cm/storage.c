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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "mero/setup.h"

#include "sns/cm/ag.h"
#include "sns/cm/cm.h"
#include "sns/cm/cp.h"
#include "sns/cm/file.h"

#include "stob/domain.h"    /* m0_stob_domain_find_by_stob_id */

/**
 * @addtogroup SNSCMCP
 * @{
 */

static int ivec_prepare(struct m0_cm_cp *cp, struct m0_indexvec *iv,
			m0_bindex_t idx, size_t unit_size, size_t max_buf_size,
			uint32_t bshift)
{
	size_t   seg_size = unit_size < max_buf_size ?
			    unit_size : max_buf_size;
	uint32_t seg_nr = (unit_size / max_buf_size) +
				(unit_size % max_buf_size > 0);
	int      rc;
	int      i;

	M0_PRE(iv != NULL);

	rc = m0_indexvec_alloc(iv, seg_nr);
	if (rc != 0)
		return M0_RC(rc);

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
	M0_ALLOC_ARR(obuf->ov_vec.v_count, data_seg_nr);
	if (obuf->ov_vec.v_count == NULL)
		return M0_ERR(-ENOMEM);

	M0_ALLOC_ARR(obuf->ov_buf, data_seg_nr);
	if (obuf->ov_buf == NULL) {
		m0_free(obuf->ov_vec.v_count);
		return M0_ERR(-ENOMEM);
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
		      m0_bindex_t start_idx, uint32_t bshift)
{
	struct m0_net_buffer *nbuf;
	uint32_t              data_seg_nr;
	size_t                unit_size;
	size_t                max_buf_size;
	size_t                seg_size;
	uint32_t              seg_nr;
	int                   rc;

	M0_PRE(m0_cm_cp_invariant(cp));
	M0_PRE(!cp_data_buf_tlist_is_empty(&cp->c_buffers));

	nbuf = cp_data_buf_tlist_head(&cp->c_buffers);
	data_seg_nr = cp->c_data_seg_nr;
	seg_size = nbuf->nb_pool->nbp_seg_size;
	seg_nr = nbuf->nb_pool->nbp_seg_nr;
	unit_size = data_seg_nr * seg_size;
	max_buf_size = seg_size * seg_nr;
	rc = ivec_prepare(cp, dst_ivec, start_idx, unit_size,
			  max_buf_size, bshift);
	if (rc != 0)
		return M0_RC(rc);
	rc = bufvec_prepare(dst_bvec, &cp->c_buffers, data_seg_nr, seg_size,
			    bshift);
	if (rc != 0)
		m0_indexvec_free(dst_ivec);

	return M0_RC(rc);
}

static int cp_stob_io_init(struct m0_cm_cp *cp, const enum m0_stob_io_opcode op)
{
	struct m0_stob_domain *dom;
	struct m0_sns_cm_cp   *sns_cp;
	struct m0_stob        *stob;
	struct m0_stob_io     *stio;
	uint32_t               bshift;
	int                    rc;

	M0_ENTRY("cp=%p op=%d", cp, op);

	sns_cp = cp2snscp(cp);
	stio = &sns_cp->sc_stio;
	dom = m0_stob_domain_find_by_stob_id(&sns_cp->sc_stob_id);

	if (dom == NULL)
		return M0_ERR(-EINVAL);

	rc = m0_stob_find(&sns_cp->sc_stob_id, &sns_cp->sc_stob);
	if (rc != 0)
		return M0_ERR(rc);

	stob = sns_cp->sc_stob;
	rc = m0_stob_state_get(stob) == CSS_UNKNOWN ? m0_stob_locate(stob) : 0;
	if (rc != 0) {
		m0_stob_put(stob);
		return M0_ERR(rc);
	}
	m0_stob_io_init(stio);
	stio->si_flags = 0;
	stio->si_opcode = op;
	stio->si_fol_frag = &sns_cp->sc_fol_frag;
	bshift = m0_stob_block_shift(stob);
	return cp_prepare(cp, &stio->si_stob, &stio->si_user, sns_cp->sc_index,
			  bshift);
}

static int cp_io(struct m0_cm_cp *cp, const enum m0_stob_io_opcode op)
{
	struct m0_fom         *cp_fom;
	struct m0_sns_cm_cp   *sns_cp;
	struct m0_reqh        *reqh;
	struct m0_stob_domain *dom;
	struct m0_stob        *stob;
	struct m0_stob_io     *stio;
	struct m0_dtx         *tx;
	int                    rc;

	M0_ENTRY("cp=%p op=%d", cp, op);

	cp_fom = &cp->c_fom;
	tx = &cp_fom->fo_tx;
	reqh = m0_fom_reqh(cp_fom);
	sns_cp = cp2snscp(cp);
	stio = &sns_cp->sc_stio;
	dom = m0_stob_domain_find_by_stob_id(&sns_cp->sc_stob_id);
	if (tx->tx_state < M0_DTX_INIT) {
		rc = cp_stob_io_init(cp, op);
		if (rc != 0)
			goto out;
		m0_dtx_init(&cp_fom->fo_tx, reqh->rh_beseg->bs_domain,
			    &cp_fom->fo_loc->fl_group);
		if (op == SIO_WRITE) {
			m0_stob_io_credit(stio, dom, m0_fom_tx_credit(cp_fom));
		}
		m0_dtx_open(&cp_fom->fo_tx);
	}
	if (m0_be_tx_state(&tx->tx_betx) == M0_BTS_FAILED) {
		rc = tx->tx_betx.t_sm.sm_rc;
		goto out;
	}
	if (m0_be_tx_state(&tx->tx_betx) == M0_BTS_OPENING) {
		m0_fom_wait_on(cp_fom, &tx->tx_betx.t_sm.sm_chan,
				&cp_fom->fo_cb);
		return M0_FSO_WAIT;
	} else
		m0_dtx_opened(tx);

	m0_mutex_lock(&stio->si_mutex);
	m0_fom_wait_on(cp_fom, &stio->si_wait, &cp_fom->fo_cb);
	m0_mutex_unlock(&stio->si_mutex);

	stob = sns_cp->sc_stob;
	rc = m0_stob_io_launch(stio, stob, &cp_fom->fo_tx, NULL);
	if (rc != 0) {
		m0_mutex_lock(&stio->si_mutex);
		m0_fom_callback_cancel(&cp_fom->fo_cb);
		m0_mutex_unlock(&stio->si_mutex);
		m0_indexvec_free(&stio->si_stob);
		bufvec_free(&stio->si_user);
		m0_stob_io_fini(stio);
		m0_stob_put(stob);
	}
out:
	if (rc != 0) {
		m0_fom_phase_move(cp_fom, rc, M0_CCP_FAIL);
		m0_dtx_done_sync(&cp_fom->fo_tx);
		m0_dtx_fini(&cp->c_fom.fo_tx);
		return M0_FSO_AGAIN;
	}
	return cp->c_ops->co_phase_next(cp);
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
	struct m0_fom       *fom = &cp->c_fom;
	struct m0_dtx       *tx = &fom->fo_tx;
	int                  rc;

	M0_ENTRY("cp=%p", cp);

        if (tx->tx_state != M0_DTX_DONE) {
                m0_fom_wait_on(fom, &tx->tx_betx.t_sm.sm_chan, &fom->fo_cb);
                m0_dtx_done(tx);
                return M0_FSO_WAIT;
        } else {
                if (m0_be_tx_state(&tx->tx_betx) != M0_BTS_DONE) {
                        m0_fom_wait_on(fom, &tx->tx_betx.t_sm.sm_chan,
                                                &fom->fo_cb);
                        return M0_FSO_WAIT;
                }
        }
        m0_dtx_fini(tx);

	if (sns_cp->sc_stio.si_opcode == SIO_WRITE)
		cp->c_ops->co_complete(cp);

	/* Cleanup before proceeding to next phase. */
	m0_indexvec_free(&sns_cp->sc_stio.si_stob);
	bufvec_free(&sns_cp->sc_stio.si_user);
	m0_stob_io_fini(&sns_cp->sc_stio);
	m0_stob_put(sns_cp->sc_stob);

	rc = sns_cp->sc_stio.si_rc;
	if (rc != 0) {
		m0_fom_phase_move(&cp->c_fom, rc, M0_CCP_FAIL);
		return M0_FSO_AGAIN;
	}
	return cp->c_ops->co_phase_next(cp);
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
