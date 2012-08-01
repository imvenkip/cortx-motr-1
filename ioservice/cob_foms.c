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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/errno.h"
#include "lib/memory.h"             /* c2_free(), C2_ALLOC_PTR() */
#include "fid/fid.h"                /* c2_fid */
#include "ioservice/io_foms.h"      /* io_fom_cob_rw_fid2stob_map */
#include "ioservice/io_fops.h"      /* c2_cobfop_common_get */
#include "ioservice/cob_foms.h"     /* c2_fom_cob_create, c2_fom_cob_delete */
#include "ioservice/io_fops.h"      /* c2_is_cob_create_fop() */
#include "ioservice/cobfid_map.h"   /* c2_cobfid_map_get() c2_cobfid_map_put()*/
#include "reqh/reqh.h"              /* c2_fom_state_generic() */
#include "reqh/reqh_service.h"
#include "colibri/colibri_setup.h"

#ifdef __KERNEL__
#include "ioservice/io_fops_k.h"
#else
#include "ioservice/io_fops_u.h"
#endif

/* Forward Declarations. */
static int  cob_fom_create(struct c2_fop *fop, struct c2_fom **out);
static void cc_fom_fini(struct c2_fom *fom);
static int  cc_fom_state(struct c2_fom *fom);
static int  cc_stob_create(struct c2_fom *fom, struct c2_fom_cob_op *cc);
static int  cc_cob_create(struct c2_fom *fom, struct c2_fom_cob_op *cc);
static int  cc_cobfid_map_add(struct c2_fom *fom, struct c2_fom_cob_op *cc);

static void cd_fom_fini(struct c2_fom *fom);
static int  cd_fom_state(struct c2_fom *fom);
static int  cd_cob_delete(struct c2_fom *fom, struct c2_fom_cob_op *cd);
static int  cd_stob_delete(struct c2_fom *fom, struct c2_fom_cob_op *cd);
static int  cd_cobfid_map_delete(struct c2_fom *fom, struct c2_fom_cob_op *cd);

static void   cob_fom_populate(struct c2_fom *fom);
static int    cob_op_fom_create(struct c2_fom **out);
static size_t cob_fom_locality_get(const struct c2_fom *fom);
static inline struct c2_fom_cob_op *cob_fom_get(struct c2_fom *fom);

enum {
	CC_COB_VERSION_INIT	= 0,
	CC_COB_HARDLINK_NR	= 1,
	CD_FOM_STOBIO_LAST_REFS = 1,
};

static const struct c2_addb_loc cc_fom_addb_loc = {
	.al_name = "create_cob_fom",
};

