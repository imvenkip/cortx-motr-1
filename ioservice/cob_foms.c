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
 * Original author: Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 * Original creation date: 02/07/2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_COB
#include "lib/trace.h"

#include "ioservice/cob_foms.h"    /* m0_fom_cob_create */
#include "ioservice/fid_convert.h" /* m0_fid_convert_cob2stob */
#include "ioservice/io_device.h" /* M0_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH */
#include "ioservice/io_service.h"  /* m0_reqh_io_service */
#include "ioservice/storage_dev.h" /* m0_storage_dev_stob_find */
#include "mero/setup.h"            /* m0_cs_ctx_get */
#include "stob/domain.h"           /* m0_stob_domain_find_by_stob_id */

struct m0_poolmach;
struct m0_poolmach_versions;

/* Forward Declarations. */
static void cc_fom_fini(struct m0_fom *fom);
static int  cob_ops_fom_tick(struct m0_fom *fom);
static void cob_op_credit(struct m0_fom *fom, enum m0_cob_op opcode,
			  struct m0_be_tx_credit *accum);
static int  cc_cob_create(struct m0_fom            *fom,
			  struct m0_fom_cob_op     *cc,
			  const struct m0_cob_attr *attr);

static void cd_fom_fini(struct m0_fom *fom);
static int  cd_cob_delete(struct m0_fom            *fom,
			  struct m0_fom_cob_op     *cd,
			  const struct m0_cob_attr *attr);
static int ce_stob_edit_credit(struct m0_fom *fom, struct m0_fom_cob_op *cc,
			       struct m0_be_tx_credit *accum, uint32_t cot);
static int ce_stob_edit(struct m0_fom *fom, struct m0_fom_cob_op *cd,
			uint32_t cot);
static void   cob_fom_populate(struct m0_fom *fom);
static int    cob_op_fom_create(struct m0_fom **out);
static size_t cob_fom_locality_get(const struct m0_fom *fom);
static inline struct m0_fom_cob_op *cob_fom_get(const struct m0_fom *fom);
static int  cob_getattr_fom_tick(struct m0_fom *fom);
static void cob_getattr_fom_fini(struct m0_fom *fom);
static int  cob_getattr(struct m0_fom        *fom,
			struct m0_fom_cob_op *gop,
			struct m0_cob_attr   *attr);
static int  cob_setattr_fom_tick(struct m0_fom *fom);
static void cob_setattr_fom_fini(struct m0_fom *fom);
static int  cob_setattr(struct m0_fom        *fom,
			struct m0_fom_cob_op *gop,
			struct m0_cob_attr   *attr);
static int cob_locate(const struct m0_fom *fom, struct m0_cob **cob);
static int cob_attr_get(struct m0_cob      *cob,
			struct m0_cob_attr *attr);
static void cob_stob_create_credit(struct m0_fom *fom);
static int cob_stob_delete_credit(struct m0_fom *fom);

enum {
	CC_COB_VERSION_INIT	= 0,
	CC_COB_HARDLINK_NR	= 1,
	CD_FOM_STOBIO_LAST_REFS = 1,
};

struct m0_sm_state_descr cob_ops_phases[] = {
	[M0_FOPH_COB_OPS_PREPARE] = {
		.sd_name      = "COB OP Prepare",
		.sd_allowed   = M0_BITS(M0_FOPH_COB_OPS_EXECUTE,
					M0_FOPH_FAILURE)
	},
	[M0_FOPH_COB_OPS_EXECUTE] = {
		.sd_name      = "COB OP EXECUTE",
		.sd_allowed   = M0_BITS(M0_FOPH_SUCCESS,
					M0_FOPH_FAILURE)
	}
};

const struct m0_sm_conf cob_ops_conf = {
	.scf_name      = "COB create/delete/getattr",
	.scf_nr_states = ARRAY_SIZE(cob_ops_phases),
	.scf_state     = cob_ops_phases
};

/**
 * Common fom_type_ops for m0_fop_cob_create, m0_fop_cob_delete,
 * m0_fop_cob_getattr, and m0_fop_cob_setattr fops.
 */
const struct m0_fom_type_ops cob_fom_type_ops = {
	.fto_create = m0_cob_fom_create,
};

/** Cob create fom ops. */
static const struct m0_fom_ops cc_fom_ops = {
	.fo_fini	  = cc_fom_fini,
	.fo_tick	  = cob_ops_fom_tick,
	.fo_home_locality = cob_fom_locality_get
};

/** Cob delete fom ops. */
static const struct m0_fom_ops cd_fom_ops = {
	.fo_fini          = cd_fom_fini,
	.fo_tick          = cob_ops_fom_tick,
	.fo_home_locality = cob_fom_locality_get
};

/** Cob truncate fom ops. */
static const struct m0_fom_ops ct_fom_ops = {
	.fo_fini          = cd_fom_fini,
	.fo_tick          = cob_ops_fom_tick,
	.fo_home_locality = cob_fom_locality_get
};

/** Cob getattr fom ops. */
static const struct m0_fom_ops cob_getattr_fom_ops = {
	.fo_fini	  = cob_getattr_fom_fini,
	.fo_tick	  = cob_getattr_fom_tick,
	.fo_home_locality = cob_fom_locality_get
};

/** Cob setattr fom ops. */
static const struct m0_fom_ops cob_setattr_fom_ops = {
	.fo_fini	  = cob_setattr_fom_fini,
	.fo_tick	  = cob_setattr_fom_tick,
	.fo_home_locality = cob_fom_locality_get
};

