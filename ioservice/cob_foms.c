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
#include <config.h>
#endif

#include "lib/errno.h"
#include "lib/memory.h"             /* c2_free(), C2_ALLOC_PTR() */
#include "fop/fop.h"
#include "fop/fop_format.h"
#include "fid/fid.h"                /* c2_fid */
#include "ioservice/io_foms.h"      /* io_fom_cob_rw_fid2stob_map */
#include "ioservice/cob_foms.h"     /* c2_fom_cob_create, c2_fom_cob_delete */
#include "ioservice/io_fops.h"      /* c2_is_cob_create_fop() */
#include "reqh/reqh.h"              /* c2_fom_state_generic() */
#include "colibri/colibri_setup.h"  /* c2_cs_ctx_get(), c2_cobfid_setup_get() */

#ifdef __KERNEL__
#include "ioservice/io_fops_k.h"
#else
#include "ioservice/io_fops_u.h"
#endif

/* Forward Declarations. */

static int    cob_fom_create(struct c2_fop *fop, struct c2_fom **out);
static int    cc_fom_create(struct c2_fom **out);
static void   cc_fom_fini(struct c2_fom *fom);
static void   cc_fom_populate(struct c2_fom *fom);
static int    cc_fom_state(struct c2_fom *fom);
static int    cc_stob_create(struct c2_fom *fom, struct c2_fom_cob_create *cc);
static int    cc_cob_create(struct c2_fom *fom, struct c2_fom_cob_create *cc);
static int    cc_cobfid_map_add(struct c2_fom *fom,
				struct c2_fom_cob_create *cc);

static void   cd_fom_fini(struct c2_fom *fom);
static int    cd_fom_state(struct c2_fom *fom);
static int    cd_fom_create(struct c2_fom **out);
static void   cd_fom_populate(struct c2_fom *fom);
static int    cd_cob_delete(struct c2_fom *fom, struct c2_fom_cob_delete *cd);
static int    cd_stob_delete(struct c2_fom *fom, struct c2_fom_cob_delete *cd);
static int    cd_cobfid_map_delete(struct c2_fom *fom,
				   struct c2_fom_cob_delete *cd);

static size_t cob_fom_locality_get(const struct c2_fom *fom);

static inline struct c2_fom_cob_create *cc_fom_get(struct c2_fom *fom);
static inline struct c2_fom_cob_delete *cd_fom_get(struct c2_fom *fom);

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

/**
 * Cob create fom ops.
 */
static struct c2_fom_ops cc_fom_ops = {
	.fo_fini	  = cc_fom_fini,
	.fo_state	  = cc_fom_state,
	.fo_home_locality = cob_fom_locality_get,
	.fo_service_name  = c2_io_fom_cob_rw_service_name,
};

static const struct c2_fom_type_ops cc_fom_type_ops = {
	.fto_create = cob_fom_create,
};

struct c2_fom_type cc_fom_type = {
	.ft_ops = &cc_fom_type_ops,
};

static const struct c2_addb_loc cd_fom_addb_loc = {
	.al_name = "cob_delete_fom",
};