C2_ADDB_EV_DEFINE(cc_fom_func_fail, "create cob func failed.",
		  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

/** Cob create fom ops. */
static struct c2_fom_ops cc_fom_ops = {
	.fo_fini	  = cc_fom_fini,
	.fo_state	  = cc_fom_state,
	.fo_home_locality = cob_fom_locality_get,
};

/** Common fom_type_ops for c2_fop_cob_create and c2_fop_cob_delete fops. */
static const struct c2_fom_type_ops cob_fom_type_ops = {
	.fto_create = cob_fom_create,
};

extern struct c2_reqh_service_type c2_ios_type;

C2_FOM_TYPE_DECLARE(cob_create, &cob_fom_type_ops, &c2_ios_type);
C2_FOM_TYPE_DECLARE(cob_delete, &cob_fom_type_ops, &c2_ios_type);

static const struct c2_addb_loc cd_fom_addb_loc = {
	.al_name = "cob_delete_fom",
};

C2_ADDB_EV_DEFINE(cd_fom_func_fail, "cob delete fom func failed.",
		  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

/** Cob delete fom ops. */
static const struct c2_fom_ops cd_fom_ops = {
	.fo_fini	  = cd_fom_fini,
	.fo_state	  = cd_fom_state,
	.fo_home_locality = cob_fom_locality_get,
};

static int cob_fom_create(struct c2_fop *fop, struct c2_fom **out)
{
	int			  rc;
	struct c2_fop            *rfop;
	struct c2_fom		 *fom;
	const struct c2_fom_ops  *fom_ops;
	struct c2_fom_cob_op     *cfom;

	C2_PRE(fop != NULL);
	C2_PRE(fop->f_type != NULL);
	C2_PRE(out != NULL);
	C2_PRE(c2_is_cob_create_delete_fop(fop));

	rc = cob_op_fom_create(out);
	cfom = cob_fom_get(*out);
	if (rc != 0) {
		C2_ADDB_ADD(&fop->f_addb, &cc_fom_addb_loc, cc_fom_func_fail,
			    "Failed to create cob_create fom.", rc);
		return rc;
	}
	fom = *out;
	C2_ASSERT(fom != NULL);

	fom_ops = c2_is_cob_create_fop(fop) ? &cc_fom_ops : &cd_fom_ops;
	rfop = c2_fop_alloc(&c2_fop_cob_op_reply_fopt, NULL);
	if (rfop == NULL) {
		C2_ADDB_ADD(&fop->f_addb, &cc_fom_addb_loc, c2_addb_oom);
		c2_free(cfom);
		return -ENOMEM;
	}

	c2_fom_init(fom, &fop->f_type->ft_fom_type, fom_ops, fop, rfop);
	cob_fom_populate(fom);
	return rc;
}

static int cob_op_fom_create(struct c2_fom **out)
{
	struct c2_fom_cob_op *cfom;

	C2_PRE(out != NULL);

	C2_ALLOC_PTR(cfom);
	if (cfom == NULL)
		return -ENOMEM;

	*out = &cfom->fco_fom;
	return 0;
}

static inline struct c2_fom_cob_op *cob_fom_get(struct c2_fom *fom)
{
	C2_PRE(fom != NULL);

	return container_of(fom, struct c2_fom_cob_op, fco_fom);
}

static void cc_fom_fini(struct c2_fom *fom)
{
	struct c2_fom_cob_op *cfom;

	C2_PRE(fom != NULL);

	cfom = cob_fom_get(fom);

	c2_fom_fini(fom);
	c2_free(cfom);
}

static size_t cob_fom_locality_get(const struct c2_fom *fom)
{
	C2_PRE(fom != NULL);

        return fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode;
}

static void cob_fom_populate(struct c2_fom *fom)
{
	struct c2_fom_cob_op     *cfom;
	struct c2_fop_cob_common *common;

	C2_PRE(fom != NULL);
	C2_PRE(fom->fo_fop != NULL);

	cfom = cob_fom_get(fom);
	common = c2_cobfop_common_get(fom->fo_fop);

	io_fom_cob_rw_fid_wire2mem(&common->c_gobfid, &cfom->fco_gfid);
	io_fom_cob_rw_fid_wire2mem(&common->c_cobfid, &cfom->fco_cfid);
	io_fom_cob_rw_fid2stob_map(&cfom->fco_cfid, &cfom->fco_stobid);
}

static int cc_fom_state(struct c2_fom *fom)
{
	int                          rc;
	struct c2_fom_cob_op        *cc;
	struct c2_fop_cob_op_reply *reply;

	C2_PRE(fom != NULL);
	C2_PRE(fom->fo_ops != NULL);
	C2_PRE(fom->fo_type != NULL);

	if (fom->fo_phase < C2_FOPH_NR) {
		rc = c2_fom_state_generic(fom);
		return rc;
	}

	if (fom->fo_phase == C2_FOPH_CC_COB_CREATE) {
		cc = cob_fom_get(fom);

		rc = cc_stob_create(fom, cc);
		if (rc != 0)
			goto out;

		rc = cc_cob_create(fom, cc);
		if (rc != 0)
			goto out;

		rc = cc_cobfid_map_add(fom, cc);
	} else
		C2_IMPOSSIBLE("Invalid phase for cob create fom.");

out:
	reply = c2_fop_data(fom->fo_rep_fop);
	reply->cor_rc = rc;

	fom->fo_rc = rc;
	fom->fo_phase = (rc == 0) ? C2_FOPH_SUCCESS : C2_FOPH_FAILURE;
	return C2_FSO_AGAIN;
}

static int cc_stob_create(struct c2_fom *fom, struct c2_fom_cob_op *cc)
{
	int                    rc;
	struct c2_stob        *stob;
	struct c2_reqh        *reqh;
	struct c2_stob_domain *sdom;

	C2_PRE(fom != NULL);
	C2_PRE(cc != NULL);

	reqh = fom->fo_loc->fl_dom->fd_reqh;
	sdom = c2_cs_stob_domain_find(reqh, &cc->fco_stobid);
	if (sdom == NULL) {
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    cc_fom_func_fail,
			    "Stob creation failed in cc_stob_create().",
			    -EINVAL);
		return -EINVAL;
	}

	rc = c2_stob_create_helper(sdom, &fom->fo_tx, &cc->fco_stobid, &stob);
	if (rc != 0)
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    cc_fom_func_fail,
			    "Stob creation failed in cc_stob_create().", rc);
	else {
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    c2_addb_trace, "Stob created successfully.");
		c2_stob_put(stob);
	}

	return rc;
}

