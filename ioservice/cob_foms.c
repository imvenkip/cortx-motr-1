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
 * Original author: Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 * Original creation date: 02/07/2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_COB
#include <sys/stat.h>    /* S_ISDIR */
#include "lib/errno.h"
#include "lib/memory.h"             /* m0_free(), M0_ALLOC_PTR() */
#include "lib/misc.h"               /* M0_SET0() */
#include "lib/trace.h"
#include "fid/fid.h"                /* m0_fid */
#include "cob/ns_iter.h"
#include "fop/fom_generic.h"        /* m0_fom_tick_generic() */
#include "ioservice/io_foms.h"      /* io_fom_cob_rw_fid2stob_map */
#include "ioservice/io_fops.h"      /* m0_cobfop_common_get */
#include "ioservice/cob_foms.h"     /* m0_fom_cob_create, m0_fom_cob_delete */
#include "ioservice/io_fops.h"      /* m0_is_cob_create_fop() */
#include "mdstore/mdstore.h"
#include "ioservice/io_device.h"   /* m0_ios_poolmach_get() */
#include "reqh/reqh_service.h"
#include "pool/pool.h"
#include "mero/mero_setup.h"
#include "ioservice/io_fops_ff.h"

/* Forward Declarations. */
static int  cob_fom_create(struct m0_fop *fop, struct m0_fom **out);
static void cc_fom_fini(struct m0_fom *fom);
static int  cc_fom_tick(struct m0_fom *fom);
static int  cc_stob_create(struct m0_fom *fom, struct m0_fom_cob_op *cc);
static int  cc_cob_create(struct m0_fom *fom, struct m0_fom_cob_op *cc);

static void cd_fom_fini(struct m0_fom *fom);
static int  cd_fom_tick(struct m0_fom *fom);
static int  cd_cob_delete(struct m0_fom *fom, struct m0_fom_cob_op *cd);
static int  cd_stob_delete(struct m0_fom *fom, struct m0_fom_cob_op *cd);

static void   cob_fom_populate(struct m0_fom *fom);
static int    cob_op_fom_create(struct m0_fom **out);
static size_t cob_fom_locality_get(const struct m0_fom *fom);
static inline struct m0_fom_cob_op *cob_fom_get(struct m0_fom *fom);

enum {
	CC_COB_VERSION_INIT	= 0,
	CC_COB_HARDLINK_NR	= 1,
	CD_FOM_STOBIO_LAST_REFS = 1,
};

static const struct m0_addb_loc cc_fom_addb_loc = {
	.al_name = "create_cob_fom",
};

M0_ADDB_EV_DEFINE(cc_fom_func_fail, "create cob func failed.",
		  M0_ADDB_EVENT_FUNC_FAIL, M0_ADDB_FUNC_CALL);

/** Cob create fom ops. */
static const struct m0_fom_ops cc_fom_ops = {
	.fo_fini	  = cc_fom_fini,
	.fo_tick	  = cc_fom_tick,
	.fo_home_locality = cob_fom_locality_get,
};

/** Common fom_type_ops for m0_fop_cob_create and m0_fop_cob_delete fops. */
const struct m0_fom_type_ops cob_fom_type_ops = {
	.fto_create = cob_fom_create,
};

static const struct m0_addb_loc cd_fom_addb_loc = {
	.al_name = "cob_delete_fom",
};

M0_ADDB_EV_DEFINE(cd_fom_func_fail, "cob delete fom func failed.",
		  M0_ADDB_EVENT_FUNC_FAIL, M0_ADDB_FUNC_CALL);

/** Cob delete fom ops. */
static const struct m0_fom_ops cd_fom_ops = {
	.fo_fini	  = cd_fom_fini,
	.fo_tick	  = cd_fom_tick,
	.fo_home_locality = cob_fom_locality_get,
};

static int cob_fom_create(struct m0_fop *fop, struct m0_fom **out)
{
	int			  rc;
	struct m0_fop            *rfop;
	struct m0_fom		 *fom;
	const struct m0_fom_ops  *fom_ops;
	struct m0_fom_cob_op     *cfom;

	M0_PRE(fop != NULL);
	M0_PRE(fop->f_type != NULL);
	M0_PRE(out != NULL);
	M0_PRE(m0_is_cob_create_delete_fop(fop));

	rc = cob_op_fom_create(out);
	cfom = cob_fom_get(*out);
	if (rc != 0) {
		M0_ADDB_ADD(&fop->f_addb, &cc_fom_addb_loc, cc_fom_func_fail,
			    "Failed to create cob_create fom.", rc);
		return rc;
	}
	fom = *out;
	M0_ASSERT(fom != NULL);

	fom_ops = m0_is_cob_create_fop(fop) ? &cc_fom_ops : &cd_fom_ops;
	rfop = m0_fop_alloc(&m0_fop_cob_op_reply_fopt, NULL);
	if (rfop == NULL) {
		M0_ADDB_ADD(&fop->f_addb, &cc_fom_addb_loc, m0_addb_oom);
		m0_free(cfom);
		return -ENOMEM;
	}

	m0_fom_init(fom, &fop->f_type->ft_fom_type, fom_ops, fop, rfop);
	cob_fom_populate(fom);
	return rc;
}