C2_ADDB_EV_DEFINE(cd_fom_func_fail, "cob delete fom func failed.",
		  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

/**
 * Cob delete fom ops.
 */
static const struct c2_fom_ops cd_fom_ops = {
	.fo_fini	  = cd_fom_fini,
	.fo_state	  = cd_fom_state,
	.fo_home_locality = cob_fom_locality_get,
	.fo_service_name  = c2_io_fom_cob_rw_service_name,
};

static const struct c2_fom_type_ops cd_fom_type_ops = {
	.fto_create = cob_fom_create,
};

struct c2_fom_type cd_fom_type = {
	.ft_ops = &cd_fom_type_ops
};

static int cob_fom_create(struct c2_fop *fop, struct c2_fom **out)
{
	int			  rc;
	bool			  cob_create;
	struct c2_fom		 *fom;
	struct c2_fom_cob_create *cc;
	struct c2_fom_cob_delete *cd;

	C2_PRE(fop != NULL);
	C2_PRE(fop->f_type != NULL);
	C2_PRE(out != NULL);
	C2_PRE(c2_is_cob_create_delete_fop(fop));

	cob_create = c2_is_cob_create_fop(fop);

	rc = cob_create ? cc_fom_create(out) : cd_fom_create(out);
	if (rc != 0) {
		C2_ADDB_ADD(&fop->f_addb, &cc_fom_addb_loc, cc_fom_func_fail,
			    "Failed to create cob_create fom.", rc);
		return rc;
	}
	fom = *out;
	C2_ASSERT(fom != NULL);

	fom->fo_fop  = fop;
	fom->fo_type = &fop->f_type->ft_fom_type;
	if (cob_create) {
		cc = cc_fom_get(fom);
		cc_fom_populate(fom);
	} else {
		cd = cd_fom_get(fom);
		cd_fom_populate(fom);
	}

	c2_fom_init(fom);
	fom->fo_rep_fop = c2_fop_alloc(&c2_fop_cob_op_reply_fopt, NULL);
	if (fom->fo_rep_fop == NULL) {
		C2_ADDB_ADD(&fop->f_addb, &cc_fom_addb_loc, c2_addb_oom);
		cob_create ? c2_free(cc) : c2_free(cd);
		return -ENOMEM;
	}

	return rc;
}

static int cc_fom_create(struct c2_fom **out)
{
	struct c2_fom_cob_create *cc;

	C2_PRE(out != NULL);

	C2_ALLOC_PTR(cc);
	if (cc == NULL)
		return -ENOMEM;

	cc->fcc_cc.cc_fom.fo_ops  = &cc_fom_ops;
	*out = &cc->fcc_cc.cc_fom;
	return 0;
}

static inline struct c2_fom_cob_create *cc_fom_get(struct c2_fom *fom)
{
	struct c2_fom_cob_common *cc;

	C2_PRE(fom != NULL);

	cc = container_of(fom, struct c2_fom_cob_common, cc_fom);
	return container_of(cc, struct c2_fom_cob_create, fcc_cc);
}

static void cc_fom_fini(struct c2_fom *fom)
{
	struct c2_fom_cob_create *cc;

	C2_PRE(fom != NULL);

	cc = cc_fom_get(fom);

	c2_fom_fini(fom);
	c2_free(cc);
}

static size_t cob_fom_locality_get(const struct c2_fom *fom)
{
	C2_PRE(fom != NULL);

        return fom->fo_fop->f_type->ft_rpc_item_type.rit_opcode;
}

static void cc_fom_populate(struct c2_fom *fom)
{
	struct c2_fom_cob_create *cc;
	struct c2_fop_cob_create *f;

	C2_PRE(fom != NULL);

	cc = cc_fom_get(fom);
	f = c2_fop_data(fom->fo_fop);
	C2_ASSERT(f != NULL);

	io_fom_cob_rw_fid_wire2mem(&f->cc_common.c_gobfid,
				   &cc->fcc_cc.cc_gfid);
	io_fom_cob_rw_fid_wire2mem(&f->cc_common.c_cobfid,
				   &cc->fcc_cc.cc_cfid);
}

static int cc_fom_state(struct c2_fom *fom)
{
	int                         rc;
	struct c2_fom_cob_create   *cc;
	struct c2_fop_cob_op_reply *reply;

	C2_PRE(fom != NULL);
	C2_PRE(fom->fo_ops != NULL);
	C2_PRE(fom->fo_type != NULL);

	if (fom->fo_phase < FOPH_NR) {
		rc = c2_fom_state_generic(fom);
		return rc;
	}

	if (fom->fo_phase == FOPH_CC_COB_CREATE) {
		cc = cc_fom_get(fom);

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
	fom->fo_phase = (rc == 0) ? FOPH_SUCCESS : FOPH_FAILURE;
	return FSO_AGAIN;
}

static int cc_stob_create(struct c2_fom *fom, struct c2_fom_cob_create *cc)
{
	int			  rc;
	struct c2_stob		 *stob;
	struct c2_stob_id	  stobid;
	struct c2_stob_domain	 *stdom;

	C2_PRE(fom != NULL);
	C2_PRE(cc != NULL);

	stdom = fom->fo_loc->fl_dom->fd_reqh->rh_stdom;
	io_fom_cob_rw_fid2stob_map(&cc->fcc_cc.cc_cfid, &stobid);

	rc = c2_stob_find(stdom, &stobid, &stob);
	if (rc != 0) {
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    cc_fom_func_fail,
			    "c2_stob_find() failed in cc_stob_create().", rc);
		return rc;
	}
	C2_ASSERT(stob != NULL);

	rc = c2_stob_create(stob, &fom->fo_tx);
	if (rc != 0)
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    cc_fom_func_fail,
			    "Stob creation failed in cc_stob_create().", rc);
	else {
		cc->fcc_stob_id = stob->so_id;
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    c2_addb_trace, "Stob created successfully.");
	}

	c2_stob_put(stob);
	return rc;
}