static bool cob_is_md(const struct m0_fom_cob_op *cfom)
{
	return cfom->fco_cob_type == M0_COB_MD;
}

static void cob_fom_stob2fid_map(const struct m0_fom_cob_op *cfom,
				 struct m0_fid *out)
{
	if (cob_is_md(cfom))
		*out = cfom->fco_gfid;
	else
		m0_fid_convert_stob2cob(&cfom->fco_stob_id, out);
}

M0_INTERNAL int m0_cob_fom_create(struct m0_fop *fop, struct m0_fom **out,
				  struct m0_reqh *reqh)
{
	int			  rc;
	struct m0_fop            *rfop;
	struct m0_fom		 *fom;
	const struct m0_fom_ops  *fom_ops;
	struct m0_fom_cob_op     *cfom;
	struct m0_fop_type       *reptype;

	M0_PRE(fop != NULL);
	M0_PRE(fop->f_type != NULL);
	M0_PRE(out != NULL);
	M0_PRE(m0_is_cob_create_fop(fop)  || m0_is_cob_delete_fop(fop) ||
	       m0_is_cob_truncate_fop(fop) || m0_is_cob_getattr_fop(fop) ||
	       m0_is_cob_setattr_fop(fop) );

	rc = cob_op_fom_create(out);
	if (rc != 0) {
		return M0_RC(rc);
	}
	cfom = cob_fom_get(*out);
	fom = *out;
	M0_ASSERT(fom != NULL);

	if (m0_is_cob_create_fop(fop)) {
		fom_ops = &cc_fom_ops;
		reptype = &m0_fop_cob_op_reply_fopt;
	} else if (m0_is_cob_delete_fop(fop)) {
		fom_ops = &cd_fom_ops;
		reptype = &m0_fop_cob_op_reply_fopt;
	} else if (m0_is_cob_truncate_fop(fop)) {
		fom_ops = &ct_fom_ops;
		reptype = &m0_fop_cob_op_reply_fopt;
	} else if (m0_is_cob_getattr_fop(fop)) {
		fom_ops = &cob_getattr_fom_ops;
		reptype = &m0_fop_cob_getattr_reply_fopt;
	} else if (m0_is_cob_setattr_fop(fop)) {
		fom_ops = &cob_setattr_fom_ops;
		reptype = &m0_fop_cob_setattr_reply_fopt;
	} else
		M0_IMPOSSIBLE("Invalid fop type!");

	rfop = m0_fop_reply_alloc(fop, reptype);
	if (rfop == NULL) {
		m0_free(cfom);
		return M0_ERR(-ENOMEM);
	}

	m0_fom_init(fom, &fop->f_type->ft_fom_type, fom_ops, fop, rfop, reqh);
	cob_fom_populate(fom);

	return M0_RC(rc);
}

static int cob_op_fom_create(struct m0_fom **out)
{
	struct m0_fom_cob_op *cfom;

	M0_PRE(out != NULL);

	M0_ALLOC_PTR(cfom);
	if (cfom == NULL)
		return M0_ERR(-ENOMEM);

	*out = &cfom->fco_fom;
	return 0;
}

static inline struct m0_fom_cob_op *cob_fom_get(const struct m0_fom *fom)
{
	M0_PRE(fom != NULL);

	return container_of(fom, struct m0_fom_cob_op, fco_fom);
}

static void cc_fom_fini(struct m0_fom *fom)
{
	struct m0_fom_cob_op *cfom;

	M0_PRE(fom != NULL);

	cfom = cob_fom_get(fom);
	m0_fom_fini(fom);
	m0_free(cfom);
}

static size_t cob_fom_locality_get(const struct m0_fom *fom)
{
	M0_PRE(fom != NULL);

	return m0_fid_cob_device_id(&cob_fom_get(fom)->fco_cfid);
}

static void cob_fom_populate(struct m0_fom *fom)
{
	struct m0_fom_cob_op     *cfom;
	struct m0_fop_cob_common *common;
	struct m0_fop            *fop;

	M0_PRE(fom != NULL);
	M0_PRE(fom->fo_fop != NULL);

	fop = fom->fo_fop;
	common = m0_cobfop_common_get(fom->fo_fop);
	cfom = cob_fom_get(fom);
	cfom->fco_gfid = common->c_gobfid;
	cfom->fco_cfid = common->c_cobfid;
	m0_fid_convert_cob2stob(&cfom->fco_cfid, &cfom->fco_stob_id);
	cfom->fco_cob_idx = common->c_cob_idx;
	cfom->fco_cob_type = common->c_cob_type;
	cfom->fco_flags = common->c_flags;
	cfom->fco_fop_type = m0_is_cob_create_fop(fop) ? M0_COB_OP_CREATE :
				m0_is_cob_delete_fop(fop) ?
				M0_COB_OP_DELETE : M0_COB_OP_TRUNCATE;
	cfom->fco_recreate = false;
	cfom->fco_is_done = false;
	M0_LOG(M0_DEBUG, "Cob %s operation for "FID_F"/%x "FID_F" for %s",
			  m0_fop_name(fop), FID_P(&cfom->fco_cfid),
			  cfom->fco_cob_idx, FID_P(&cfom->fco_gfid),
			  cob_is_md(cfom) ? "MD" : "IO");
}

