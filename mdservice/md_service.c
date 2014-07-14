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
 * Original author: Yuriy Umanets <yuriy_umanets@xyratex.com>
 * Original creation date: 23/07/2012
 */

/**
   @addtogroup mdservice
   @{
 */
#undef M0_ADDB_CT_CREATE_DEFINITION
#define M0_ADDB_CT_CREATE_DEFINITION
#include "mdservice/mdservice_addb.h"

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_MDS
#include "lib/trace.h"

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/locality.h"
#include "mero/magic.h"
#include "mero/setup.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "layout/layout.h"
#include "layout/linear_enum.h"
#include "layout/layout_db.h"
#include "layout/pdclust.h"
#include "conf/confc.h"
#include "mdservice/md_fops.h"
#include "mdservice/md_service.h"

static struct m0_addb_ctx m0_mds_mod_ctx;

static int mds_allocate(struct m0_reqh_service **service,
			const struct m0_reqh_service_type *stype);
static void mds_fini(struct m0_reqh_service *service);

static int mds_start(struct m0_reqh_service *service);
static void mds_stop(struct m0_reqh_service *service);

/**
 * MD Service type operations.
 */
static const struct m0_reqh_service_type_ops mds_type_ops = {
        .rsto_service_allocate = mds_allocate
};

/**
 * MD Service operations.
 */
static const struct m0_reqh_service_ops mds_ops = {
        .rso_start       = mds_start,
	.rso_start_async = m0_reqh_service_async_start_simple,
        .rso_stop        = mds_stop,
        .rso_fini        = mds_fini
};

M0_REQH_SERVICE_TYPE_DEFINE(m0_mds_type, &mds_type_ops, "mdservice",
			     &m0_addb_ct_mds_serv, 2);

M0_INTERNAL int m0_mds_register(void)
{
	m0_addb_ctx_type_register(&m0_addb_ct_mds_mod);
	m0_addb_ctx_type_register(&m0_addb_ct_mds_serv);
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &m0_mds_mod_ctx,
			 &m0_addb_ct_mds_mod, &m0_addb_proc_ctx);
        m0_reqh_service_type_register(&m0_mds_type);
        return m0_mdservice_fop_init();
}

M0_INTERNAL void m0_mds_unregister(void)
{
        m0_reqh_service_type_unregister(&m0_mds_type);
        m0_mdservice_fop_fini();
	m0_addb_ctx_fini(&m0_mds_mod_ctx);
}

/**
 * Allocates and initiates MD Service instance.
 * This operation allocates & initiates service instance with its operation
 * vector.
 */
static int mds_allocate(struct m0_reqh_service **service,
			const struct m0_reqh_service_type *stype)
{
        struct m0_reqh_md_service *mds;

        M0_PRE(service != NULL && stype != NULL);

        M0_ALLOC_PTR_ADDB(mds, &m0_addb_gmc, M0_MDS_ADDB_LOC_ALLOCATE,
			  &m0_mds_mod_ctx);
        if (mds == NULL)
                return -ENOMEM;

        mds->rmds_magic = M0_MDS_REQH_SVC_MAGIC;

        *service = &mds->rmds_gen;
        (*service)->rs_ops = &mds_ops;
        return 0;
}