static int cc_cob_create(struct c2_fom *fom, struct c2_fom_cob_op *cc)
{
	int			  rc;
	struct c2_cob		 *cob;
	struct c2_cob_domain	 *cdom;
	struct c2_fop_cob_create *fop;
	struct c2_cob_nskey	 *nskey;
	struct c2_cob_nsrec	  nsrec;
	struct c2_cob_fabrec	  fabrec;

	C2_PRE(fom != NULL);
	C2_PRE(cc != NULL);

	cdom = fom->fo_loc->fl_dom->fd_reqh->rh_cob_domain;
	C2_ASSERT(cdom != NULL);
	fop = c2_fop_data(fom->fo_fop);

	c2_cob_nskey_make(&nskey, cc->fco_cfid.f_container,
			  cc->fco_cfid.f_key, fop->cc_cobname.cn_name);
	if (nskey == NULL) {
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    c2_addb_oom);
		return -ENOMEM;
	}

	nsrec.cnr_stobid = cc->fco_stobid;
	nsrec.cnr_nlink = CC_COB_HARDLINK_NR;

	fabrec.cfb_version.vn_lsn = c2_fol_lsn_allocate(fom->fo_fol);
	fabrec.cfb_version.vn_vc = CC_COB_VERSION_INIT;

	rc = c2_cob_create(cdom, nskey, &nsrec, &fabrec, CA_NSKEY_FREE, &cob,
			   &fom->fo_tx.tx_dbtx);

	/*
	 * For all errors except -ENOMEM, cob code puts reference on cob
	 * which in turn finalizes the in-memory cob object.
	 * The flag CA_NSKEY_FREE takes care of deallocating memory for
	 * nskey during cob finalization.
	 */
	switch (rc) {
	case 0:
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    c2_addb_trace, "Cob created successfully.");
		/**
		 * Since c2_cob_locate() does not cache in-memory cobs,
		 * it allocates a new c2_cob structure by default every time
		 * c2_cob_locate() is called.
		 * Hence releasing reference of cob here which
		 * otherwise would cause a memory leak.
		 */
		c2_cob_put(cob);
		break;

	case -ENOMEM:
		c2_free(nskey->cnk_name.b_data);
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    cc_fom_func_fail,
			    "Memory allocation failed in cc_cob_create().", rc);
		break;

	default:
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    cc_fom_func_fail, "Cob creation failed", rc);
	}

	return rc;
}

static int cc_cobfid_map_add(struct c2_fom *fom, struct c2_fom_cob_op *cc)
{
	int			    rc;
	struct c2_uint128	    cob_fid;
	struct c2_reqh             *reqh;
	struct c2_cobfid_map       *cfm;

	C2_PRE(fom != NULL);
	C2_PRE(cc != NULL);

	reqh = fom->fo_service->rs_reqh;
	rc = c2_cobfid_map_get(reqh, &cfm);
	if (rc != 0)
		return rc;

	cob_fid.u_hi = cc->fco_cfid.f_container;
	cob_fid.u_lo = cc->fco_cfid.f_key;

	c2_mutex_lock(&cfm->cfm_mutex);
	rc = c2_cobfid_map_add(cfm, cob_fid.u_hi, cc->fco_gfid, cob_fid);
	c2_mutex_unlock(&cfm->cfm_mutex);
	if (rc != 0)
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    cc_fom_func_fail, "cobfid_map_add() failed.", rc);
	else
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    c2_addb_trace, "Record added to cobfid_map.");

	c2_cobfid_map_put(reqh);

	return rc;
}

static void cd_fom_fini(struct c2_fom *fom)
{
	struct c2_fom_cob_op *cfom;

	C2_PRE(fom != NULL);

	cfom = cob_fom_get(fom);

	c2_fom_fini(fom);
	c2_free(cfom);
}