static int cob_fom_pool_version_get(struct m0_fom *fom)
{
	struct m0_fom_cob_op     *cob_op;
	struct m0_fop_cob_common *common;
	struct m0_reqh           *reqh;
	struct m0_mero           *mero;
	int                       rc = 0;

	common = m0_cobfop_common_get(fom->fo_fop);
	cob_op = cob_fom_get(fom);
	reqh = m0_fom_reqh(fom);
	mero = m0_cs_ctx_get(reqh);
	cob_op->fco_pver = m0_pool_version_find(&mero->cc_pools_common,
						&common->c_pver);
	if (cob_op->fco_pver == NULL)
		rc = -EINVAL;
	return M0_RC(rc);
}

static int cob_ops_stob_find(struct m0_fom_cob_op *co)
{
	struct m0_storage_devs *devs = m0_cs_storage_devs_get();
	int                     rc;

	rc = m0_storage_dev_stob_find(devs, &co->fco_stob_id, &co->fco_stob);
	if (rc == 0 && m0_stob_state_get(co->fco_stob) == CSS_NOENT) {
		m0_storage_dev_stob_put(devs, co->fco_stob);
		rc = M0_ERR(-ENOENT);
	}
	return M0_RC(rc);
}

static int cob_tick_prepare(struct m0_fom *fom)
{
	struct m0_fop               *fop;
	struct m0_fop_cob_common    *common;
	struct m0_fom_cob_op        *cob_op;
	int                          rc = 0;

	M0_PRE(fom != NULL);

	fop = fom->fo_fop;
	common = m0_cobfop_common_get(fop);
	cob_op = cob_fom_get(fom);
	/* pool machine check for meta-data cobs is not needed. */
	rc = cob_is_md(cob_op) ? 0 : cob_fom_pool_version_get(fom) ?:
	  m0_ios__poolmach_check(&cob_op->fco_pver->pv_mach,
			      (struct m0_poolmach_versions*)&common->c_version);

	if (rc == 0 || rc == M0_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH) {
		m0_fom_phase_set(fom, M0_FOPH_COB_OPS_EXECUTE);
	} else
		m0_fom_phase_move(fom, rc, M0_FOPH_FAILURE);
	return M0_RC(rc);
}

static void cob_tick_tail(struct m0_fom *fom,
			  struct m0_fop_cob_op_rep_common *r_common)
{
	struct m0_fop            *fop;
	struct m0_fop_cob_common *common;
	struct m0_fom_cob_op     *cob_op;
	struct m0_poolmach       *poolmach = NULL;

	M0_PRE(fom != NULL);

	fop = fom->fo_fop;
	common = m0_cobfop_common_get(fop);
	cob_op = cob_fom_get(fom);

	if (m0_fom_phase(fom) == M0_FOPH_SUCCESS ||
	    m0_fom_phase(fom) == M0_FOPH_FAILURE) {
		/* Piggyback some information about the transaction */
		m0_fom_mod_rep_fill(&r_common->cor_mod_rep, fom);
		if (cob_op->fco_pver != NULL) {
			poolmach = &cob_op->fco_pver->pv_mach;
			m0_ios_poolmach_version_updates_pack(poolmach,
						&common->c_version,
						&r_common->cor_fv_version,
						&r_common->cor_fv_updates);
		}
	}
}

static int cob_getattr_fom_tick(struct m0_fom *fom)
{
	struct m0_cob_attr               attr = { { 0, } };
	int                              rc = 0;
	struct m0_fom_cob_op            *cob_op;
	const char                      *ops;
	struct m0_fop                   *fop;
	struct m0_fop_cob_op_rep_common *r_common;
	struct m0_fop_cob_getattr_reply *reply;

	M0_PRE(fom != NULL);
	M0_PRE(fom->fo_ops != NULL);
	M0_PRE(fom->fo_type != NULL);

	cob_op = cob_fom_get(fom);
	M0_ENTRY("cob_getattr for "FID_F, FID_P(&cob_op->fco_gfid));

	fop = fom->fo_fop;
	reply = m0_fop_data(fom->fo_rep_fop);
	r_common = &reply->cgr_common;

	if (m0_fom_phase(fom) < M0_FOPH_NR) {
		rc = m0_fom_tick_generic(fom);
		return M0_RC(rc);
	}

	ops = m0_fop_name(fop);

	switch (m0_fom_phase(fom)) {
	case M0_FOPH_COB_OPS_PREPARE:
		M0_LOG(M0_DEBUG, "Cob %s operation prepare", ops);
		rc = cob_tick_prepare(fom);
		reply->cgr_rc = rc;
		return M0_FSO_AGAIN;
	case M0_FOPH_COB_OPS_EXECUTE:
		M0_LOG(M0_DEBUG, "Cob %s operation started for "FID_F,
		       ops, FID_P(&cob_op->fco_gfid));
		rc = cob_getattr(fom, cob_op, &attr);
		m0_md_cob_mem2wire(&reply->cgr_body, &attr);
		m0_fom_phase_moveif(fom, rc, M0_FOPH_SUCCESS, M0_FOPH_FAILURE);
	        M0_LOG(M0_DEBUG, "Cob %s operation finished with %d", ops, rc);
		break;
	default:
		M0_IMPOSSIBLE("Invalid phase for cob getattr fom.");
		rc = -EINVAL;
		m0_fom_phase_moveif(fom, rc, M0_FOPH_SUCCESS, M0_FOPH_FAILURE);
	}

	if (rc != 0)
		reply->cgr_rc = rc;
	cob_tick_tail(fom, r_common);
	return M0_RC(M0_FSO_AGAIN);
}