/**
 * Finalise MD Service instance.
 * This operation finalises service instance and de-allocate it.
 *
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static void mds_fini(struct m0_reqh_service *service)
{
        struct m0_reqh_md_service *serv_obj;

        M0_PRE(service != NULL);

        serv_obj = container_of(service, struct m0_reqh_md_service, rmds_gen);
        m0_free(serv_obj);
}

static int mds_ldom_init(struct m0_reqh_md_service *mds)
{
	struct m0_reqh *reqh;
	int rc;

	M0_ENTRY();

	reqh = mds->rmds_gen.rs_reqh;
	rc = m0_layout_domain_init(&mds->rmds_layout_dom, reqh->rh_dbenv);
	if (rc == 0) {
		rc = m0_layout_standard_types_register(&mds->rmds_layout_dom);
		if (rc != 0)
			m0_layout_domain_fini(&mds->rmds_layout_dom);
	}

	return M0_RC(rc);
}

static void mds_ldom_fini(struct m0_reqh_md_service *mds)
{
	M0_ENTRY();

	m0_layout_standard_types_unregister(&mds->rmds_layout_dom);
	m0_layout_domain_fini(&mds->rmds_layout_dom);

	M0_LEAVE();
}

static int mds_layout_enum_build(struct m0_reqh_md_service *mds,
				 const uint32_t pool_width,
				 struct m0_layout_enum **lay_enum)
{
	struct m0_layout_linear_attr  lin_attr;
	struct m0_layout_linear_enum *lle;
	int                           rc;

	M0_ENTRY();
	M0_PRE(pool_width > 0 && lay_enum != NULL);
	/*
	 * cob_fid = fid { B * idx + A, gob_fid.key }
	 * where idx is in [0, pool_width)
	 */
	lin_attr = (struct m0_layout_linear_attr){
		.lla_nr = pool_width,
		.lla_A  = 1,
		.lla_B  = 1
	};

	*lay_enum = NULL;
	rc = m0_linear_enum_build(&mds->rmds_layout_dom,
				  &lin_attr, &lle);
	if (rc == 0)
		*lay_enum = &lle->lle_base;

	return M0_RC(rc);
}

static int mds_layout_build(struct m0_reqh_md_service *mds,
			    const uint64_t layout_id,
			    const uint32_t N,
			    const uint32_t K,
			    const uint32_t pool_width,
			    const uint64_t unit_size,
			    struct m0_layout_enum *le,
			    struct m0_layout **layout)
{
	struct m0_pdclust_attr    pl_attr;
	struct m0_pdclust_layout *pdlayout = NULL;
	int                       rc;

	M0_ENTRY();
	M0_PRE(pool_width > 0);
	M0_PRE(le != NULL && layout != NULL);

	pl_attr = (struct m0_pdclust_attr){
		.pa_N         = N,
		.pa_K         = K,
		.pa_P         = pool_width,
		.pa_unit_size = unit_size,
	};
	m0_uint128_init(&pl_attr.pa_seed, "upjumpandpumpim,");

	*layout = NULL;
	rc = m0_pdclust_build(&mds->rmds_layout_dom,
			      layout_id, &pl_attr, le,
			      &pdlayout);
	if (rc == 0)
		*layout = m0_pdl_to_layout(pdlayout);

	return M0_RC(rc);
}

static int
mds_layout_add(struct m0_reqh_md_service *mds, struct m0_layout *l, uint64_t lid)
{
	struct m0_db_pair   pair;
	struct m0_buf       buf;
	struct m0_reqh     *reqh = mds->rmds_gen.rs_reqh;
	struct m0_db_tx     tx;
	int                 rc;

	/* TODO m0_layout_size(const struct *l) should be used
	 * in the future to calculate buffer size large enough
	 * for any type of layout.
	 */
	buf.b_nob = m0_layout_max_recsize(&mds->rmds_layout_dom);
	buf.b_addr = m0_alloc(buf.b_nob);
	if (buf.b_addr == NULL)
		return M0_ERR(-ENOMEM, "layout buffer allocation failed");

	m0_layout_pair_set(&pair, &lid, buf.b_addr, buf.b_nob);

	m0_db_tx_init(&tx, reqh->rh_dbenv, 0);
	rc = m0_layout_add(l, &tx, &pair);
	m0_db_tx_commit(&tx);

	m0_buf_free(&buf);

	return rc;
}

/* start from 1 */
static int lid_to_unit_map[] = {
	[ 0] =       -1, /* invalid */
	[ 1] =     4096,
	[ 2] =     8192,
	[ 3] =    16384,
	[ 4] =    32768,
	[ 5] =    65536,
	[ 6] =   131072,
	[ 7] =   262144,
	[ 8] =   524288,
	[ 9] =  1048576,
	[10] =  2097152,
	[11] =  4194304,
	[12] =  8388608,
	[13] = 16777216,
	[14] = 33554432,
};