static int cc_cob_create(struct c2_fom *fom, struct c2_fom_cob_create *cc)
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

	c2_cob_nskey_make(&nskey, cc->fcc_cc.cc_cfid.f_container,
			  cc->fcc_cc.cc_cfid.f_key, fop->cc_cobname.ib_buf);
	if (nskey == NULL) {
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    c2_addb_oom);
		return -ENOMEM;
	}

	nsrec.cnr_stobid = cc->fcc_stob_id;
	nsrec.cnr_nlink = CC_COB_HARDLINK_NR;

	fabrec.cfb_version.vn_lsn = c2_fol_lsn_allocate(fom->fo_fol);
	fabrec.cfb_version.vn_vc = CC_COB_VERSION_INIT;

	rc = c2_cob_create(cdom, nskey, &nsrec, &fabrec, CA_NSKEY_FREE, &cob,
			   &fom->fo_tx.tx_dbtx);

	/*
	 * For rest of all errors, cob code puts reference on cob
	 * which in turn finalizes the in-memory cob object.
	 * The flag CA_NSKEY_FREE takes care of deallocating memory for
	 * nskey during cob finalization.
	 */
	if (rc == 0) {
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
	} else if (rc == -ENOMEM) {
		c2_free(nskey->cnk_name.b_data);
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    cc_fom_func_fail,
			    "Memory allocation failed in cc_cob_create().", rc);
	}

	return rc;
}

static int cc_cobfid_map_add(struct c2_fom *fom, struct c2_fom_cob_create *cc)
{
	int			rc;
	struct c2_uint128	cob_fid;
	struct c2_colibri      *cctx;
	struct c2_cobfid_setup *s = NULL;

	C2_PRE(fom != NULL);
	C2_PRE(cc != NULL);

	cctx = c2_cs_ctx_get(fom->fo_service);
	C2_ASSERT(cctx != NULL);
	c2_mutex_lock(&cctx->cc_mutex);
	rc = c2_cobfid_setup_get(&s, cctx);
	c2_mutex_unlock(&cctx->cc_mutex);
	C2_ASSERT(rc == 0 && s != NULL);

	cob_fid.u_hi = cc->fcc_cc.cc_cfid.f_container;
	cob_fid.u_lo = cc->fcc_cc.cc_cfid.f_key;

	rc = c2_cobfid_setup_recadd(s, cc->fcc_cc.cc_gfid, cob_fid);
	if (rc != 0)
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    cc_fom_func_fail, "cobfid_map_add() failed.", rc);
	else
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    c2_addb_trace, "Record added to cobfid_map.");

	c2_mutex_lock(&cctx->cc_mutex);
	c2_cobfid_setup_put(cctx);
	c2_mutex_unlock(&cctx->cc_mutex);
	return rc;
}

static int cd_fom_create(struct c2_fom **out)
{
	struct c2_fom_cob_delete *cd;

	C2_PRE(out != NULL);

	C2_ALLOC_PTR(cd);
	if (cd == NULL)
		return -ENOMEM;

	cd->fcd_cc.cc_fom.fo_ops = &cd_fom_ops;
	*out = &cd->fcd_cc.cc_fom;
	return 0;
}

static inline struct c2_fom_cob_delete *cd_fom_get(struct c2_fom *fom)
{
	struct c2_fom_cob_common *c;

	C2_PRE(fom != NULL);

	c = container_of(fom, struct c2_fom_cob_common, cc_fom);
	return container_of(c, struct c2_fom_cob_delete, fcd_cc);
}

static void cd_fom_fini(struct c2_fom *fom)
{
	struct c2_fom_cob_delete *cd;

	C2_PRE(fom != NULL);

	cd = cd_fom_get(fom);

	c2_fom_fini(fom);
	c2_free(cd);
}

