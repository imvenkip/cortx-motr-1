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
 * Original author: Rohan Puri <rohan_puri@xyratex.com>
 * Original creation date: 03/14/2013
 */

/* This file is designed to be included by addb/addb.c */

/**
   @ingroup addb_svc_pvt
   @{
 */

static int addb_fom_create(struct m0_fop *fop, struct m0_fom **out,
			   struct m0_reqh *reqh);

/*
 ******************************************************************************
 * ADDB fop FOM Type
 ******************************************************************************
 */
enum addb_fom_phase {
	ADDB_FOM_PHASE_INIT = M0_FOM_PHASE_INIT,
	ADDB_FOM_PHASE_FINI = M0_FOM_PHASE_FINISH,
	ADDB_FOM_PHASE_REC_SEQ_STOB_WRITE = M0_FOPH_NR,
};

static struct m0_sm_state_descr addb_fom_state_descr[] = {
	[ADDB_FOM_PHASE_INIT] = {
		.sd_flags    = M0_SDF_INITIAL,
		.sd_name     = "Init",
		.sd_allowed  = M0_BITS(ADDB_FOM_PHASE_REC_SEQ_STOB_WRITE)
	},
        [ADDB_FOM_PHASE_REC_SEQ_STOB_WRITE] = {
		.sd_flags       = 0,
                .sd_name        = "ADDB rec seq. stob write",
                .sd_allowed     = M0_BITS(ADDB_FOM_PHASE_FINI)
        },
        [ADDB_FOM_PHASE_FINI] = {
		.sd_flags       = M0_SDF_TERMINAL,
                .sd_name        = "Fini",
                .sd_allowed     = 0
        },
};

static struct m0_sm_conf addb_fom_sm_conf = {
	.scf_name = "addb-fom-sm",
	.scf_nr_states = ARRAY_SIZE(addb_fom_state_descr),
	.scf_state = addb_fom_state_descr,
};

static const struct m0_fom_type_ops addb_fom_type_ops = {
        .fto_create = addb_fom_create,
};

static void addb_fom_fo_fini(struct m0_fom *fom)
{
        struct addb_fom *addb_fom = container_of(fom, struct addb_fom, af_fom);

	m0_fom_fini(fom);
	m0_free(addb_fom);
}

static size_t addb_fom_fo_locality(const struct m0_fom *fom)
{
	M0_PRE(fom != NULL);

	return m0_fop_opcode(fom->fo_fop);
}

static int addb_fom_fo_tick(struct m0_fom *fom)
{
	struct m0_addb_rpc_sink_fop  *addb_fop;
	struct m0_bufvec_cursor       cur;
	struct m0_bufvec              bv;
	struct m0_xcode_ctx           ctx;
	struct m0_reqh               *reqh;
	m0_bcount_t                   len;
	int                           rc = M0_FSO_AGAIN;

	switch (m0_fom_phase(fom)) {
	case ADDB_FOM_PHASE_INIT:
		m0_fom_phase_set(fom, ADDB_FOM_PHASE_REC_SEQ_STOB_WRITE);
		break;
	case ADDB_FOM_PHASE_REC_SEQ_STOB_WRITE:
		reqh = m0_fom_reqh(fom);
		M0_ASSERT(m0_addb_mc_is_fully_configured(&reqh->rh_addb_mc));
		addb_fop = m0_fop_data(fom->fo_fop);
		M0_ASSERT(addb_fop != NULL);
		/* Get the length of addb recs seq */
		m0_xcode_ctx_init(&ctx, &M0_FOP_XCODE_OBJ(fom->fo_fop));
		len = m0_xcode_length(&ctx);
		rc = m0_bufvec_alloc(&bv, 1, len + 1);
		if (rc != 0)
			return M0_RC(rc);
		m0_bufvec_cursor_init(&cur, &bv);
		/**
		 * @todo When encode/decode methods of addb rpc items are
		 * defined, do not encode the addb rec sequence again as its
		 * decode method would not xcode decode it. Just write this
		 * xcode encoded addb rec sequence directly to addb stob.
		 */
		rc = addb_rec_seq_enc(addb_fop, &cur, m0_addb_rpc_sink_fop_xc);
		if (rc != 0) {
			m0_bufvec_free(&bv);
			return M0_RC(rc);
		}
		M0_ASSERT(cur.bc_vc.vc_seg == 0 && cur.bc_vc.vc_offset == len);
		m0_bufvec_cursor_init(&cur, &bv);
		reqh->rh_addb_mc.am_sink->rs_save_seq(&reqh->rh_addb_mc, &cur,
						      len);
		m0_bufvec_free(&bv);
		m0_fom_phase_set(fom, ADDB_FOM_PHASE_FINI);
		rc = M0_FSO_WAIT;
		break;
	default:
		M0_IMPOSSIBLE("Phase not defined");
	}

	return M0_RC(rc);
}

static void addb_fom_fo_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
	M0_PRE(mc != NULL);
	M0_PRE(fom->fo_service != NULL);

	M0_ADDB_CTX_INIT(mc, &fom->fo_addb_ctx, &m0_addb_ct_addb_fom,
			 &fom->fo_service->rs_addb_ctx);
}

static const struct m0_fom_ops addb_fom_ops = {
        .fo_fini          = addb_fom_fo_fini,
        .fo_tick          = addb_fom_fo_tick,
        .fo_home_locality = addb_fom_fo_locality,
	.fo_addb_init     = addb_fom_fo_addb_init
};

static int addb_fom_create(struct m0_fop *fop, struct m0_fom **out,
			      struct m0_reqh *reqh)
{
	struct addb_fom *addb_fom;
	struct m0_fom   *fom;

	M0_ALLOC_PTR(addb_fom);
	if (addb_fom == NULL)
		return M0_RC(-ENOMEM);

	fom = &addb_fom->af_fom;

	m0_fom_init(fom, &fop->f_type->ft_fom_type, &addb_fom_ops, fop, NULL,
		    reqh, fop->f_type->ft_fom_type.ft_rstype);

	M0_PRE(m0_fom_phase(fom) == ADDB_FOM_PHASE_INIT);
	*out = fom;
	return M0_RC(0);
}

/** @} end group addb_pvt */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
