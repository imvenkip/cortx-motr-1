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
#include "mero/setup.h"
#include "ioservice/io_service_addb.h"
#include "ioservice/io_service.h"
#include "lib/finject.h"
#include "layout/layout.h"
#include "layout/pdclust.h"
#include "stob/domain.h"	   /* m0_stob_domain_find_by_stob_fid */

/* Forward Declarations. */
static void cc_fom_fini(struct m0_fom *fom);
static int  cob_ops_fom_tick(struct m0_fom *fom);
static void cc_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc);
static int  cc_stob_create_credit(struct m0_fom *fom, struct m0_fom_cob_op *cc,
				  struct m0_be_tx_credit *accum);
static int  cc_stob_create(struct m0_fom *fom, struct m0_fom_cob_op *cc);
static void cob_op_credit(struct m0_fom *fom, enum m0_cob_op opcode,
			  struct m0_be_tx_credit *accum);
static int  cc_cob_create(struct m0_fom            *fom,
			  struct m0_fom_cob_op     *cc,
			  const struct m0_cob_attr *attr);

static void cd_fom_fini(struct m0_fom *fom);
static void cd_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc);
static int  cd_cob_delete(struct m0_fom            *fom,
			  struct m0_fom_cob_op     *cd,
			  const struct m0_cob_attr *attr);
static int cd_stob_delete_credit(struct m0_fom *fom, struct m0_fom_cob_op *cc,
				 struct m0_be_tx_credit *accum);
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

struct m0_sm_state_descr cob_ops_phases[] = {
	[M0_FOPH_COB_OPS_PREPARE] = {
		.sd_name      = "COB Create/Delete Prepare",
		.sd_allowed   = M0_BITS(M0_FOPH_COB_OPS_CREATE_DELETE,
					M0_FOPH_FAILURE)
	},
	[M0_FOPH_COB_OPS_CREATE_DELETE] = {
		.sd_name      = "COB Create/Delete",
		.sd_allowed   = M0_BITS(M0_FOPH_SUCCESS,
					M0_FOPH_FAILURE)
	}
};

struct m0_sm_conf cob_ops_conf = {
	.scf_name      = "COB create/delete phases",
	.scf_nr_states = ARRAY_SIZE(cob_ops_phases),
	.scf_state     = cob_ops_phases
};

/** Cob create fom ops. */
static const struct m0_fom_ops cc_fom_ops = {
	.fo_fini	  = cc_fom_fini,
	.fo_tick	  = cob_ops_fom_tick,
	.fo_home_locality = cob_fom_locality_get,
	.fo_addb_init     = cc_fom_addb_init
};

/** Common fom_type_ops for m0_fop_cob_create and m0_fop_cob_delete fops. */
const struct m0_fom_type_ops cob_fom_type_ops = {
	.fto_create = m0_cob_fom_create,
};

/** Cob delete fom ops. */
static const struct m0_fom_ops cd_fom_ops = {
	.fo_fini	  = cd_fom_fini,
	.fo_tick	  = cob_ops_fom_tick,
	.fo_home_locality = cob_fom_locality_get,
	.fo_addb_init     = cd_fom_addb_init
};

/* defined in mdservice/md_foms.c */
extern void m0_md_cob_wire2mem(struct m0_cob_attr *attr,
			       const struct m0_fop_cob *body);