static int cob_setattr_fom_tick(struct m0_fom *fom)
{
	struct m0_cob_attr               attr = { { 0, } };
	int                              rc = 0;
	struct m0_fom_cob_op            *cob_op;
	const char                      *ops;
	struct m0_fop                   *fop;
	struct m0_fop_cob_common        *cs_common;
	struct m0_fop_cob_op_rep_common *r_common;
	struct m0_fop_cob_setattr_reply *reply;
	struct m0_be_tx_credit          *tx_cred;

	M0_PRE(fom != NULL);
	M0_PRE(fom->fo_ops != NULL);
	M0_PRE(fom->fo_type != NULL);

	cob_op = cob_fom_get(fom);
	M0_ENTRY("cob_setattr for "FID_F, FID_P(&cob_op->fco_gfid));

	fop = fom->fo_fop;
	cs_common = m0_cobfop_common_get(fop);
	reply = m0_fop_data(fom->fo_rep_fop);
	r_common = &reply->csr_common;
	ops = m0_fop_name(fop);

	if (m0_fom_phase(fom) < M0_FOPH_NR) {
		switch(m0_fom_phase(fom)) {
		case M0_FOPH_TXN_OPEN:
			tx_cred = m0_fom_tx_credit(fom);
			cob_op_credit(fom, M0_COB_OP_UPDATE, tx_cred);
			if (cob_op->fco_flags & M0_IO_FLAG_CROW)
				cob_stob_create_credit(fom);
			break;
		}
		rc = m0_fom_tick_generic(fom);
		return M0_RC(rc);
	}

	switch (m0_fom_phase(fom)) {
	case M0_FOPH_COB_OPS_PREPARE:
		M0_LOG(M0_DEBUG, "Cob %s operation prepare", ops);
		rc = cob_tick_prepare(fom);
		reply->csr_rc = rc;
		return M0_FSO_AGAIN;
	case M0_FOPH_COB_OPS_EXECUTE:
		M0_LOG(M0_DEBUG, "Cob %s operation started for "FID_F,
		       ops, FID_P(&cob_op->fco_gfid));
		m0_md_cob_wire2mem(&attr, &cs_common->c_body);
		m0_dump_cob_attr(&attr);
		rc = cob_setattr(fom, cob_op, &attr);
		m0_fom_phase_moveif(fom, rc, M0_FOPH_SUCCESS, M0_FOPH_FAILURE);
	        M0_LOG(M0_DEBUG, "Cob %s operation finished with %d", ops, rc);
		break;
	default:
		M0_IMPOSSIBLE("Invalid phase for cob setattr fom.");
		rc = -EINVAL;
		m0_fom_phase_moveif(fom, rc, M0_FOPH_SUCCESS, M0_FOPH_FAILURE);
	}

	if (rc != 0)
		reply->csr_rc = rc;
	cob_tick_tail(fom, r_common);
	return M0_RC(M0_FSO_AGAIN);
}

static bool cob_pool_version_mismatch(const struct m0_fom *fom)
{
	int                       rc;
	struct m0_cob            *cob;
	struct m0_fop_cob_common *common;

	common = m0_cobfop_common_get(fom->fo_fop);
	rc = cob_locate(fom, &cob);
	if (rc == 0 && cob != NULL) {
		M0_LOG(M0_DEBUG, "cob pver"FID_F", common pver"FID_F,
				FID_P(&cob->co_nsrec.cnr_pver),
				FID_P(&common->c_body.b_pver));
		return !m0_fid_eq(&cob->co_nsrec.cnr_pver,
				  &common->c_body.b_pver);
	}
	return false;
}

static void cob_stob_create_credit(struct m0_fom *fom)
{
	struct m0_fom_cob_op   *cob_op;
	struct m0_be_tx_credit *tx_cred;

	cob_op = cob_fom_get(fom);
	tx_cred = m0_fom_tx_credit(fom);
	if (!cob_is_md(cob_op))
		m0_cc_stob_cr_credit(&cob_op->fco_stob_id, tx_cred);
	cob_op_credit(fom, M0_COB_OP_CREATE, tx_cred);
	cob_op->fco_is_done = true;
}

static int cob_stob_create(struct m0_fom *fom, struct m0_cob_attr *attr)
{
	int                   rc = 0;
	struct m0_fom_cob_op *cob_op;

	cob_op = cob_fom_get(fom);
	if (!cob_is_md(cob_op))
		rc = m0_cc_stob_create(fom, &cob_op->fco_stob_id);
	return rc ?: cc_cob_create(fom, cob_op, attr);
}