static int cob_op_fom_create(struct m0_fom **out)
{
	struct m0_fom_cob_op *cfom;

	M0_PRE(out != NULL);

	M0_ALLOC_PTR(cfom);
	if (cfom == NULL)
		return -ENOMEM;

	*out = &cfom->fco_fom;
	return 0;
}

static inline struct m0_fom_cob_op *cob_fom_get(struct m0_fom *fom)
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

	return m0_fop_opcode(fom->fo_fop);
}

static void cob_fom_populate(struct m0_fom *fom)
{
	struct m0_fom_cob_op     *cfom;
	struct m0_fop_cob_common *common;

	M0_PRE(fom != NULL);
	M0_PRE(fom->fo_fop != NULL);

	cfom = cob_fom_get(fom);
	common = m0_cobfop_common_get(fom->fo_fop);

	cfom->fco_gfid = common->c_gobfid;
	cfom->fco_cfid = common->c_cobfid;
	io_fom_cob_rw_fid2stob_map(&cfom->fco_cfid, &cfom->fco_stobid);
	cfom->fco_cob_idx = common->c_cob_idx;
}

static int cc_fom_tick(struct m0_fom *fom)
{
	int                             rc;
	struct m0_fom_cob_op           *cc;
	struct m0_fop_cob_op_reply     *reply;
	struct m0_poolmach             *poolmach;
	struct m0_reqh                 *reqh;
	struct m0_pool_version_numbers *verp;
	struct m0_pool_version_numbers  curr;
	struct m0_fop_cob_create *fop;


	M0_PRE(fom != NULL);
	M0_PRE(fom->fo_ops != NULL);
	M0_PRE(fom->fo_type != NULL);

	if (m0_fom_phase(fom) < M0_FOPH_NR) {
		rc = m0_fom_tick_generic(fom);
		return rc;
	}

	fop  = m0_fop_data(fom->fo_fop);
	reqh = fom->fo_loc->fl_dom->fd_reqh;
	poolmach = m0_ios_poolmach_get(reqh);
	m0_poolmach_current_version_get(poolmach, &curr);
	verp = (struct m0_pool_version_numbers*)&fop->cc_common.c_version;

	/* Check the client version and server version before any processing */
	if (!m0_poolmach_version_equal(verp, &curr)) {
		rc = M0_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH;
		goto out;
	}

        M0_LOG(M0_DEBUG, "Cob operation started");
	if (m0_fom_phase(fom) == M0_FOPH_CC_COB_CREATE) {
		cc = cob_fom_get(fom);

		rc = cc_stob_create(fom, cc);
		if (rc != 0) {
                        M0_LOG(M0_DEBUG, "Stob create failed with %d", rc);
			goto out;
		}

		rc = cc_cob_create(fom, cc);
	} else
		M0_IMPOSSIBLE("Invalid phase for cob create fom.");

out:
        M0_LOG(M0_DEBUG, "Cob operation finished with %d", rc);
	reply = m0_fop_data(fom->fo_rep_fop);
	reply->cor_rc = rc;

	m0_ios_poolmach_version_updates_pack(poolmach,
					     &fop->cc_common.c_version,
					     &reply->cor_fv_version,
					     &reply->cor_fv_updates);

	m0_fom_phase_moveif(fom, rc, M0_FOPH_SUCCESS, M0_FOPH_FAILURE);
	return M0_FSO_AGAIN;
}

static int cc_stob_create(struct m0_fom *fom, struct m0_fom_cob_op *cc)
{
	int                    rc;
	struct m0_stob        *stob;
	struct m0_reqh        *reqh;
	struct m0_stob_domain *sdom;

	M0_PRE(fom != NULL);
	M0_PRE(cc != NULL);

	reqh = m0_fom_reqh(fom);
	sdom = m0_cs_stob_domain_find(reqh, &cc->fco_stobid);
	if (sdom == NULL) {
		M0_LOG(M0_DEBUG, "can't find domain for stob_id=%lu",
		                 (unsigned long)cc->fco_stobid.si_bits.u_hi);
		M0_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    cc_fom_func_fail,
			    "Stob creation failed in cc_stob_create().",
			    -EINVAL);
		return -EINVAL;
	}

	rc = m0_stob_create_helper(sdom, &fom->fo_tx, &cc->fco_stobid, &stob);
	if (rc != 0) {
	        M0_LOG(M0_DEBUG, "m0_stob_create_helper() failed with %d", rc);
		M0_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    cc_fom_func_fail,
			    "Stob creation failed in cc_stob_create().", rc);
	} else {
		M0_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    m0_addb_trace, "Stob created successfully.");
		m0_stob_put(stob);
	}

	return rc;
}