static int cd_fom_state(struct c2_fom *fom)
{
	int                         rc;
	struct c2_fom_cob_op       *cd;
	struct c2_fop_cob_op_reply *reply;

	C2_PRE(fom != NULL);
	C2_PRE(fom->fo_ops != NULL);
	C2_PRE(fom->fo_type != NULL);

	if (fom->fo_phase < C2_FOPH_NR) {
		rc = c2_fom_state_generic(fom);
		return rc;
	}

	if (fom->fo_phase == C2_FOPH_CD_COB_DEL) {
		cd = cob_fom_get(fom);

		rc = cd_cob_delete(fom, cd);
		if (rc != 0)
			goto out;

		rc = cd_stob_delete(fom, cd);
		if (rc != 0)
			goto out;

		rc = cd_cobfid_map_delete(fom, cd);
	} else
		C2_IMPOSSIBLE("Invalid phase for cob delete fom.");

out:
	reply = c2_fop_data(fom->fo_rep_fop);
	reply->cor_rc = rc;

	fom->fo_rc = rc;
	fom->fo_phase = (rc == 0) ? C2_FOPH_SUCCESS : C2_FOPH_FAILURE;
	return C2_FSO_AGAIN;
}

static int cd_cob_delete(struct c2_fom *fom, struct c2_fom_cob_op *cd)
{
	int                   rc;
	struct c2_cob        *cob;
	struct c2_cob_domain *cdom;

	C2_PRE(fom != NULL);
	C2_PRE(cd != NULL);

	cdom = fom->fo_loc->fl_dom->fd_reqh->rh_cob_domain;
	C2_ASSERT(cdom != NULL);

	rc = c2_cob_locate(cdom, &cd->fco_stobid, &cob, &fom->fo_tx.tx_dbtx);
	if (rc != 0) {
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cd_fom_addb_loc,
			    cd_fom_func_fail,
			    "c2_cob_locate() failed.", rc);
		return rc;
	}
	C2_ASSERT(cob != NULL);

	rc = c2_cob_delete(cob, &fom->fo_tx.tx_dbtx);
	if (rc != 0)
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cd_fom_addb_loc,
			    cd_fom_func_fail, "c2_cob_delete() failed.", rc);
	else
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    c2_addb_trace, "Cob deleted successfully.");

	return rc;
}

static int cd_stob_delete(struct c2_fom *fom, struct c2_fom_cob_op *cd)
{
	int                    rc;
	struct c2_stob        *stob = NULL;
	struct c2_stob_domain *sdom;
	struct c2_reqh        *reqh;

	C2_PRE(fom != NULL);
	C2_PRE(cd != NULL);

	reqh = fom->fo_loc->fl_dom->fd_reqh;
	sdom = c2_cs_stob_domain_find(reqh, &cd->fco_stobid);
	if (sdom == NULL) {
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    cc_fom_func_fail,
			    "Stob deletion failed",
			    -EINVAL);
		return -EINVAL;
	}
	rc = c2_stob_find(sdom, &cd->fco_stobid, &stob);
	if (rc != 0) {
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cd_fom_addb_loc,
			    cd_fom_func_fail,
			    "c2_stob_find() failed.", rc);
		return rc;
	}
	C2_ASSERT(stob != NULL);

	/** @todo Implement c2_stob_delete(). */

	C2_ASSERT(stob->so_ref.a_value  >= CD_FOM_STOBIO_LAST_REFS);
	c2_stob_put(stob);
	C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
		    c2_addb_trace, "Stob deleted successfully.");

	return rc;
}

static int cd_cobfid_map_delete(struct c2_fom *fom, struct c2_fom_cob_op *cd)
{
	int                         rc;
	struct c2_uint128           cob_fid;
	struct c2_reqh             *reqh;
	struct c2_cobfid_map       *cfm;

	C2_PRE(fom != NULL);
	C2_PRE(cd != NULL);

	reqh = fom->fo_service->rs_reqh;
	rc = c2_cobfid_map_get(reqh, &cfm);
	if (rc != 0)
		return rc;

	cob_fid.u_hi = cd->fco_cfid.f_container;
	cob_fid.u_lo = cd->fco_cfid.f_key;

	c2_mutex_lock(&cfm->cfm_mutex);
	rc = c2_cobfid_map_del(cfm, cob_fid.u_hi, cd->fco_gfid);
	c2_mutex_unlock(&cfm->cfm_mutex);

	if (rc != 0)
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cd_fom_addb_loc,
			    cd_fom_func_fail,
			    "c2_cobfid_map_setup_delrec() failed.", rc);
	else
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    c2_addb_trace,
			    "Record removed from cobfid_map.");

	c2_cobfid_map_put(reqh);

	return rc;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