static void cd_fom_populate(struct c2_fom *fom)
{
	struct c2_fom_cob_delete *cd;
	struct c2_fop_cob_delete *f;

	C2_PRE(fom != NULL);

	cd = cd_fom_get(fom);
	f = c2_fop_data(fom->fo_fop);

	io_fom_cob_rw_fid_wire2mem(&f->cd_common.c_gobfid,
				   &cd->fcd_cc.cc_gfid);
	io_fom_cob_rw_fid_wire2mem(&f->cd_common.c_cobfid,
				   &cd->fcd_cc.cc_cfid);

	cd->fcd_stobid.si_bits.u_hi = f->cd_common.c_cobfid.f_seq;
	cd->fcd_stobid.si_bits.u_lo = f->cd_common.c_cobfid.f_oid;
}

static int cd_fom_state(struct c2_fom *fom)
{
	int                         rc;
	struct c2_fom_cob_delete   *cd;
	struct c2_fop_cob_op_reply *reply;

	C2_PRE(fom != NULL);
	C2_PRE(fom->fo_ops != NULL);
	C2_PRE(fom->fo_type != NULL);

	if (fom->fo_phase < FOPH_NR) {
		rc = c2_fom_state_generic(fom);
		return rc;
	}

	if (fom->fo_phase == FOPH_CD_COB_DEL) {
		cd = cd_fom_get(fom);

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
	fom->fo_phase = (rc == 0) ? FOPH_SUCCESS : FOPH_FAILURE;
	return FSO_AGAIN;
}

static int cd_cob_delete(struct c2_fom *fom, struct c2_fom_cob_delete *cd)
{
	int			  rc;
	struct c2_cob	         *cob;
	struct c2_cob_domain	 *cdom;

	C2_PRE(fom != NULL);
	C2_PRE(cd != NULL);

	cdom = fom->fo_loc->fl_dom->fd_reqh->rh_cob_domain;
	C2_ASSERT(cdom != NULL);

	rc = c2_cob_locate(cdom, &cd->fcd_stobid, &cob, &fom->fo_tx.tx_dbtx);
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

static int cd_stob_delete(struct c2_fom *fom, struct c2_fom_cob_delete *cd)
{
	int			  rc;
	struct c2_stob		 *stob = NULL;
	struct c2_stob_domain	 *stdom;

	C2_PRE(fom != NULL);
	C2_PRE(cd != NULL);

	stdom = fom->fo_loc->fl_dom->fd_reqh->rh_stdom;

	rc = c2_stob_find(stdom, &cd->fcd_stobid, &stob);
	if (rc != 0) {
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cd_fom_addb_loc,
			    cd_fom_func_fail,
			    "c2_stob_find() failed.", rc);
		return rc;
	}
	C2_ASSERT(stob != NULL);

	C2_ASSERT(stob->so_ref.a_value == CD_FOM_STOBIO_LAST_REFS);
	c2_stob_put(stob);
	C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
		    c2_addb_trace, "Stob deleted successfully.");

	return rc;
}

static int cd_cobfid_map_delete(struct c2_fom *fom,
				struct c2_fom_cob_delete *cd)
{
	int			  rc;
	struct c2_uint128	  cob_fid;
	struct c2_colibri        *cctx;
	struct c2_cobfid_setup	 *s;

	C2_PRE(fom != NULL);
	C2_PRE(cd != NULL);

	cctx = c2_cs_ctx_get(fom->fo_service);
	C2_ASSERT(cctx != NULL);
	c2_mutex_lock(&cctx->cc_mutex);
	rc = c2_cobfid_setup_get(&s, cctx);
	c2_mutex_unlock(&cctx->cc_mutex);
	C2_ASSERT(rc == 0 && s != NULL);

	cob_fid.u_hi = cd->fcd_cc.cc_cfid.f_container;
	cob_fid.u_lo = cd->fcd_cc.cc_cfid.f_key;

	rc = c2_cobfid_setup_recdel(s, cd->fcd_cc.cc_gfid, cob_fid);
	if (rc != 0)
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cd_fom_addb_loc,
			    cd_fom_func_fail,
			    "c2_cobfid_setup_delrec() failed.", rc);
	else
		C2_ADDB_ADD(&fom->fo_fop->f_addb, &cc_fom_addb_loc,
			    c2_addb_trace,
			    "Record removed from cobfid_map.");

	c2_mutex_lock(&cctx->cc_mutex);
	c2_cobfid_setup_put(cctx);
	c2_mutex_unlock(&cctx->cc_mutex);
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