void dump_cob_attr(const struct m0_cob_attr *attr)
{
	uint32_t valid = attr->ca_valid;
#define	level M0_FATAL
	M0_LOG(level, "pfid = "FID_F, FID_P(&attr->ca_pfid));
	M0_LOG(level, "tfid = "FID_F, FID_P(&attr->ca_tfid));
	if (valid & M0_COB_MODE)
		M0_LOG(level, "mode = %o", attr->ca_mode);
	if (valid & M0_COB_UID)
		M0_LOG(level, "uid = %u", attr->ca_uid);
	if (valid & M0_COB_GID)
		M0_LOG(level, "gid = %u", attr->ca_gid);
	if (valid & M0_COB_ATIME)
		M0_LOG(level, "atime = %lu", attr->ca_atime);
	if (valid & M0_COB_MTIME)
		M0_LOG(level, "mtime = %lu", attr->ca_mtime);
	if (valid & M0_COB_CTIME)
		M0_LOG(level, "ctime = %lu", attr->ca_ctime);
	if (valid & M0_COB_NLINK)
		M0_LOG(level, "nlink = %u", attr->ca_nlink);
	if (valid & M0_COB_RDEV)
		M0_LOG(level, "rdev = %lu", attr->ca_rdev);
	if (valid & M0_COB_SIZE)
		M0_LOG(level, "szie = %lu", attr->ca_size);
	if (valid & M0_COB_BLKSIZE)
		M0_LOG(level, "blkszie = %lu", attr->ca_blksize);
	if (valid & M0_COB_BLOCKS)
		M0_LOG(level, "blocks = %lu", attr->ca_blocks);
	if (valid & M0_COB_LID)
		M0_LOG(level, "lid = %lu", attr->ca_lid);
	M0_LOG(level, "version = %lu", attr->ca_version);
#undef level
}

M0_INTERNAL int m0_cob_fom_create(struct m0_fop *fop, struct m0_fom **out,
				  struct m0_reqh *reqh)
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
		IOS_ADDB_FUNCFAIL(rc, COB_FOM_CREATE_1, &m0_ios_addb_ctx);

		return M0_RC(rc);
	}
	fom = *out;
	M0_ASSERT(fom != NULL);

	fom_ops = m0_is_cob_create_fop(fop) ? &cc_fom_ops : &cd_fom_ops;
	rfop = m0_fop_reply_alloc(fop, &m0_fop_cob_op_reply_fopt);
	if (rfop == NULL) {
		IOS_ADDB_FUNCFAIL(rc, COB_FOM_CREATE_2, &m0_ios_addb_ctx);
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

static inline struct m0_fom_cob_op *cob_fom_get(struct m0_fom *fom)
{
	M0_PRE(fom != NULL);

	return container_of(fom, struct m0_fom_cob_op, fco_fom);
}

static void cc_fom_fini(struct m0_fom *fom)
{
	struct m0_fom_cob_op *cfom;
	struct m0_fid	     *stob_fid;

	M0_PRE(fom != NULL);

	cfom = cob_fom_get(fom);
	stob_fid = &cfom->fco_stob_fid;

	M0_FOM_ADDB_POST(fom, &fom->fo_service->rs_reqh->rh_addb_mc,
			 &m0_addb_rt_ios_ccfom_finish,
			 m0_stob_fid_dom_id_get(stob_fid),
			 m0_stob_fid_key_get(stob_fid),
			 m0_fom_rc(fom));

	m0_fom_fini(fom);
	m0_free(cfom);
}

static size_t cob_fom_locality_get(const struct m0_fom *fom)
{
	M0_PRE(fom != NULL);

	return m0_fop_opcode(fom->fo_fop);
}

static void cc_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
	struct m0_fom_cob_op *cfom;

	cfom = cob_fom_get(fom);

	M0_ADDB_CTX_INIT(mc, &fom->fo_addb_ctx, &m0_addb_ct_cob_create_fom,
			 &fom->fo_service->rs_addb_ctx,
			 cfom->fco_gfid.f_container, cfom->fco_gfid.f_key,
			 cfom->fco_cfid.f_container, cfom->fco_gfid.f_key);
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
	io_fom_cob_rw_fid2stob_map(&cfom->fco_cfid, &cfom->fco_stob_fid);
	cfom->fco_cob_idx = common->c_cob_idx;
}

/* async getattr & gatlayout ut and sample code: helper callback */
static int step = 0;
static struct m0_cob_attr async_attr;
static int async_attr_rc = -1;
static void _getattr_async_callback(void *arg, int rc)
{
	struct m0_fom *fom;

	M0_LOG(M0_FATAL, "getattr async callback arg=%p rc=%d", arg, rc);
	fom = arg;
	async_attr_rc = rc;
	m0_fom_wakeup(fom);
}

static struct m0_layout *async_layout = NULL;
static int async_layout_rc = -1;
static void _getlayout_async_callback(void *arg, int rc)
{
	struct m0_fom *fom;

	M0_LOG(M0_FATAL, "getlayout async callback arg=%p rc=%d", arg, rc);
	fom = arg;
	async_layout_rc = rc;
	m0_fom_wakeup(fom);
}
/* async getattr & gatlayout ut and sample code: helper ends here */