static int cc_cob_nskey_make(struct m0_cob_nskey **nskey,
			     const struct m0_fid *gfid,
			     uint32_t cob_idx)
{
	char     nskey_name[UINT32_MAX_STR_LEN];
	uint32_t nskey_name_len;
	int      rc;

	M0_PRE(m0_fid_is_set(gfid));

	M0_SET_ARR0(nskey_name);
	snprintf((char*)nskey_name, UINT32_MAX_STR_LEN, "%u",
		 (uint32_t)cob_idx);

	nskey_name_len = strlen(nskey_name);

	rc = m0_cob_nskey_make(nskey, gfid, (char *)nskey_name, nskey_name_len);
        if (rc == -ENOMEM || nskey == NULL)
		return -ENOMEM;

	return 0;
}

static int cc_cob_create(struct m0_fom *fom, struct m0_fom_cob_op *cc)
{
	int			  rc;
	struct m0_cob		 *cob;
	struct m0_cob_domain	 *cdom;
	struct m0_fop_cob_create *fop;
	struct m0_cob_nskey	 *nskey;
	struct m0_cob_nsrec	  nsrec;
	struct m0_cob_fabrec	 *fabrec;
	struct m0_cob_omgrec      omgrec;

	M0_PRE(fom != NULL);
	M0_PRE(cc != NULL);
	M0_SET0(&nsrec);

	cdom = &fom->fo_loc->fl_dom->fd_reqh->rh_mdstore->md_dom;
	M0_ASSERT(cdom != NULL);
	fop = m0_fop_data(fom->fo_fop);

        rc = m0_cob_alloc(cdom, &cob);
        if (rc)
                return rc;

	rc = cc_cob_nskey_make(&nskey, &cc->fco_gfid, cc->fco_cob_idx);
	if (rc != 0) {
		M0_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    m0_addb_oom);
	        m0_cob_put(cob);
		return rc;
	}

        io_fom_cob_rw_stob2fid_map(&cc->fco_stobid, &nsrec.cnr_fid);
	nsrec.cnr_nlink = CC_COB_HARDLINK_NR;

	rc = m0_cob_fabrec_make(&fabrec, NULL, 0);
	if (rc) {
		m0_free(nskey);
		m0_cob_put(cob);
		return rc;
	}

	fabrec->cfb_version.vn_lsn =
	             m0_fol_lsn_allocate(m0_fom_reqh(fom)->rh_fol);
	fabrec->cfb_version.vn_vc = CC_COB_VERSION_INIT;

        omgrec.cor_uid = 0;
        omgrec.cor_gid = 0;
        omgrec.cor_mode = S_IFDIR |
                          S_IRUSR | S_IWUSR | S_IXUSR | /* rwx for owner */
                          S_IRGRP | S_IXGRP |           /* r-x for group */
                          S_IROTH | S_IXOTH;            /* r-x for others */

	rc = m0_cob_create(cob, nskey, &nsrec, fabrec, &omgrec, &fom->fo_tx.tx_dbtx);
	if (rc) {
	        /*
	         * Cob does not free nskey and fab rec on errors. We need to do so
	         * ourself. In case cob created successfully, it frees things on
	         * last put.
	         */
		m0_free(nskey);
		m0_free(fabrec);
		M0_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    cc_fom_func_fail, "Cob creation failed", rc);
	} else {
		M0_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    m0_addb_trace, "Cob created successfully.");
        }
	m0_cob_put(cob);

	return rc;
}

static void cd_fom_fini(struct m0_fom *fom)
{
	struct m0_fom_cob_op *cfom;

	M0_PRE(fom != NULL);

	cfom = cob_fom_get(fom);

	m0_fom_fini(fom);
	m0_free(cfom);
}