static int cob_stob_delete_credit(struct m0_fom *fom)
{
	int                    rc = 0;
	struct m0_fom_cob_op  *cob_op;
	uint32_t               fop_type;
	struct m0_be_tx_credit *tx_cred;
	struct m0_be_tx_credit cob_op_tx_credit = {};

	cob_op = cob_fom_get(fom);
	tx_cred = m0_fom_tx_credit(fom);
	fop_type = cob_op->fco_fop_type;
	if (cob_is_md(cob_op)) {
		cob_op_credit(fom, M0_COB_OP_DELETE, tx_cred);
		if (cob_op->fco_recreate)
			cob_op_credit(fom, M0_COB_OP_CREATE, tx_cred);
		cob_op->fco_is_done = true;
		return M0_RC(rc);
	}
	rc = ce_stob_edit_credit(fom, cob_op, tx_cred, fop_type);
	if (rc == 0) {
		M0_SET0(&cob_op_tx_credit);
		cob_op_credit(fom, fop_type, &cob_op_tx_credit);
		if (cob_op->fco_recreate)
			cob_stob_create_credit(fom);
		if (!m0_be_should_break(m0_fom_tx(fom)->t_engine, tx_cred,
					&cob_op_tx_credit)) {
			m0_be_tx_credit_add(tx_cred, &cob_op_tx_credit);
			cob_op->fco_is_done = true;
		}
	}
	return M0_RC(rc);
}

static int cob_ops_fom_tick(struct m0_fom *fom)
{
	struct m0_fom_cob_op            *cob_op;
	struct m0_fop_cob_common        *common;
	struct m0_fop_cob_truncate      *ct;
	struct m0_cob_attr               attr = { { 0, } };
	int                              rc = 0;
	uint32_t                         fop_type;
	const char                      *ops;
	struct m0_fop                   *fop;
	struct m0_fop_cob_op_rep_common *r_common;
	struct m0_fop_cob_op_reply      *reply;

	M0_PRE(fom != NULL);
	M0_PRE(fom->fo_ops != NULL);
	M0_PRE(fom->fo_type != NULL);

	fop = fom->fo_fop;
	common = m0_cobfop_common_get(fop);
	reply = m0_fop_data(fom->fo_rep_fop);
	r_common = &reply->cor_common;
	cob_op = cob_fom_get(fom);
	fop_type = cob_op->fco_fop_type;
	if (m0_fom_phase(fom) < M0_FOPH_NR) {
		switch (m0_fom_phase(fom)) {
		case M0_FOPH_INIT:
			/* Check if cob with different pool version exists. */
			if (fop_type == M0_COB_OP_CREATE &&
			    cob_pool_version_mismatch(fom)) {
				M0_CNT_DEC(common->c_body.b_nlink);
				fop_type = cob_op->fco_fop_type =
					M0_COB_OP_DELETE;
				cob_op->fco_recreate = true;
			}

			if (fop_type == M0_COB_OP_CREATE)
				break;
			/* Check if the truncation size is valid. */
			if (fop_type == M0_COB_OP_TRUNCATE) {
				ct = m0_fop_data(fom->fo_fop);
				if (ct->ct_size != 0) {
					cob_op->fco_is_done = true;
					rc = -EINVAL;
					m0_fom_phase_move(fom, rc,
							  M0_FOPH_FAILURE);
					goto tail;
				}
			}
			/*
			 * Find the stob and handle non-existing stob error
			 * earlier, before initialising the transaction.
			 * This avoids complications in handling transaction
			 * cleanup.
			 */
			rc = cob_is_md(cob_op) ? 0 : cob_ops_stob_find(cob_op);
			if (rc != 0) {
				if (rc == -ENOENT &&
				    cob_op->fco_flags & M0_IO_FLAG_CROW) {
					/* nothing to delete or truncate */
					M0_ASSERT(M0_IN(fop_type,
							(M0_COB_OP_DELETE,
							 M0_COB_OP_TRUNCATE)));
					rc = 0;
					m0_fom_phase_move(fom, rc,
							  M0_FOPH_SUCCESS);
				} else {
					m0_fom_phase_move(fom, rc,
							  M0_FOPH_FAILURE);
				}
				cob_op->fco_is_done = true;
				goto tail;
			}
			break;
		case M0_FOPH_TXN_OPEN:
			switch (fop_type) {
			case M0_COB_OP_CREATE:
				cob_stob_create_credit(fom);
				break;
			case M0_COB_OP_DELETE:
			case M0_COB_OP_TRUNCATE:
				rc = cob_stob_delete_credit(fom);
				rc = rc == -EAGAIN ? 0 : rc;
				break;
			default:
				M0_IMPOSSIBLE("Invalid fop type!");
				break;
			}
			break;
		case M0_FOPH_QUEUE_REPLY:
			/*
			 * When an operation can't be done in a single
			 * transaction due to insufficient credits, it is split
			 * into multiple trasactions.
			 * As the the operation is incomplete, skip the sending
			 * of reply and reinitialise the trasaction.
			 * i.e move fom phase to M0_FOPH_TXN_COMMIT_WAIT and
			 * then to M0_FOPH_TXN_INIT.
			 */
			if (!cob_op->fco_is_done && m0_fom_rc(fom) == 0) {
				m0_fom_phase_set(fom, M0_FOPH_TXN_COMMIT_WAIT);
				return M0_FSO_AGAIN;
			}
			break;
		case M0_FOPH_TXN_COMMIT_WAIT:
			if (!cob_op->fco_is_done && m0_fom_rc(fom) == 0) {
				rc = m0_fom_tx_commit_wait(fom);
				if (rc == M0_FSO_AGAIN) {
					M0_SET0(m0_fom_tx(fom));
					m0_fom_phase_set(fom, M0_FOPH_TXN_INIT);
				} else
					return M0_RC(rc);
			}
			break;
		}
		rc = m0_fom_tick_generic(fom);
		return M0_RC(rc);
	}
	ops = m0_fop_name(fop);

	switch (m0_fom_phase(fom)) {
	case M0_FOPH_COB_OPS_PREPARE:
		rc = cob_tick_prepare(fom);
		reply->cor_rc = rc;
		return M0_RC(M0_FSO_AGAIN);
	case M0_FOPH_COB_OPS_EXECUTE:
		fop_type = cob_op->fco_fop_type;
		M0_LOG(M0_DEBUG, "Cob %s operation for "FID_F"/%x "FID_F" for %s",
				ops, FID_P(&cob_op->fco_cfid),
				cob_op->fco_cob_idx,
				FID_P(&cob_op->fco_gfid),
				cob_is_md(cob_op) ? "MD" : "IO");
		m0_md_cob_wire2mem(&attr, &common->c_body);
		if (fop_type == M0_COB_OP_CREATE) {
			rc = cob_stob_create(fom, &attr);
		} else if (fop_type == M0_COB_OP_DELETE) {
			if (cob_op->fco_is_done) {
				rc = cob_is_md(cob_op) ? 0 :
					ce_stob_edit(fom, cob_op,
						     M0_COB_OP_DELETE);
				rc = rc ?: cd_cob_delete(fom, cob_op, &attr);
				if (rc == 0 && cob_op->fco_recreate) {
					cob_op->fco_fop_type = M0_COB_OP_CREATE;
					M0_CNT_INC(attr.ca_nlink);
					rc = cob_stob_create(fom, &attr);
				}
			} else
				rc = ce_stob_edit(fom, cob_op,
						  M0_COB_OP_TRUNCATE);
		} else {
			rc = ce_stob_edit(fom, cob_op, M0_COB_OP_TRUNCATE);
			if (cob_op->fco_is_done) {
				m0_storage_dev_stob_put(m0_cs_storage_devs_get(),
							cob_op->fco_stob);
			}
		}

		m0_fom_phase_moveif(fom, rc, M0_FOPH_SUCCESS, M0_FOPH_FAILURE);
	        M0_LOG(M0_DEBUG, "Cob %s operation finished with %d", ops, rc);
		break;
	default:
		M0_IMPOSSIBLE("Invalid phase for cob create/delete fom.");
		rc = -EINVAL;
		m0_fom_phase_moveif(fom, rc, M0_FOPH_SUCCESS, M0_FOPH_FAILURE);
	}

tail:
	if (rc != 0)
		reply->cor_rc = rc;
	cob_tick_tail(fom, r_common);
	return M0_RC(M0_FSO_AGAIN);
}