/* defined in io_foms.c */
extern int ios__poolmach_check(struct m0_poolmach *poolmach,
			       struct m0_pool_version_numbers *cliv);

static int cob_ops_stob_find(struct m0_fom_cob_op *co)
{
	int rc;

	rc = m0_stob_find(&co->fco_stob_fid, &co->fco_stob);
	if (rc != 0)
		return M0_RC(rc);
	if (m0_stob_state_get(co->fco_stob) == CSS_UNKNOWN)
		   rc = m0_stob_locate(co->fco_stob);
	if (rc == 0 && m0_stob_state_get(co->fco_stob) == CSS_NOENT)
		rc = -ENOENT;
	m0_stob_put(co->fco_stob);

	return M0_RC(rc);
}

static int cob_ops_fom_tick(struct m0_fom *fom)
{
	struct m0_fom_cob_op           *cob_op;
	struct m0_poolmach             *poolmach;
	struct m0_reqh                 *reqh;
	struct m0_fop_cob_common       *common;
	struct m0_cob_attr              attr = { { 0, } };
	struct m0_be_tx_credit          cob_op_tx_credit = {};
	struct m0_be_tx_credit         *tx_cred;
	int                             rc = 0;
	bool                            fop_is_create;
	const char                     *ops;

	M0_PRE(fom != NULL);
	M0_PRE(fom->fo_ops != NULL);
	M0_PRE(fom->fo_type != NULL);

	common = m0_cobfop_common_get(fom->fo_fop);
	reqh = m0_fom_reqh(fom);
	poolmach = m0_ios_poolmach_get(reqh);
	fop_is_create = fom->fo_fop->f_type == &m0_fop_cob_create_fopt;
	if (m0_fom_phase(fom) < M0_FOPH_NR) {
		cob_op = cob_fom_get(fom);
		switch(m0_fom_phase(fom)) {
		case M0_FOPH_INIT:
			if (fop_is_create)
				break;
			/*
			 * Find the stob and handle non-existing stob error
			 * earlier, before initialising the transaction.
			 * This avoids complications in handling transaction
			 * cleanup.
			 */
			rc = cob_ops_stob_find(cob_op);
			if (rc != 0) {
				cob_op->fco_is_done = true;
				m0_fom_phase_move(fom, rc, M0_FOPH_FAILURE);
				goto pack;
			}
			break;
		case M0_FOPH_TXN_OPEN:
			tx_cred = m0_fom_tx_credit(fom);
			if (fop_is_create) {
				cc_stob_create_credit(fom, cob_op,
						      tx_cred);
				cob_op_credit(fom, M0_COB_OP_CREATE,
					      tx_cred);
			} else {
				rc = cd_stob_delete_credit(fom, cob_op,
							   tx_cred);
				if (rc == 0) {
					M0_SET0(&cob_op_tx_credit);
					cob_op_credit(fom, M0_COB_OP_DELETE,
						      &cob_op_tx_credit);
					if (!m0_be_tx_should_break(m0_fom_tx(fom),
								   &cob_op_tx_credit)) {
						m0_be_tx_credit_add(tx_cred,
								    &cob_op_tx_credit);
						cob_op->fco_is_done = true;
					}
				}
				rc = rc == -EAGAIN ? 0 : rc;
			}
		}
		if (rc == 0 && !fop_is_create && !cob_op->fco_is_done &&
		    m0_fom_rc(fom) == 0) {
			if (m0_fom_phase(fom) == M0_FOPH_QUEUE_REPLY) {
				/*
				 * We have come here from M0_FOPH_TXN_COMMIT but
				 * we have not completed the operation yet.
				 * Move to M0_FOPH_TXN_COMMIT_WAIT.
				 */
				m0_fom_phase_set(fom, M0_FOPH_TXN_COMMIT_WAIT);
				return M0_FSO_AGAIN;
			} else if (m0_fom_phase(fom) == M0_FOPH_TXN_COMMIT_WAIT) {
				rc = m0_fom_tx_commit_wait(fom);
				/*
				 * We have come here from M0_FOPH_TXN_COMMIT_WAIT
				 * but have not completed the operation so go
				 * back to M0_FOPH_TXN_INIT.
				 */
				if (rc == M0_FSO_AGAIN)
					m0_fom_phase_set(fom, M0_FOPH_TXN_INIT);
				else
					return M0_RC(rc);
			}
		}
		rc = m0_fom_tick_generic(fom);
		return M0_RC(rc);
	}
	if (fop_is_create)
		ops = "Create";
	else
		ops = "Delete";

	if (M0_FI_ENABLED("async_getattr_getlayout_it")) {
		if (step == 0) {
			M0_LOG(M0_FATAL, "Sending getattr async in %s", ops);
			rc = m0_ios_mds_getattr_async(reqh, &common->c_gobfid,
						      &async_attr,
						      &_getattr_async_callback,
						      fom);
			M0_ASSERT(rc == 0);
			step = 1;
			return M0_FSO_WAIT;
		} else if (step == 1) {
			if (async_attr_rc == 0) {
				M0_LOG(M0_FATAL, "ATTR: tfid = "FID_F" "
						 "mode = [%o] lid = [%lu]",
				       FID_P(&async_attr.ca_tfid),
				       async_attr.ca_mode,
				       async_attr.ca_lid);

				/* It's important here to find the layout first.
				 * m0_ios_mds_layout_get_async() doesn't check
				 * this before sending fop.
				 */
				async_layout = m0_layout_find(&reqh->rh_ldom,
							      async_attr.ca_lid);
				if (async_layout != NULL) {
					async_layout_rc = 0;
					step = 2;
					return M0_FSO_AGAIN;
				}

				M0_LOG(M0_FATAL, "LAYOUT Async Sending");
				rc = m0_ios_mds_layout_get_async(reqh,
						&reqh->rh_ldom,
						async_attr.ca_lid,
						&async_layout,
						&_getlayout_async_callback,
						fom);
				M0_ASSERT(rc == 0);
				step = 2;
				return M0_FSO_WAIT;
			}
			step = -1;
		} else if (step == 2) {
			if (async_layout_rc == 0) {
				struct m0_pdclust_layout *pdl = NULL;
				pdl = m0_layout_to_pdl(async_layout);
				M0_LOG(M0_FATAL, "pdl N=%d,K=%d,P=%d,"
						 "unit_size=%llu",
						 m0_pdclust_N(pdl),
						 m0_pdclust_K(pdl),
						 m0_pdclust_P(pdl),
						 (unsigned long long)
						 m0_pdclust_unit_size(pdl));
				m0_layout_put(async_layout);
				async_layout = NULL;
			}
			step = -1;
		}
	}

	switch (m0_fom_phase(fom)) {
	case M0_FOPH_COB_OPS_PREPARE: {
		struct m0_pool_version_numbers *cliv;

		M0_LOG(M0_DEBUG, "Cob %s operation prepare", ops);
		cliv = (struct m0_pool_version_numbers*)&common->c_version;

		rc = ios__poolmach_check(poolmach, cliv);
		if (rc != 0) {
			m0_fom_phase_move(fom, rc, M0_FOPH_FAILURE);
			goto pack;
		}

		m0_fom_phase_set(fom, M0_FOPH_COB_OPS_CREATE_DELETE);
		return M0_FSO_AGAIN;
	}
	case M0_FOPH_COB_OPS_CREATE_DELETE:
		M0_LOG(M0_DEBUG, "Cob %s operation started", ops);
		m0_md_cob_wire2mem(&attr, &common->c_body);
		cob_op = cob_fom_get(fom);
		if (fop_is_create) {
			rc = cc_stob_create(fom, cob_op) ?:
			     cc_cob_create(fom, cob_op, &attr);
		} else {
			if (cob_op->fco_is_done)
				rc = cd_cob_delete(fom, cob_op, &attr);
			if (rc == 0)
				rc = cd_stob_delete(fom, cob_op);
		}
		step = 0;
		m0_fom_phase_moveif(fom, rc, M0_FOPH_SUCCESS, M0_FOPH_FAILURE);
	        M0_LOG(M0_DEBUG, "Cob %s operation finished with %d", ops, rc);
		break;
	default:
		M0_IMPOSSIBLE("Invalid phase for cob create fom.");
		rc = -EINVAL;
		m0_fom_phase_moveif(fom, rc, M0_FOPH_SUCCESS, M0_FOPH_FAILURE);
	}

pack:
        if (m0_fom_phase(fom) == M0_FOPH_SUCCESS ||
            m0_fom_phase(fom) == M0_FOPH_FAILURE) {
		struct m0_fop_cob_op_reply     *reply;
		reply = m0_fop_data(fom->fo_rep_fop);
		reply->cor_rc = rc;

		/* Piggyback some information about the transaction */
		m0_fom_mod_rep_fill(&reply->cor_mod_rep, fom);

		m0_ios_poolmach_version_updates_pack(poolmach,
						     &common->c_version,
						     &reply->cor_fv_version,
						     &reply->cor_fv_updates);
	}
	return M0_FSO_AGAIN;
}