static int cd_fom_tick(struct m0_fom *fom)
{
	int                             rc;
	struct m0_fom_cob_op           *cd;
	struct m0_fop_cob_op_reply     *reply;
	struct m0_poolmach             *poolmach;
	struct m0_reqh                 *reqh;
	struct m0_pool_version_numbers *verp;
	struct m0_pool_version_numbers  curr;
	struct m0_fop_cob_delete       *fop;

	M0_PRE(fom != NULL);
	M0_PRE(fom->fo_ops != NULL);
	M0_PRE(fom->fo_type != NULL);

	reqh = fom->fo_loc->fl_dom->fd_reqh;

	if (m0_fom_phase(fom) < M0_FOPH_NR) {
		rc = m0_fom_tick_generic(fom);
		return rc;
	}

	fop  = m0_fop_data(fom->fo_fop);
	reqh = fom->fo_loc->fl_dom->fd_reqh;
	poolmach = m0_ios_poolmach_get(reqh);
	m0_poolmach_current_version_get(poolmach, &curr);
	verp = (struct m0_pool_version_numbers*)&fop->cd_common.c_version;

	/* Check the client version and server version before any processing */
	if (!m0_poolmach_version_equal(verp, &curr)) {
		rc = M0_IOP_ERROR_FAILURE_VECTOR_VER_MISMATCH;
		goto out;
	}

	if (m0_fom_phase(fom) == M0_FOPH_CD_COB_DEL) {
		cd = cob_fom_get(fom);

		rc = cd_cob_delete(fom, cd);
		if (rc != 0)
			goto out;

		rc = cd_stob_delete(fom, cd);
	} else
		M0_IMPOSSIBLE("Invalid phase for cob delete fom.");

out:
	reply = m0_fop_data(fom->fo_rep_fop);
	reply->cor_rc = rc;

	m0_ios_poolmach_version_updates_pack(poolmach,
					     &fop->cd_common.c_version,
					     &reply->cor_fv_version,
					     &reply->cor_fv_updates);

	m0_fom_phase_moveif(fom, rc, M0_FOPH_SUCCESS, M0_FOPH_FAILURE);
	return M0_FSO_AGAIN;
}

static int cd_cob_delete(struct m0_fom *fom, struct m0_fom_cob_op *cd)
{
	int                   rc;
	struct m0_cob_oikey   oikey;
	struct m0_cob        *cob;
	struct m0_cob_domain *cdom;
	struct m0_fid         fid;

	M0_PRE(fom != NULL);
	M0_PRE(cd != NULL);

	cdom = &fom->fo_loc->fl_dom->fd_reqh->rh_mdstore->md_dom;
	M0_ASSERT(cdom != NULL);

        io_fom_cob_rw_stob2fid_map(&cd->fco_stobid, &fid);
        m0_cob_oikey_make(&oikey, &fid, 0);
	rc = m0_cob_locate(cdom, &oikey, 0, &cob, &fom->fo_tx.tx_dbtx);
	if (rc != 0) {
		M0_ADDB_ADD(&fom->fo_fop->f_addb, &cd_fom_addb_loc,
			    cd_fom_func_fail,
			    "m0_cob_locate() failed.", rc);
		return rc;
	}

	M0_ASSERT(cob != NULL);
	rc = m0_cob_delete_put(cob, &fom->fo_tx.tx_dbtx);
	if (rc == 0)
		M0_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    m0_addb_trace, "Cob deleted successfully.");
	else
		M0_ADDB_ADD(&fom->fo_fop->f_addb, &cd_fom_addb_loc,
			    cd_fom_func_fail, "Cob deletion failed.", rc);
	return rc;
}

static int cd_stob_delete(struct m0_fom *fom, struct m0_fom_cob_op *cd)
{
	int                    rc;
	struct m0_stob        *stob = NULL;
	struct m0_stob_domain *sdom;
	struct m0_reqh        *reqh;

	M0_PRE(fom != NULL);
	M0_PRE(cd != NULL);

	reqh = m0_fom_reqh(fom);
	sdom = m0_cs_stob_domain_find(reqh, &cd->fco_stobid);
	if (sdom == NULL) {
		M0_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    cc_fom_func_fail,
			    "Stob deletion failed",
			    -EINVAL);
		return -EINVAL;
	}
	rc = m0_stob_find(sdom, &cd->fco_stobid, &stob);
	if (rc != 0) {
		M0_ADDB_ADD(&fom->fo_fop->f_addb, &cd_fom_addb_loc,
			    cd_fom_func_fail,
			    "m0_stob_find() failed.", rc);
		return rc;
	}
	M0_ASSERT(stob != NULL);

	/** @todo Implement m0_stob_delete(). */

	M0_ASSERT(stob->so_ref.a_value  >= CD_FOM_STOBIO_LAST_REFS);
	m0_stob_put(stob);
	M0_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
		    m0_addb_trace, "Stob deleted successfully.");

	return rc;
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
