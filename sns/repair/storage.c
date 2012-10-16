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
#include "colibri/colibri_setup.h"
#include "sns/repair/cp.h"

/**
 * @addtogroup SNSRepairCP
 * @{
 */

enum {
	CP_BUF_NR = 1
};

static int indexvec_prepare(struct c2_indexvec *iv, c2_bindex_t idx,
			    uint32_t bshift)
{
	C2_PRE(iv != NULL);

	iv->iv_vec.v_nr = CP_BUF_NR;

	C2_ALLOC_ARR(iv->iv_vec.v_count, CP_BUF_NR); 
	if (iv->iv_vec.v_count == NULL)
		return -ENOMEM;

	C2_ALLOC_ARR(iv->iv_vec.v_count, CP_BUF_NR);
	if (iv->iv_vec.v_count == NULL) {
		c2_free(iv->iv_vec.v_count);
		return -ENOMEM;
	}

	iv->iv_index[0] = idx >> bshift;
	iv->iv_vec.v_count[0] = C2_CP_SIZE >> bshift;

	return 0;
}

static void indexvec_free(struct c2_indexvec *iv)
{
	c2_free(iv->iv_vec.v_count);
	c2_free(iv->iv_index);
}

static int bufvec_prepare(struct c2_bufvec *obuf, struct c2_bufvec *ibuf,
			  uint32_t bshift)
{
	C2_PRE(obuf != NULL);
	C2_PRE(ibuf != NULL);

	obuf->ov_vec.v_nr = CP_BUF_NR;
	C2_ALLOC_ARR(obuf->ov_vec.v_count, CP_BUF_NR);
	if (obuf->ov_vec.v_count == NULL)
		return -ENOMEM;

	C2_ALLOC_ARR(obuf->ov_buf, CP_BUF_NR);
	if (obuf->ov_buf == NULL) {
		c2_free(obuf->ov_vec.v_count);
		return -ENOMEM;
	}

	obuf->ov_vec.v_count[0] = C2_CP_SIZE >> bshift;
	obuf->ov_buf[0] = c2_stob_addr_pack(ibuf->ov_buf[0], bshift);

	return 0;
}

static void bufvec_free(struct c2_bufvec *bv)
{
	c2_free(bv->ov_vec.v_count);
	c2_free(bv->ov_buf);
}

static int cp_io(struct c2_cm_cp *cp, const enum c2_stob_io_opcode op)
{
	struct c2_fom           *cp_fom;
	struct c2_reqh          *reqh;
	struct c2_stob_domain   *dom;
	struct c2_sns_repair_cp *sns_cp;
	struct c2_stob          *stob;
	struct c2_stob_id       *stobid;
	struct c2_stob_io       *stio;
	uint32_t                 bshift;
	int                      rc;
	
	sns_cp = cp2snscp(cp);
	cp_fom = &cp->c_fom;
	reqh = c2_fom_reqh(cp_fom);
	stobid = &sns_cp->rc_sid;
	dom = c2_cs_stob_domain_find(reqh, stobid);
	if (dom == NULL) {
		rc = -EINVAL;
		goto err;
	}
	
	C2_ALLOC_PTR(stob);
	if (stob == NULL){
		rc = -ENOMEM;
		goto err;
	}

	C2_ALLOC_PTR(stio);
	if (stio == NULL) {
		rc = -ENOMEM;
		goto err_stob;
	}

	rc = c2_stob_find(dom, stobid, &stob);
	if (rc != 0)
		goto err_stob;

	rc = c2_stob_locate(stob, &cp_fom->fo_tx); 
	if (rc != 0) {
		c2_stob_put(stob);
		goto err_stob;
	}

	bshift = stob->so_op->sop_block_shift(stob);

	c2_stob_io_init(stio);
	stio->si_flags = 0;
	stio->si_opcode = op;
 
	rc = indexvec_prepare(&stio->si_stob, sns_cp->rc_index, bshift);
	if (rc != 0)
		goto err_stio;

	rc = bufvec_prepare(&stio->si_user, cp->c_data, bshift);
	if (rc != 0) {
		indexvec_free(&stio->si_stob);
		goto err_stio;
	}

	rc = c2_stob_io_launch(stio, stob, &cp_fom->fo_tx, NULL);
	if (rc != 0) {
		indexvec_free(&stio->si_stob);
		bufvec_free(&stio->si_user);
		goto err_stio;
	}

err_stio:
	c2_stob_io_fini(stio);
	c2_stob_put(stob);
	c2_free(stio);
err_stob:
	c2_free(stob);
err:
	return rc;
}

int c2_repair_cp_read(struct c2_cm_cp *cp)
{
	int rc;

	rc = cp_io(cp, SIO_READ);	
        cp->c_ops->co_phase_next(cp);
        return C2_FSO_AGAIN;
}

int c2_repair_cp_write(struct c2_cm_cp *cp)
{
	int rc;

	rc = cp_io(cp, SIO_WRITE);
        cp->c_ops->co_phase_next(cp);
        return C2_FSO_AGAIN;
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