static int cob_foms_stob_domain_find(struct m0_fom_cob_op *cc,
				     struct m0_stob_domain **sdom)
{
	*sdom = m0_stob_domain_find_by_stob_fid(&cc->fco_stob_fid);
	if (*sdom == NULL) {
		M0_LOG(M0_DEBUG, "can't find domain for stob_fid="FID_F,
		       FID_P(&cc->fco_stob_fid));
	}
	return *sdom == NULL ? -EINVAL : 0;
}

static int cc_stob_create_credit(struct m0_fom *fom, struct m0_fom_cob_op *cc,
				 struct m0_be_tx_credit *accum)
{
	struct m0_stob_domain *sdom;
	int		       rc;

	rc = cob_foms_stob_domain_find(cc, &sdom);
	if (rc != 0) {
		IOS_ADDB_FUNCFAIL(-EINVAL, CC_STOB_CREATE_CRED,
				  &m0_ios_addb_ctx);
	} else {
		m0_stob_create_credit(sdom, m0_fom_tx_credit(fom));
	}
	return M0_RC(rc);
}

static int cc_stob_create(struct m0_fom *fom, struct m0_fom_cob_op *cc)
{
	struct m0_stob_domain *sdom;
	struct m0_stob        *stob;
	int                    rc;

	rc = cob_foms_stob_domain_find(cc, &sdom);
	if (rc != 0) {
		IOS_ADDB_FUNCFAIL(-EINVAL, CC_STOB_CREATE_1, &m0_ios_addb_ctx);
	} else {
		rc = m0_stob_find(&cc->fco_stob_fid, &stob);
		rc = rc ?: m0_stob_state_get(stob) == CSS_UNKNOWN ?
			   m0_stob_locate(stob) : 0;
		rc = rc ?: m0_stob_state_get(stob) == CSS_NOENT ?
			   m0_stob_create(stob, &fom->fo_tx, NULL) : 0;
		if (rc != 0) {
			M0_LOG(M0_DEBUG, "m0_stob_create() failed with %d", rc);
			IOS_ADDB_FUNCFAIL(rc, CC_STOB_CREATE_2,
					  &m0_ios_addb_ctx);
		} else {
			M0_LOG(M0_DEBUG, "Stob created successfully.");
		}
		if (stob != NULL)
			m0_stob_put(stob);
	}
	return M0_RC(rc);
}