M0_INTERNAL int m0_cc_stob_cr_credit(struct m0_stob_id *sid,
				     struct m0_be_tx_credit *accum)
{
	struct m0_stob_domain *sdom;

	M0_ENTRY("stob_fid="FID_F, FID_P(&sid->si_fid));

	sdom = m0_stob_domain_find_by_stob_id(sid);
	if (sdom == NULL) {
		return M0_ERR(-EINVAL);
	}

	m0_stob_create_credit(sdom, accum);

	return M0_RC(0);
}

M0_INTERNAL int m0_cc_stob_create(struct m0_fom *fom, struct m0_stob_id *sid)
{
	int rc;

	M0_ENTRY("stob create fid="FID_F, FID_P(&sid->si_fid));
	rc = m0_storage_dev_stob_create(m0_cs_storage_devs_get(),
					sid, &fom->fo_tx);
	return M0_RC(rc);
}

static struct m0_cob_domain *cdom_get(const struct m0_fom *fom)
{
	struct m0_reqh_io_service *ios;

	M0_PRE(fom != NULL);

	ios = container_of(fom->fo_service, struct m0_reqh_io_service,
			   rios_gen);

	return ios->rios_cdom;
}

M0_INTERNAL int m0_cc_cob_nskey_make(struct m0_cob_nskey **nskey,
				     const struct m0_fid *gfid,
				     uint32_t cob_idx)
{
	char     nskey_name[M0_FID_STR_LEN] = { 0 };
	uint32_t nskey_name_len;

	M0_PRE(m0_fid_is_set(gfid));

	nskey_name_len = sprintf(nskey_name, "%u", cob_idx);

	return m0_cob_nskey_make(nskey, gfid, nskey_name, nskey_name_len);
}

static int cc_md_cob_nskey_make(struct m0_cob_nskey **nskey,
				const struct m0_fid *gfid)
{
	M0_PRE(m0_fid_is_set(gfid));

	return m0_cob_nskey_make(nskey, gfid, (const char*)gfid, sizeof *gfid);
}

static void cob_op_credit(struct m0_fom *fom, enum m0_cob_op opcode,
			  struct m0_be_tx_credit *accum)
{
	struct m0_cob_domain *cdom;

	M0_PRE(fom != NULL);
	cdom = cdom_get(fom);
	M0_ASSERT(cdom != NULL);
	m0_cob_tx_credit(cdom, opcode, accum);
}

enum cob_attr_operation {
	COB_ATTR_GET,
	COB_ATTR_SET
};

static int cob_attr_get(struct m0_cob        *cob,
			struct m0_cob_attr   *attr)
{
	/* copy attr from cob to @attr */
	M0_SET0(attr);
	attr->ca_valid = 0;
	attr->ca_tfid = cob->co_nsrec.cnr_fid;
	attr->ca_pfid = cob->co_nskey->cnk_pfid;