static int mds_layouts_init(struct m0_reqh_md_service *mds,
			    const struct m0_pdclust_attr *pa)
{
	struct m0_layout	*layout;
	struct m0_layout_enum	*layout_enum;
	int			 i;
	int			 rc;

	M0_ENTRY();

	M0_PRE(pa->pa_P != 0);
	M0_PRE(pa->pa_N != 0);
	M0_PRE(pa->pa_K != 0);

	for (i = 1; i < ARRAY_SIZE(lid_to_unit_map); ++i) {
		rc = mds_layout_enum_build(mds, pa->pa_P, &layout_enum);
		if (rc != 0) {
			M0_LOG(M0_ERROR, "layout %d enum build failed: rc=%d",
						i, rc);
			break;
		}

		rc = mds_layout_build(mds, i, pa->pa_N, pa->pa_K, pa->pa_P,
				      lid_to_unit_map[i],
				      layout_enum,
				      &layout);
		if (rc != 0) {
			M0_LOG(M0_ERROR, "layout %d build failed: rc=%d",
						i, rc);
			m0_layout_enum_fini(layout_enum);
			break;
		}

		rc = mds_layout_add(mds, layout, i);
		/* layout_enum will be released along
		 * with this layout */
		m0_layout_put(layout);
		if (rc != 0)
			break;
	}

	return M0_RC(rc);
}

/**
 * Start MD Service.
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static int mds_start(struct m0_reqh_service *service)
{
	struct m0_mero			*cctx;
	struct m0_reqh			*reqh;
	struct m0_reqh_md_service	*mds;
	struct m0_sm_group		*grp;
	struct m0_rpc_machine		*rmach;
	struct m0_confc			 confc;
	struct m0_conf_obj		*fs;
	struct m0_pdclust_attr		 pa = {0};
	int				 rc;

	M0_PRE(service != NULL);

	mds = container_of(service, struct m0_reqh_md_service, rmds_gen);

	/* in UT we don't init layouts */
	if (mds->rmds_gen.rs_reqh_ctx == NULL)
		return 0;

	rc = mds_ldom_init(mds);
	if (rc != 0)
		return M0_ERR(rc, "layout domain initialization failed");

	cctx = mds->rmds_gen.rs_reqh_ctx->rc_mero;
	if (cctx->cc_profile == NULL || cctx->cc_confd_addr == NULL)
		return 0;
	reqh = mds->rmds_gen.rs_reqh;
	/* grp  = &reqh->rh_sm_grp; - no, reqh is not started yet */
	/** @todo XXX change this when reqh will be started before services,
	 *        see MERO-317
	 */
	grp  = m0_locality0_get()->lo_grp;
	rmach = m0_reqh_rpc_mach_tlist_head(&reqh->rh_rpc_machines);
	rc = m0_conf_fs_get(cctx->cc_profile, cctx->cc_confd_addr,
		rmach, grp, &confc, &fs);
	if (rc != 0)
		return M0_ERR(rc, "failed to get fs configuration");
	rc = m0_pdclust_attr_read(fs, &pa);
	m0_confc_close(fs);
	m0_confc_fini(&confc);
	if (rc != 0)
		return M0_ERR(rc, "failed to get fs params");

	return mds_layouts_init(mds, &pa);
}

/**
 * Stops MD Service.
 * @param service pointer to service instance.
 *
 * @pre service != NULL
 */
static void mds_stop(struct m0_reqh_service *service)
{
	struct m0_reqh_md_service	*mds;

	M0_PRE(service != NULL);

	mds = container_of(service, struct m0_reqh_md_service, rmds_gen);
	if (mds->rmds_gen.rs_reqh_ctx != NULL)
		mds_ldom_fini(mds);
}

#undef M0_TRACE_SUBSYSTEM

/** @} endgroup mdservice */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