static struct m0_cob_domain *cdom_get(struct m0_fom *fom)
{
	struct m0_reqh_io_service *ios;

	M0_PRE(fom != NULL);

	ios = container_of(fom->fo_service, struct m0_reqh_io_service,
			   rios_gen);

	return ios->rios_cdom;
}

static int cc_cob_nskey_make(struct m0_cob_nskey **nskey,
			     const struct m0_fid *gfid,
			     uint32_t cob_idx)
{
	char     nskey_name[UINT32_MAX_STR_LEN];
	uint32_t nskey_name_len;

	M0_PRE(m0_fid_is_set(gfid));

	M0_SET_ARR0(nskey_name);
	snprintf((char*)nskey_name, UINT32_MAX_STR_LEN, "%u",
		 (uint32_t)cob_idx);

	nskey_name_len = strlen(nskey_name);

	return m0_cob_nskey_make(nskey, gfid, (char *)nskey_name,
				 nskey_name_len);
}

static void cob_op_credit(struct m0_fom *fom, enum m0_cob_op opcode,
			  struct m0_be_tx_credit *accum)
{
	struct m0_cob_domain *cdom;

	M0_PRE(fom != NULL);
	cdom = cdom_get(fom);
	M0_ASSERT(cdom != NULL);
	m0_cob_tx_credit(cdom, opcode, m0_fom_tx_credit(fom));
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
	struct m0_cob_nskey  *nskey;
	struct m0_cob_nsrec   nsrec;
	struct m0_cob_fabrec *fabrec;
	struct m0_cob_omgrec  omgrec;