	/*
	 * Copy permissions and owner info into rep.
	 */
	if (cob->co_flags & M0_CA_OMGREC) {
		attr->ca_valid |= M0_COB_UID | M0_COB_GID | M0_COB_MODE;
		attr->ca_uid  = cob->co_omgrec.cor_uid;
		attr->ca_gid  = cob->co_omgrec.cor_gid;
		attr->ca_mode = cob->co_omgrec.cor_mode;
	}

	/*
	 * Copy nsrec fields into response.
	 */
	if (cob->co_flags & M0_CA_NSREC) {
		attr->ca_valid |= M0_COB_ATIME | M0_COB_CTIME   | M0_COB_MTIME |
				  M0_COB_SIZE  | M0_COB_BLKSIZE | M0_COB_BLOCKS|
				  M0_COB_LID | M0_COB_PVER;
		attr->ca_atime   = cob->co_nsrec.cnr_atime;
		attr->ca_ctime   = cob->co_nsrec.cnr_ctime;
		attr->ca_mtime   = cob->co_nsrec.cnr_mtime;
		attr->ca_blksize = cob->co_nsrec.cnr_blksize;
		attr->ca_blocks  = cob->co_nsrec.cnr_blocks;
		attr->ca_nlink   = cob->co_nsrec.cnr_nlink;
		attr->ca_size    = cob->co_nsrec.cnr_size;
		attr->ca_lid     = cob->co_nsrec.cnr_lid;
		attr->ca_pver    = cob->co_nsrec.cnr_pver;
	}
	return 0;
}

static int cob_locate(const struct m0_fom *fom, struct m0_cob **cob_out)
{
	struct m0_cob_oikey   oikey;
	struct m0_cob_domain *cdom;
	struct m0_fid         fid;
	struct m0_fom_cob_op *cob_op;
	struct m0_cob        *cob;
	int                   rc;

	cob_op = cob_fom_get(fom);
	M0_ASSERT(cob_op != NULL);
	cdom = cdom_get(fom);
	M0_ASSERT(cdom != NULL);
	cob_fom_stob2fid_map(cob_op, &fid);
	m0_cob_oikey_make(&oikey, &fid, 0);
	rc = m0_cob_locate(cdom, &oikey, 0, &cob);
	if (rc == 0)
		*cob_out = cob;
	return rc;
}

static int cob_attr_op(struct m0_fom          *fom,
		       struct m0_fom_cob_op   *gop,
		       struct m0_cob_attr     *attr,
		       enum cob_attr_operation op)
{
	int              rc;
	struct m0_cob   *cob;
	struct m0_be_tx *tx;
	uint32_t valid = attr->ca_valid;

	M0_PRE(fom != NULL);
	M0_PRE(gop != NULL);
	M0_PRE(op == COB_ATTR_GET || op == COB_ATTR_SET);

	M0_LOG(M0_DEBUG, "cob attr for "FID_F"/%x "FID_F" %s",
			 FID_P(&gop->fco_cfid), gop->fco_cob_idx,
			 FID_P(&gop->fco_gfid),
			 cob_is_md(gop) ? "MD" : "IO");

	rc = cob_locate(fom, &cob);
	if (rc != 0) {
		if (valid & M0_COB_NLINK)
			M0_LOG(M0_DEBUG, "nlink = %u", attr->ca_nlink);
		/*
		 * CROW setattr must have non-zero nlink set
		 * to avoid creation of invalid cobs.
		 */
		if (rc != -ENOENT || !(gop->fco_flags & M0_IO_FLAG_CROW) ||
		    !(valid & M0_COB_NLINK) || attr->ca_nlink == 0)
			return M0_RC(rc);
		M0_ASSERT(op == COB_ATTR_SET);
		rc = cob_stob_create(fom, attr) ?: cob_locate(fom, &cob);
		if (rc != 0)
			return M0_RC(rc);
	}

	M0_ASSERT(cob != NULL);
	M0_ASSERT(cob->co_nsrec.cnr_nlink != 0);
	switch (op) {
	case COB_ATTR_GET:
		rc = cob_attr_get(cob, attr);
		M0_ASSERT(ergo(attr->ca_valid & M0_COB_PVER,
			       m0_fid_is_set(&attr->ca_pver) &&
			       m0_fid_is_valid(&attr->ca_pver)));
		break;
	case COB_ATTR_SET:
		tx = m0_fom_tx(fom);
		rc = m0_cob_setattr(cob, attr, tx);
		break;
	}
	m0_cob_put(cob);

	M0_LOG(M0_DEBUG, "Cob attr: %d rc: %d", op, rc);
	return M0_RC(rc);
}

static int cob_getattr(struct m0_fom        *fom,
		       struct m0_fom_cob_op *gop,
		       struct m0_cob_attr   *attr)
{
	return cob_attr_op(fom, gop, attr, COB_ATTR_GET);
}

static int cob_setattr(struct m0_fom        *fom,
		       struct m0_fom_cob_op *gop,
		       struct m0_cob_attr   *attr)
{
	return cob_attr_op(fom, gop, attr, COB_ATTR_SET);
}

static int cc_cob_create(struct m0_fom            *fom,
			 struct m0_fom_cob_op     *cc,
			 const struct m0_cob_attr *attr)
{
	struct m0_cob_domain *cdom;
	struct m0_be_tx	     *tx;
	int                   rc;

	M0_PRE(fom != NULL);
	M0_PRE(cc != NULL);

	cdom = cdom_get(fom);
	M0_ASSERT(cdom != NULL);
	tx = m0_fom_tx(fom);
	rc = m0_cc_cob_setup(cc, cdom, attr, tx);

	return M0_RC(rc);
}