	M0_PRE(cc != NULL);
	M0_PRE(cdom != NULL);

	M0_SET0(&nsrec);
        rc = m0_cob_alloc(cdom, &cob);
        if (rc)
                return M0_RC(rc);

        M0_LOG(M0_DEBUG, "Creating cob for "FID_F"/%x",
               FID_P(&cc->fco_cfid), cc->fco_cob_idx);

	rc = cc_cob_nskey_make(&nskey, &cc->fco_gfid, cc->fco_cob_idx);
	if (rc != 0) {
		IOS_ADDB_FUNCFAIL(rc, CC_COB_CREATE_1, &m0_ios_addb_ctx);
	        m0_cob_put(cob);
		return M0_RC(rc);
	}

        io_fom_cob_rw_stob2fid_map(&cc->fco_stob_fid, &nsrec.cnr_fid);
	nsrec.cnr_nlink   = attr->ca_nlink;
	nsrec.cnr_size    = attr->ca_size;
	nsrec.cnr_blksize = attr->ca_blksize;
	nsrec.cnr_blocks  = attr->ca_blocks;
	nsrec.cnr_atime   = attr->ca_atime;
	nsrec.cnr_mtime   = attr->ca_mtime;
	nsrec.cnr_ctime   = attr->ca_ctime;
	nsrec.cnr_lid     = attr->ca_lid;

	rc = m0_cob_fabrec_make(&fabrec, NULL, 0);
	if (rc) {
		m0_free(nskey);
		m0_cob_put(cob);
		return M0_RC(rc);
	}
	fabrec->cfb_version = ctx->t_id;

	omgrec.cor_uid  = attr->ca_uid;
	omgrec.cor_gid  = attr->ca_gid;
	omgrec.cor_mode = attr->ca_mode;

	rc = m0_cob_create(cob, nskey, &nsrec, fabrec, &omgrec, ctx);
	if (rc) {
	        /*
	         * Cob does not free nskey and fab rec on errors. We need to do
		 * so ourself. In case cob created successfully, it frees things
		 * on last put.
	         */
		m0_free(nskey);
		m0_free(fabrec);
		IOS_ADDB_FUNCFAIL(rc, CC_COB_CREATE_2, &m0_ios_addb_ctx);
	}
	m0_cob_put(cob);

	return M0_RC(rc);
}

static void cd_fom_fini(struct m0_fom *fom)
{
	struct m0_fom_cob_op *cfom;
	struct m0_fid	     *stob_fid;

	M0_PRE(fom != NULL);

	cfom = cob_fom_get(fom);
	stob_fid = &cfom->fco_stob_fid;

	M0_FOM_ADDB_POST(fom, &fom->fo_service->rs_reqh->rh_addb_mc,
			 &m0_addb_rt_ios_ccfom_finish,
			 m0_stob_fid_dom_id_get(stob_fid),
			 m0_stob_fid_key_get(stob_fid),
			 m0_fom_rc(fom));

	m0_fom_fini(fom);
	m0_free(cfom);
}


static void cd_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
	struct m0_fom_cob_op *cfom;

	cfom = cob_fom_get(fom);

	M0_ADDB_CTX_INIT(mc, &fom->fo_addb_ctx, &m0_addb_ct_cob_delete_fom,
			 &fom->fo_service->rs_addb_ctx,
			 cfom->fco_gfid.f_container, cfom->fco_gfid.f_key,
			 cfom->fco_cfid.f_container, cfom->fco_gfid.f_key);
}

static int cd_cob_delete(struct m0_fom            *fom,
			 struct m0_fom_cob_op     *cd,
			 const struct m0_cob_attr *attr)
{
	int                   rc;
	struct m0_cob_oikey   oikey;
	struct m0_cob        *cob;
	struct m0_cob_domain *cdom;
	struct m0_fid         fid;

	M0_PRE(fom != NULL);
	M0_PRE(cd != NULL);

	cdom = cdom_get(fom);
	M0_ASSERT(cdom != NULL);

        M0_LOG(M0_DEBUG, "Deleting cob for "FID_F"/%x",
	       FID_P(&cd->fco_cfid), cd->fco_cob_idx);

        io_fom_cob_rw_stob2fid_map(&cd->fco_stob_fid, &fid);
        m0_cob_oikey_make(&oikey, &fid, 0);
	rc = m0_cob_locate(cdom, &oikey, 0, &cob);
	if (rc != 0) {
		IOS_ADDB_FUNCFAIL(rc, CD_COB_DELETE_1, &m0_ios_addb_ctx);
		return M0_RC(rc);
	}

	M0_ASSERT(cob != NULL);
	cob->co_nsrec.cnr_nlink--;
	M0_ASSERT(attr->ca_nlink == 0);
	M0_ASSERT(cob->co_nsrec.cnr_nlink == 0);

	rc = m0_cob_delete(cob, m0_fom_tx(fom));
	if (rc != 0)
		IOS_ADDB_FUNCFAIL(rc, CD_COB_DELETE_2, &m0_ios_addb_ctx);
	else
		M0_LOG(M0_DEBUG, "Cob deleted successfully.");

	return M0_RC(rc);
}

static int cd_stob_delete_credit(struct m0_fom *fom, struct m0_fom_cob_op *cc,
				 struct m0_be_tx_credit *accum)
{
	struct m0_stob_domain *sdom;
	struct m0_stob	      *stob;
	int		       rc;

	rc = cob_foms_stob_domain_find(cc, &sdom);
	if (rc != 0) {
		IOS_ADDB_FUNCFAIL(-EINVAL, CC_STOB_DELETE_CRED_1,
				  &m0_ios_addb_ctx);
	} else {
		/*
		 * XXX: We need not lookup the stob again as it was already
		 * found and saved in M0_FOPH_INIT phase. But due to MERO-244
		 * (resend) the ref count is increased which is not expected
		 * and causes side effect on the operation.
		 */
		rc = m0_stob_lookup(&cc->fco_stob_fid, &cc->fco_stob);
		if (rc != 0)
			return M0_RC(rc);
		stob = cc->fco_stob;
		rc = m0_stob_state_get(stob) == CSS_UNKNOWN ? m0_stob_locate(stob) : 0;
		M0_ASSERT(m0_stob_state_get(stob) == CSS_EXISTS);
		if (rc != 0)
			return M0_RC(rc);
		M0_ASSERT(stob != NULL);
		rc = m0_stob_destroy_credit(stob, accum);
		m0_stob_put(stob);
	}
	return M0_RC(rc);
}

static int cd_stob_delete(struct m0_fom *fom, struct m0_fom_cob_op *cd)
{
	struct m0_stob_domain *sdom;
	struct m0_stob        *stob = NULL;
	int                    rc;

	rc = cob_foms_stob_domain_find(cd, &sdom);
	if (rc != 0) {
		IOS_ADDB_FUNCFAIL(-EINVAL, CD_STOB_DELETE_1, &m0_ios_addb_ctx);
	} else {
		/*
		 * XXX: We need not lookup the stob again as it was already
		 * found and saved in M0_FOPH_INIT phase. But due to MERO-244
		 * (resend) the ref count is increased which is not expected
		 * and causes side effect on the operation.
		 */
		rc = m0_stob_lookup(&cd->fco_stob_fid, &cd->fco_stob);
		if (rc != 0)
			return M0_RC(rc);
		stob = cd->fco_stob;
		M0_ASSERT(stob != NULL);
		rc = m0_stob_destroy(stob, &fom->fo_tx);
		if (rc != 0) {
			M0_LOG(M0_DEBUG, "m0_stob_destroy() failed with %d",
			       rc);
			IOS_ADDB_FUNCFAIL(rc, CD_STOB_DELETE_2,
					  &m0_ios_addb_ctx);
		} else {
			M0_LOG(M0_DEBUG, "Stob deleted successfully.");
		}
	}
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