M0_INTERNAL int m0_cc_cob_setup(struct m0_fom_cob_op     *cc,
				struct m0_cob_domain     *cdom,
				const struct m0_cob_attr *attr,
				struct m0_be_tx	         *ctx)
{
	int		      rc;
	struct m0_cob	     *cob;
	struct m0_cob_nskey  *nskey = NULL;
	struct m0_cob_nsrec   nsrec = {};

	M0_PRE(cc != NULL);
	M0_PRE(cdom != NULL);

	rc = m0_cob_alloc(cdom, &cob);
	if (rc != 0)
		return M0_RC(rc);

	rc = cob_is_md(cc) ?
		cc_md_cob_nskey_make(&nskey, &cc->fco_gfid) :
		m0_cc_cob_nskey_make(&nskey, &cc->fco_gfid, cc->fco_cob_idx);
	if (rc != 0) {
		m0_cob_put(cob);
		return M0_RC(rc);
	}

	cob_fom_stob2fid_map(cc, &nsrec.cnr_fid);
	m0_cob_nsrec_init(&nsrec);
	nsrec.cnr_nlink   = attr->ca_nlink;
	nsrec.cnr_size    = attr->ca_size;
	nsrec.cnr_blksize = attr->ca_blksize;
	nsrec.cnr_blocks  = attr->ca_blocks;
	nsrec.cnr_atime   = attr->ca_atime;
	nsrec.cnr_mtime   = attr->ca_mtime;
	nsrec.cnr_ctime   = attr->ca_ctime;
	nsrec.cnr_lid     = attr->ca_lid;
	nsrec.cnr_pver    = attr->ca_pver;

	rc = m0_cob_create(cob, nskey, &nsrec, NULL, NULL, ctx);
	if (rc != 0) {
	        /*
	         * Cob does not free nskey and fab rec on errors. We need to do
		 * so ourself. In case cob created successfully, it frees things
		 * on last put.
	         */
		m0_free(nskey);
	}
	m0_cob_put(cob);

	return M0_RC(rc);
}

static void cd_fom_fini(struct m0_fom *fom)
{
	struct m0_fom_cob_op *cfom;

	M0_PRE(fom != NULL);

	cfom = cob_fom_get(fom);
	m0_fom_fini(fom);
	m0_free(cfom);
}

static void cob_getattr_fom_fini(struct m0_fom *fom)
{
	struct m0_fom_cob_op *cfom;
	M0_PRE(fom != NULL);

	cfom = cob_fom_get(fom);
	m0_fom_fini(fom);
	m0_free(cfom);
}

static void cob_setattr_fom_fini(struct m0_fom *fom)
{
	struct m0_fom_cob_op *cfom;
	M0_PRE(fom != NULL);

	cfom = cob_fom_get(fom);
	m0_fom_fini(fom);
	m0_free(cfom);
}

static int cd_cob_delete(struct m0_fom            *fom,
			 struct m0_fom_cob_op     *cd,
			 const struct m0_cob_attr *attr)
{
	int                   rc;
	struct m0_cob        *cob;

	M0_PRE(fom != NULL);
	M0_PRE(cd != NULL);

        M0_LOG(M0_DEBUG, "Deleting cob for "FID_F"/%x",
	       FID_P(&cd->fco_cfid), cd->fco_cob_idx);

	rc = cob_locate(fom, &cob);
	if (rc != 0)
		return M0_RC(rc);

	M0_ASSERT(cob != NULL);
	M0_CNT_DEC(cob->co_nsrec.cnr_nlink);
	M0_ASSERT(attr->ca_nlink == 0);
	M0_ASSERT(cob->co_nsrec.cnr_nlink == 0);

	rc = m0_cob_delete(cob, m0_fom_tx(fom));
	if (rc == 0)
		M0_LOG(M0_DEBUG, "Cob deleted successfully.");

	return M0_RC(rc);
}

static int ce_stob_edit_credit(struct m0_fom *fom, struct m0_fom_cob_op *cc,
			       struct m0_be_tx_credit *accum, uint32_t cot)
{
	struct m0_stob *stob;
	int             rc;

	M0_PRE(M0_IN(cot, (M0_COB_OP_DELETE, M0_COB_OP_TRUNCATE)));

	stob = cc->fco_stob;
	M0_ASSERT(stob != NULL);
	M0_ASSERT(m0_stob_state_get(stob) == CSS_EXISTS);
	rc = cot == M0_COB_OP_TRUNCATE ? m0_stob_punch_credit(stob, accum) :
					 m0_stob_destroy_credit(stob, accum);
	return M0_RC(rc);
}

static int ce_stob_edit(struct m0_fom *fom, struct m0_fom_cob_op *cd,
			uint32_t cot)
{
	struct m0_storage_devs *devs = m0_cs_storage_devs_get();
	struct m0_stob         *stob = cd->fco_stob;
	struct m0_indexvec      range;
	int                     rc;

	M0_ASSERT(stob != NULL);
	rc = cot == M0_COB_OP_DELETE ?
			m0_storage_dev_stob_destroy(devs, stob, &fom->fo_tx) :
			m0_indexvec_universal_set(&range) ?:
			m0_stob_punch(stob, &range, &fom->fo_tx);
	return M0_RC(rc);
}

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
