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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 08/24/2010
 */

#include "balloc/balloc.h"

#include "be/extmap.h"
#include "be/seg.h"
#include "be/seg_dict.h"

#include "dtm/dtm.h"		/* m0_dtx */

#include "fid/fid.h"		/* m0_fid */

#include "lib/errno.h"
#include "lib/locality.h"	/* m0_locality0_get */
#include "lib/memory.h"
#include "lib/string.h"
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_ADSTOB
#include "lib/trace.h"		/* M0_LOG */

#include "stob/ad.h"
#include "stob/ad_private.h"
#include "stob/ad_private_xc.h"
#include "stob/domain.h"
#include "stob/io.h"
#include "stob/stob.h"
#include "stob/stob_internal.h"	/* m0_stob__fid_set */
#include "stob/stob_addb.h"	/* M0_STOB_OOM */
#include "stob/type.h"		/* m0_stob_type */

enum {
	STOB_TYPE_AD = 0x02,
};

/**
   Types of allocation extents.

   Values of this enum are stored as "physical extent start" in allocation
   extents.
 */
enum stob_ad_allocation_extent_type {
	/**
	    Minimal "special" extent type. All values less than this are valid
	    start values of normal allocated extents.
	 */
	AET_MIN = M0_BINDEX_MAX - (1ULL << 32),
	/**
	   This value is used to tag an extent that does not belong to the
	   stob's name-space. For example, an extent [X, M0_BINDEX_MAX + 1)
	   would usually be AET_NONE for a file of size X.
	 */
	AET_NONE,
	/**
	   This value is used to tag a hole in the storage object.
	 */
	AET_HOLE
};

struct ad_domain_cfg {
	struct m0_fid     adg_fid;
	struct m0_be_seg *adg_seg;
	m0_bcount_t       adg_container_size;
	uint32_t          adg_bshift;
	m0_bcount_t       adg_blocks_per_group;
	m0_bcount_t       adg_res_groups;
};

static struct m0_stob_domain_ops stob_ad_domain_ops;
static struct m0_stob_ops stob_ad_ops;

static int stob_ad_io_init(struct m0_stob *stob, struct m0_stob_io *io);
static void stob_ad_write_credit(struct m0_stob_domain  *dom,
				 struct m0_indexvec     *iv,
				 struct m0_be_tx_credit *accum);
static void
stob_ad_rec_frag_undo_redo_op_cred(const struct m0_fol_frag *frag,
				   struct m0_be_tx_credit       *accum);
static int stob_ad_rec_frag_undo_redo_op(struct m0_fol_frag *frag,
					 struct m0_be_tx	*tx);

M0_FOL_FRAG_TYPE_DECLARE(stob_ad_rec_frag, static,
			 stob_ad_rec_frag_undo_redo_op,
			 stob_ad_rec_frag_undo_redo_op,
			 stob_ad_rec_frag_undo_redo_op_cred,
			 stob_ad_rec_frag_undo_redo_op_cred);
static int stob_ad_cursor(struct m0_stob_ad_domain *adom,
			  struct m0_stob *obj,
			  uint64_t offset,
			  struct m0_be_emap_cursor *it);

static int stob_ad_seg_free(struct m0_dtx *tx,
			    struct m0_stob_ad_domain *adom,
			    const struct m0_be_emap_seg *seg,
			    const struct m0_ext *ext,
			    uint64_t val);

static struct m0_stob_ad_domain *stob_ad_domain2ad(struct m0_stob_domain *dom)
{
	return container_of(dom, struct m0_stob_ad_domain, sad_base);
}

static struct m0_stob_ad *stob_ad_stob2ad(struct m0_stob *stob)
{
	return container_of(stob, struct m0_stob_ad, ad_stob);
}

static void stob_ad_type_register(struct m0_stob_type *type)
{
	int rc;

	m0_xc_ad_private_init();
	M0_FOL_FRAG_TYPE_INIT(stob_ad_rec_frag, "AD record fragment");
	rc = m0_fol_frag_type_register(&stob_ad_rec_frag_type);
	M0_ASSERT(rc == 0); /* XXX void */
}

static void stob_ad_type_deregister(struct m0_stob_type *type)
{
	m0_xc_ad_private_fini();
	m0_fol_frag_type_deregister(&stob_ad_rec_frag_type);
}

M0_INTERNAL void m0_stob_ad_cfg_make(char **str,
				     const struct m0_be_seg *seg,
				     const struct m0_fid *bstore_fid)
{
	char buf[0x400];

	snprintf(buf, ARRAY_SIZE(buf), "%p:"FID_F, seg, FID_P(bstore_fid));
	*str = m0_strdup(buf);
}

static int stob_ad_domain_cfg_init_parse(const char *str_cfg_init,
					 void **cfg_init)
{
	return 0;
}

static void stob_ad_domain_cfg_init_free(void *cfg_init)
{
}

static int stob_ad_domain_cfg_create_parse(const char *str_cfg_create,
					   void **cfg_create)
{
	struct ad_domain_cfg *cfg;
	int                   rc;

	if (str_cfg_create == NULL)
		return -EINVAL;

	M0_ALLOC_PTR(cfg);
	if (cfg != NULL) {
		*cfg = (struct ad_domain_cfg) {
			.adg_container_size   = BALLOC_DEF_CONTAINER_SIZE,
			.adg_bshift           = BALLOC_DEF_BLOCK_SHIFT,
			.adg_blocks_per_group = BALLOC_DEF_BLOCKS_PER_GROUP,
			.adg_res_groups       = BALLOC_DEF_RESERVED_GROUPS,
		};
		/* format = seg:fid */
		rc = sscanf(str_cfg_create, "%p:" FID_SF,
			    (void **)&cfg->adg_seg, FID_S(&cfg->adg_fid));
		rc = rc == 3 ? 0 : -EINVAL;
	} else
		rc = -ENOMEM;

	if (rc == 0)
		*cfg_create = cfg;

	return rc;
}

static void stob_ad_domain_cfg_create_free(void *cfg_create)
{
	m0_free(cfg_create);
}

M0_INTERNAL bool m0_stob_ad_domain__invariant(struct m0_stob_ad_domain *adom)
{
	return _0C(adom->sad_ballroom != NULL);
}

/*
 * Extract be_segment pointer from location string.
 * This is only temporary solution until we haven't mechanism to save domain
 * location permanently.
 *
 * @todo Remove when seg0 is landed as location string won't contain pointer
 *       to segment.
 */
static struct m0_be_seg *stob_ad_domain_seg(const char *location_data)
{
	void *seg;
	int   rc;

	rc = sscanf(location_data, "seg=%p,", &seg);
	M0_ASSERT(rc == 1);
	return (struct m0_be_seg *)seg;
}

static struct m0_sm_group *stob_ad_sm_group(void)
{
	return m0_locality0_get()->lo_grp;
}

static int stob_ad_bstore(struct m0_fid *fid, struct m0_stob **out)
{
	struct m0_stob *stob;
	int		rc;

	rc = m0_stob_find(fid, &stob);
	if (rc == 0) {
		if (m0_stob_state_get(stob) == CSS_UNKNOWN)
			rc = m0_stob_locate(stob);
		if (rc != 0 || m0_stob_state_get(stob) != CSS_EXISTS) {
			m0_stob_put(stob);
			rc = rc ?: -ENOENT;
		}
	}
	*out = rc == 0 ? stob : NULL;
	return rc;
}

static const char *stob_ad_domain_dict_key(const char *location_data)
{
	/* XXX Fix when seg0 is in master: this function should return
	 * a key for seg0 based on location_data.
	 */
	char *ptr = strchr(location_data, ',');
	return ptr == NULL ? NULL : (const char *)++ptr;
}

/*
 * Return pointer to m0_stob_ad_domain within be_segment by location.
 *
 * Current solution assumes the pointer is stored in seg_dict. The final
 * implementation should store this information in seg0.
 *
 * @todo lookup seg0 instead of seg_dict
 */
static struct m0_stob_ad_domain *
stob_ad_domain_locate(const char *location_data)
{
	struct m0_stob_ad_domain *adom;
	struct m0_be_seg         *seg = stob_ad_domain_seg(location_data);
	const char               *dict_key;
	int                       rc;

	M0_ASSERT(seg != NULL);
	dict_key = stob_ad_domain_dict_key(location_data);
	M0_ASSERT(dict_key != NULL);
	rc = m0_be_seg_dict_lookup(seg, dict_key, (void **)&adom);
	if (rc == 0)
		adom->sad_be_seg = seg;
	else
		adom = NULL;

	return adom;
}

static int stob_ad_domain_init(struct m0_stob_type *type,
			       const char *location_data,
			       void *cfg_init,
			       struct m0_stob_domain **out)
{
	struct m0_stob_ad_domain *adom;
	struct m0_stob_domain    *dom;
	struct m0_ad_balloc      *ballroom;
	struct m0_sm_group       *grp = stob_ad_sm_group();
	bool                      balloc_inited;
	int                       rc;

	adom = stob_ad_domain_locate(location_data);
	if (adom != NULL) {
		M0_ASSERT(m0_stob_ad_domain__invariant(adom));

		dom         = &adom->sad_base;
		dom->sd_ops = &stob_ad_domain_ops;
		m0_mutex_init(&adom->sad_mutex);
		m0_be_emap_init(&adom->sad_adata, adom->sad_be_seg);

		ballroom = adom->sad_ballroom;
		m0_balloc_init(b2m0(ballroom));
		m0_sm_group_lock(grp);
		rc = ballroom->ab_ops->bo_init(ballroom, adom->sad_be_seg, grp,
					       adom->sad_bshift,
					       adom->sad_container_size,
					       adom->sad_blocks_per_group,
					       adom->sad_res_groups);
		m0_sm_group_unlock(grp);
		balloc_inited = rc == 0;
		rc = rc ?: stob_ad_bstore(&adom->sad_bstore_fid,
					  &adom->sad_bstore);
		if (rc != 0) {
			if (balloc_inited)
				ballroom->ab_ops->bo_fini(ballroom);
			m0_be_emap_fini(&adom->sad_adata);
			m0_mutex_fini(&adom->sad_mutex);
		} else {
			adom->sad_babshift = adom->sad_bshift -
					m0_stob_block_shift(adom->sad_bstore);
			M0_ASSERT(adom->sad_babshift >= 0);
		}
	} else
		rc = -ENOENT;

	*out = rc == 0 ? dom : NULL;
	return rc;
}

static void stob_ad_domain_fini(struct m0_stob_domain *dom)
{
	struct m0_stob_ad_domain *adom = stob_ad_domain2ad(dom);
	struct m0_ad_balloc      *ballroom = adom->sad_ballroom;

	ballroom->ab_ops->bo_fini(ballroom);
	m0_be_emap_fini(&adom->sad_adata);
	m0_stob_put(adom->sad_bstore);
	m0_mutex_fini(&adom->sad_mutex);
}

static void stob_ad_domain_create_credit(struct m0_be_seg *seg,
					 const char *dict_key,
					 struct m0_be_tx_credit *accum)
{
	struct m0_be_emap map;

	M0_BE_ALLOC_CREDIT_PTR((struct m0_stob_ad_domain *)NULL, seg, accum);
	m0_be_emap_init(&map, seg);
	m0_be_emap_credit(&map, M0_BEO_CREATE, 1, accum);
	m0_be_emap_fini(&map);
	m0_be_seg_dict_insert_credit(seg, dict_key, accum);
}

static void stob_ad_domain_destroy_credit(struct m0_be_seg *seg,
					  const char *dict_key,
					  struct m0_be_tx_credit *accum)
{
	struct m0_be_emap map;

	M0_BE_FREE_CREDIT_PTR((struct m0_stob_ad_domain *)NULL, seg, accum);
	m0_be_emap_init(&map, seg);
	m0_be_emap_credit(&map, M0_BEO_DESTROY, 1, accum);
	m0_be_emap_fini(&map);
	m0_be_seg_dict_delete_credit(seg, dict_key, accum);
}

static int stob_ad_domain_create(struct m0_stob_type *type,
				 const char *location_data,
				 uint64_t dom_key,
				 void *cfg_create)
{
	struct ad_domain_cfg     *cfg = (struct ad_domain_cfg *)cfg_create;
	struct m0_be_seg         *seg = cfg->adg_seg;
	struct m0_sm_group       *grp = stob_ad_sm_group();
	struct m0_stob_ad_domain *adom;
	struct m0_stob_domain    *dom;
	struct m0_be_emap        *emap;
	struct m0_balloc         *cb;
	struct m0_be_tx           tx = {};
	struct m0_be_tx_credit    cred = M0_BE_TX_CREDIT(0, 0);
	const char               *dict_key;
	int                       rc;

	M0_PRE(seg != NULL);
	M0_PRE(strlen(location_data) < ARRAY_SIZE(adom->sad_path));

	/* XXX Fix when seg0 is landed: be_segment won't be a part of location
	 * string and seg0 should be used instead of seg_dict.
	 * TODO Make cleanup on fail.
	 */
	M0_ASSERT(stob_ad_domain_seg(location_data) == seg);
	dict_key = stob_ad_domain_dict_key(location_data);
	M0_ASSERT(dict_key != NULL);

	adom = stob_ad_domain_locate(location_data);
	if (adom != NULL)
		return -EEXIST;

	m0_sm_group_lock(grp);
	m0_be_tx_init(&tx, 0, seg->bs_domain, grp, NULL, NULL, NULL, NULL);
	stob_ad_domain_create_credit(seg, dict_key, &cred);
	m0_be_tx_prep(&tx, &cred);
	rc = m0_be_tx_open_sync(&tx);

	M0_ASSERT(adom == NULL);
	if (rc == 0)
		M0_BE_ALLOC_PTR_SYNC(adom, seg, &tx);
	if (adom != NULL) {
		dom = &adom->sad_base;
		dom->sd_id = m0_stob_domain__dom_id(m0_stob_type_id_get(type),
						    dom_key);
		adom->sad_container_size   = cfg->adg_container_size;
		adom->sad_bshift           = cfg->adg_bshift;
		adom->sad_blocks_per_group = cfg->adg_blocks_per_group;
		adom->sad_res_groups       = cfg->adg_res_groups;
		adom->sad_bstore_fid       = cfg->adg_fid;
		strcpy(adom->sad_path, location_data);
		emap = &adom->sad_adata;
		m0_be_emap_init(emap, seg);
		rc = M0_BE_OP_SYNC_RET(
			op,
			m0_be_emap_create(emap, &tx, &op),
			bo_u.u_emap.e_rc);
		m0_be_emap_fini(emap);
		rc = rc ?: m0_balloc_create(dom_key, seg, grp, &cb);
		rc = rc ?: m0_be_seg_dict_insert(seg, &tx, dict_key, adom);
		if (rc == 0) {
			adom->sad_ballroom = &cb->cb_ballroom;
			M0_BE_TX_CAPTURE_PTR(seg, &tx, adom);
		}

		m0_be_tx_close_sync(&tx);
	}

	m0_be_tx_fini(&tx);
	m0_sm_group_unlock(grp);

	if (adom == NULL && rc == 0) {
		M0_STOB_OOM(AD_DOM_LOCATE);
		rc = -ENOMEM;
	}
	return rc;
}

static int stob_ad_domain_destroy(struct m0_stob_type *type,
				  const char *location_data)
{
	struct m0_stob_ad_domain *adom = stob_ad_domain_locate(location_data);
	struct m0_be_seg         *seg = stob_ad_domain_seg(location_data);
	struct m0_sm_group       *grp = stob_ad_sm_group();
	struct m0_be_emap        *emap = &adom->sad_adata;
	struct m0_be_tx           tx = {};
	struct m0_be_tx_credit    cred = M0_BE_TX_CREDIT(0, 0);
	const char               *dict_key;
	int                       rc;

	if (adom == NULL)
		return -ENOENT;

	dict_key = stob_ad_domain_dict_key(location_data);
	m0_sm_group_lock(grp);
	m0_be_tx_init(&tx, 0, seg->bs_domain, grp, NULL, NULL, NULL, NULL);
	stob_ad_domain_destroy_credit(seg, dict_key, &cred);
	m0_be_tx_prep(&tx, &cred);
	rc = m0_be_tx_open_sync(&tx);
	if (rc == 0) {
		m0_be_emap_init(emap, seg);
		rc = M0_BE_OP_SYNC_RET(op, m0_be_emap_destroy(emap, &tx, &op),
				       bo_u.u_emap.e_rc);
		rc = rc ?: m0_be_seg_dict_delete(seg, &tx, dict_key);
		if (rc == 0)
			M0_BE_FREE_PTR_SYNC(adom, seg, &tx);
		m0_be_tx_close_sync(&tx);
	}
	m0_be_tx_fini(&tx);
	m0_sm_group_unlock(grp);

	return rc;
}

static struct m0_stob *stob_ad_alloc(struct m0_stob_domain *dom,
				     uint64_t stob_key)
{
	struct m0_stob_ad *adstob;

	M0_ALLOC_PTR(adstob);
	return adstob == NULL ? NULL : &adstob->ad_stob;
}

static void stob_ad_free(struct m0_stob_domain *dom,
			 struct m0_stob *stob)
{
	struct m0_stob_ad *adstob = stob_ad_stob2ad(stob);
	m0_free(adstob);
}

static int stob_ad_cfg_parse(const char *str_cfg_create, void **cfg_create)
{
	return 0;
}

static void stob_ad_cfg_free(void *cfg_create)
{
}

static int stob_ad_init(struct m0_stob *stob,
			struct m0_stob_domain *dom,
			uint64_t stob_key)
{
	struct m0_stob_ad_domain *adom = stob_ad_domain2ad(dom);
	struct m0_be_emap_cursor  it;
	struct m0_uint128         prefix;
	int                       rc;

	stob->so_ops = &stob_ad_ops;
	prefix = M0_UINT128(m0_stob_domain_id_get(dom), stob_key);
	rc = M0_BE_OP_SYNC_RET_WITH(
		&it.ec_op,
		m0_be_emap_lookup(&adom->sad_adata, &prefix, 0, &it),
		bo_u.u_emap.e_rc);
	if (rc == 0) {
		m0_stob__fid_set(stob, dom, stob_key);
		stob_ad_stob2ad(stob)->ad_overwrite = false;
		m0_be_emap_close(&it);
	}
	return rc == -ESRCH ? -ENOENT : rc;
}

static void stob_ad_fini(struct m0_stob *stob)
{
}

static void stob_ad_create_credit(struct m0_stob_domain *dom,
				  struct m0_be_tx_credit *accum)
{
	struct m0_stob_ad_domain *adom = stob_ad_domain2ad(dom);
	m0_be_emap_credit(&adom->sad_adata, M0_BEO_INSERT, 1, accum);
}

static int stob_ad_create(struct m0_stob *stob,
			  struct m0_stob_domain *dom,
			  struct m0_dtx *dtx,
			  uint64_t stob_key,
			  void *cfg)
{
	struct m0_stob_ad_domain *adom = stob_ad_domain2ad(dom);
	struct m0_uint128         prefix;
	int                       rc;

	M0_PRE(dtx != NULL);
	prefix = M0_UINT128(m0_stob_domain_id_get(dom), stob_key);
	M0_LOG(M0_DEBUG, U128D_F, U128_P(&prefix));
	rc = M0_BE_OP_SYNC_RET(
		op,
		m0_be_emap_obj_insert(&adom->sad_adata, &dtx->tx_betx, &op,
				      &prefix, AET_NONE),
		bo_u.u_emap.e_rc);

	return rc;
}

static int stob_ad_destroy_credit(struct m0_stob *stob,
				  struct m0_be_tx_credit *accum)
{
	struct m0_stob_ad_domain *adom;
	struct m0_be_emap_cursor  it;
	int                       rc;
	m0_bcount_t               frags = 0;

	adom = stob_ad_domain2ad(m0_stob_dom_get(stob));
	rc = stob_ad_cursor(adom, stob, 0, &it);
	if (rc != 0)
		return M0_RC(rc);
	frags = m0_be_emap_count(&it);
	rc = m0_be_emap_op_rc(&it);
	m0_be_emap_close(&it);
	if (rc == 0)
		m0_be_emap_credit(&adom->sad_adata, M0_BEO_PASTE, frags, accum);

	return M0_RC(rc);
}

static int stob_ad_map_ext_delete(struct m0_stob_ad_domain *adom,
				  struct m0_be_emap_cursor *it,
				  struct m0_dtx *tx)
{
	struct m0_be_op *it_op;
	struct m0_ext   *ext = &it->ec_seg.ee_ext;
	struct m0_ext    todo = {
				.e_start = 0,
				.e_end   = M0_BCOUNT_MAX
			 };
	int              rc = 0;

	M0_LOG(M0_DEBUG, "ext="EXT_F" val=%llu",
		EXT_P(ext), (unsigned long long)it->ec_seg.ee_val);

	it_op = &it->ec_op;
	m0_be_op_init(it_op);
	m0_be_emap_paste(it, &tx->tx_betx, &todo, AET_HOLE,
		 LAMBDA(void, (struct m0_be_emap_seg *seg) {
				/* handle extent deletion. */
				M0_LOG(M0_DEBUG, "del: val=%llu",
					(unsigned long long)seg->ee_val);
				rc = rc ?: stob_ad_seg_free(tx, adom, seg,
							    &seg->ee_ext,
							    seg->ee_val);
		}), NULL, NULL);
	M0_ASSERT(m0_be_op_state(it_op) == M0_BOS_SUCCESS);
	rc = it_op->bo_u.u_emap.e_rc;
	m0_be_op_fini(it_op);

	return M0_RC(rc);
}

/**
 * Destroys ad stob ext map and releases underlying storage object's
 * extents.
 */
static int stob_ad_destroy(struct m0_stob *stob, struct m0_dtx *tx)
{
	struct m0_stob_ad_domain *adom;
	struct m0_be_emap_seg    *seg;
	struct m0_be_emap_cursor  it;
	struct m0_uint128         prefix;
	int                       rc;

	adom   = stob_ad_domain2ad(m0_stob_dom_get(stob));
	prefix = M0_UINT128(stob->so_fid.f_container, stob->so_fid.f_key);
	rc = stob_ad_cursor(adom, stob, 0, &it);
	M0_LOG(M0_DEBUG, U128D_F, U128_P(&it.ec_prefix));
	seg = m0_be_emap_seg_get(&it);
	rc = stob_ad_map_ext_delete(adom, &it, tx);
	if (rc == 0)
		rc = M0_BE_OP_SYNC_RET(op,
				       m0_be_emap_obj_delete(&adom->sad_adata,
							     &tx->tx_betx, &op,
							     &prefix),
				       bo_u.u_emap.e_rc);

	return M0_RC(rc);
}

static uint32_t stob_ad_block_shift(struct m0_stob *stob)
{
	struct m0_stob_ad_domain *adom;

	adom = stob_ad_domain2ad(m0_stob_dom_get(stob));
	return m0_stob_block_shift(adom->sad_bstore);
}

static struct m0_stob_type_ops stob_ad_type_ops = {
	.sto_register		     = &stob_ad_type_register,
	.sto_deregister		     = &stob_ad_type_deregister,
	.sto_domain_cfg_init_parse   = &stob_ad_domain_cfg_init_parse,
	.sto_domain_cfg_init_free    = &stob_ad_domain_cfg_init_free,
	.sto_domain_cfg_create_parse = &stob_ad_domain_cfg_create_parse,
	.sto_domain_cfg_create_free  = &stob_ad_domain_cfg_create_free,
	.sto_domain_init	     = &stob_ad_domain_init,
	.sto_domain_create	     = &stob_ad_domain_create,
	.sto_domain_destroy	     = &stob_ad_domain_destroy,
};

static struct m0_stob_domain_ops stob_ad_domain_ops = {
	.sdo_fini		= &stob_ad_domain_fini,
	.sdo_stob_alloc	    	= &stob_ad_alloc,
	.sdo_stob_free	    	= &stob_ad_free,
	.sdo_stob_cfg_parse 	= &stob_ad_cfg_parse,
	.sdo_stob_cfg_free  	= &stob_ad_cfg_free,
	.sdo_stob_init	    	= &stob_ad_init,
	.sdo_stob_create_credit	= &stob_ad_create_credit,
	.sdo_stob_create	= &stob_ad_create,
	.sdo_stob_write_credit	= &stob_ad_write_credit,
};

static struct m0_stob_ops stob_ad_ops = {
	.sop_fini	    = &stob_ad_fini,
	.sop_destroy_credit = &stob_ad_destroy_credit,
	.sop_destroy	    = &stob_ad_destroy,
	.sop_io_init	    = &stob_ad_io_init,
	.sop_block_shift    = &stob_ad_block_shift,
};

const struct m0_stob_type m0_stob_ad_type = {
	.st_ops  = &stob_ad_type_ops,
	.st_fidt = {
		.ft_id   = STOB_TYPE_AD,
		.ft_name = "adstob",
	},
};

/*
 * Adieu
 */

static const struct m0_stob_io_op stob_ad_io_op;

static bool stob_ad_endio(struct m0_clink *link);
static void stob_ad_io_release(struct m0_stob_ad_io *aio);

static int stob_ad_io_init(struct m0_stob *stob, struct m0_stob_io *io)
{
	struct m0_stob_ad_io *aio;
	int                   rc;

	M0_PRE(io->si_state == SIS_IDLE);

	M0_ALLOC_PTR(aio);
	if (aio != NULL) {
		io->si_stob_private = aio;
		io->si_op = &stob_ad_io_op;
		aio->ai_fore = io;
		m0_stob_io_init(&aio->ai_back);
		m0_clink_init(&aio->ai_clink, &stob_ad_endio);
		m0_clink_add_lock(&aio->ai_back.si_wait, &aio->ai_clink);
		rc = 0;
	} else {
		M0_STOB_OOM(AD_IO_INIT);
		rc = -ENOMEM;
	}
	return rc;
}

static void stob_ad_io_fini(struct m0_stob_io *io)
{
	struct m0_stob_ad_io *aio = io->si_stob_private;

	stob_ad_io_release(aio);
	m0_clink_del_lock(&aio->ai_clink);
	m0_clink_fini(&aio->ai_clink);
	m0_stob_io_fini(&aio->ai_back);
	m0_free(aio);
}

static void *stob_ad_addr_open(const void *buf, uint32_t shift)
{
	uint64_t addr = (uint64_t)buf;

	M0_PRE(((addr << shift) >> shift) == addr);
	return (void *)(addr << shift);
}

/**
   Helper function to allocate a given number of blocks in the underlying
   storage object.
 */
static int stob_ad_balloc(struct m0_stob_ad_domain *adom, struct m0_dtx *tx,
			  m0_bcount_t count, struct m0_ext *out)
{
	struct m0_ad_balloc *ballroom = adom->sad_ballroom;
	int                  rc;

	count >>= adom->sad_babshift;
	M0_LOG(M0_DEBUG, "count=%lu", (unsigned long)count);
	M0_ASSERT(count > 0);
	rc = ballroom->ab_ops->bo_alloc(ballroom, tx, count, out);
	out->e_start <<= adom->sad_babshift;
	out->e_end   <<= adom->sad_babshift;

	return rc;
}

/**
   Helper function to free a given block extent in the underlying storage
   object.
 */
static int stob_ad_bfree(struct m0_stob_ad_domain *adom, struct m0_dtx *tx,
			 struct m0_ext *ext)
{
	struct m0_ad_balloc *ballroom = adom->sad_ballroom;
	struct m0_ext        tgt;

	M0_PRE((ext->e_start & ((1ULL << adom->sad_babshift) - 1)) == 0);
	M0_PRE((ext->e_end   & ((1ULL << adom->sad_babshift) - 1)) == 0);

	tgt.e_start = ext->e_start >> adom->sad_babshift;
	tgt.e_end   = ext->e_end   >> adom->sad_babshift;
	return ballroom->ab_ops->bo_free(ballroom, tx, &tgt);
}

static int stob_ad_cursor(struct m0_stob_ad_domain *adom,
			  struct m0_stob *obj,
			  uint64_t offset,
			  struct m0_be_emap_cursor *it)
{
	const struct m0_fid *fid = m0_stob_fid_get(obj);
	struct m0_uint128    prefix;
	int                  rc;

	/* XXX make fid2prefix */
	prefix = M0_UINT128(fid->f_container, fid->f_key);
	rc = M0_BE_OP_SYNC_RET_WITH(
		&it->ec_op,
		m0_be_emap_lookup(&adom->sad_adata, &prefix, offset, it),
		bo_u.u_emap.e_rc);
	if (rc != 0 && rc != -ENOENT && rc != -ESRCH)
		M0_STOB_FUNC_FAIL(AD_CURSOR, rc);

	return rc;
}
static uint32_t stob_ad_write_map_count(struct m0_stob_ad_domain *adom,
					struct m0_indexvec *iv)
{
	uint32_t               frags;
	m0_bcount_t            frag_size;
	m0_bcount_t            grp_size;
	bool                   eov;
	struct m0_ivec_cursor  it;

	frags = 0;
	m0_ivec_cursor_init(&it, iv);
	grp_size = adom->sad_blocks_per_group << adom->sad_bshift;

	m0_indexvec_pack(iv);
	do {
		frag_size = min_check(m0_ivec_cursor_step(&it), grp_size);
		M0_ASSERT(frag_size > 0);
		M0_ASSERT(frag_size <= (size_t)~0ULL);

		eov = m0_ivec_cursor_move(&it, frag_size);

		++frags;
	} while (!eov);

	return frags;
}

static void stob_ad_write_credit(struct m0_stob_domain  *dom,
				 struct m0_indexvec     *iv,
				 struct m0_be_tx_credit *accum)
{
	struct m0_stob_ad_domain *adom = stob_ad_domain2ad(dom);
	struct m0_ad_balloc      *ballroom = adom->sad_ballroom;
	int                       blocks;
	int                       bfrags;
	int                       frags;

	blocks = m0_vec_count(&iv->iv_vec) >> adom->sad_babshift;
	bfrags = blocks / adom->sad_blocks_per_group + 1;
	if (ballroom->ab_ops->bo_alloc_credit != NULL)
		ballroom->ab_ops->bo_alloc_credit(ballroom, bfrags, accum);
	frags = stob_ad_write_map_count(adom, iv);
	m0_be_emap_credit(&adom->sad_adata, M0_BEO_PASTE, frags, accum);

#if 0 /* Depends on as_overwrite flag which is always false now. */
	/* for each emap_paste() seg_free() could be called 3 times */
	if (ballroom->ab_ops->bo_free_credit != NULL)
		ballroom->ab_ops->bo_free_credit(ballroom, 3, accum);
#endif
	m0_stob_write_credit(m0_stob_dom_get(adom->sad_bstore), iv, accum);
}

/**
   Releases vectors allocated for back IO.

   @note that back->si_stob.ov_vec.v_count is _not_ freed separately, as it is
   aliased to back->si_user.z_bvec.ov_vec.v_count.

   @see ad_vec_alloc()
 */
static void stob_ad_io_release(struct m0_stob_ad_io *aio)
{
	struct m0_stob_io *back = &aio->ai_back;

	M0_ASSERT(back->si_user.ov_vec.v_count == back->si_stob.iv_vec.v_count);
	m0_free0(&back->si_user.ov_vec.v_count);
	back->si_stob.iv_vec.v_count = NULL;

	m0_free0(&back->si_user.ov_buf);
	m0_free0(&back->si_stob.iv_index);

	back->si_obj = NULL;
}

/**
   Initializes cursors at the beginning of a pass.
 */
static int stob_ad_cursors_init(struct m0_stob_io *io,
				struct m0_stob_ad_domain *adom,
				struct m0_be_emap_cursor *it,
				struct m0_vec_cursor *src,
				struct m0_vec_cursor *dst,
				struct m0_be_emap_caret *map)
{
	int rc;

	rc = stob_ad_cursor(adom, io->si_obj, io->si_stob.iv_index[0], it);
	if (rc == 0) {
		m0_vec_cursor_init(src, &io->si_user.ov_vec);
		m0_vec_cursor_init(dst, &io->si_stob.iv_vec);
		m0_be_emap_caret_init(map, it, io->si_stob.iv_index[0]);
	}
	return rc;
}

/**
   Finalizes the cursors that need finalisation.
 */
static void stob_ad_cursors_fini(struct m0_be_emap_cursor *it,
				 struct m0_vec_cursor *src,
				 struct m0_vec_cursor *dst,
				 struct m0_be_emap_caret *map)
{
	m0_be_emap_caret_fini(map);
	m0_be_emap_close(it);
}

/**
   Allocates back IO buffers after number of fragments has been calculated.

   @see stob_ad_io_release()
 */
static int stob_ad_vec_alloc(struct m0_stob *obj,
			     struct m0_stob_io *back,
			     uint32_t frags)
{
	m0_bcount_t *counts;
	int          rc = 0;

	M0_ASSERT(back->si_user.ov_vec.v_count == NULL);

	if (frags > 0) {
		M0_ALLOC_ARR(counts, frags);
		back->si_user.ov_vec.v_count = counts;
		back->si_stob.iv_vec.v_count = counts;
		M0_ALLOC_ARR(back->si_user.ov_buf, frags);
		M0_ALLOC_ARR(back->si_stob.iv_index, frags);

		back->si_user.ov_vec.v_nr = frags;
		back->si_stob.iv_vec.v_nr = frags;

		if (counts == NULL || back->si_user.ov_buf == NULL ||
		    back->si_stob.iv_index == NULL) {
			M0_STOB_OOM(AD_VEC_ALLOC);
			rc = -ENOMEM;
		}
	}
	return rc;
}

/**
   Constructs back IO for read.

   This is done in two passes:

   @li first, calculate number of fragments, taking holes into account. This
   pass iterates over user buffers list (src), target extents list (dst) and
   extents map (map). Once this pass is completed, back IO vectors can be
   allocated;

   @li then, iterate over the same sequences again. For holes, call memset()
   immediately, for other fragments, fill back IO vectors with the fragment
   description.

   @note assumes that allocation data can not change concurrently.

   @note memset() could become a bottleneck here.

   @note cursors and fragment sizes are measured in blocks.
 */
static int stob_ad_read_launch(struct m0_stob_io *io,
			       struct m0_stob_ad_domain *adom,
			       struct m0_vec_cursor *src,
			       struct m0_vec_cursor *dst,
			       struct m0_be_emap_caret *car)
{
	struct m0_be_emap_cursor *it;
	struct m0_be_emap_seg    *seg;
	struct m0_stob_io        *back;
	struct m0_stob_ad_io     *aio = io->si_stob_private;
	uint32_t                  frags;
	uint32_t                  frags_not_empty;
	uint32_t                  bshift;
	m0_bcount_t               frag_size; /* measured in blocks */
	m0_bindex_t               off;       /* measured in blocks */
	int                       rc;
	int                       i;
	int                       idx;
	bool                      eosrc;
	bool                      eodst;
	int                       eomap;

	M0_PRE(io->si_opcode == SIO_READ);

	bshift = m0_stob_block_shift(adom->sad_bstore);
	it     = car->ct_it;
	seg    = m0_be_emap_seg_get(it);
	back   = &aio->ai_back;

	M0_LOG(M0_DEBUG, "ext="EXT_F" val=0x%llx",
		EXT_P(&seg->ee_ext), (unsigned long long)seg->ee_val);

	frags = frags_not_empty = 0;
	do {
		off = io->si_stob.iv_index[dst->vc_seg] + dst->vc_offset;

		/*
		 * The next fragment starts at the offset off and the extents
		 * car has to be positioned at this offset. There are two ways
		 * to do this:
		 *
		 * * lookup an extent containing off (m0_emap_lookup()), or
		 *
		 * * iterate from the current position (m0_emap_caret_move())
		 *   until off is reached.
		 *
		 * Lookup incurs an overhead of tree traversal, whereas
		 * iteration could become expensive when extents car is
		 * fragmented and target extents are far from each other.
		 *
		 * Iteration is used for now, because when extents car is
		 * fragmented or IO locality of reference is weak, performance
		 * will be bad anyway.
		 *
		 * Note: the code relies on the target extents being in
		 * increasing offset order in dst.
		 */
		M0_ASSERT(off >= car->ct_index);
		eomap = m0_be_emap_caret_move_sync(car, off - car->ct_index);
		if (eomap < 0) {
			M0_STOB_FUNC_FAIL(AD_READ_LAUNCH_1, eomap);
			return eomap;
		}
		M0_ASSERT(!eomap);
		M0_ASSERT(m0_ext_is_in(&seg->ee_ext, off));

		frag_size = min3(m0_vec_cursor_step(src),
				 m0_vec_cursor_step(dst),
				 m0_be_emap_caret_step(car));
		M0_ASSERT(frag_size > 0);
		if (frag_size > (size_t)~0ULL) {
			M0_STOB_FUNC_FAIL(AD_READ_LAUNCH_2, -EOVERFLOW);
			return -EOVERFLOW;
		}

		frags++;
		if (seg->ee_val < AET_MIN)
			frags_not_empty++;

		eosrc = m0_vec_cursor_move(src, frag_size);
		eodst = m0_vec_cursor_move(dst, frag_size);
		eomap = m0_be_emap_caret_move_sync(car, frag_size);
		if (eomap < 0) {
			M0_STOB_FUNC_FAIL(AD_READ_LAUNCH_3, eomap);
			return eomap;
		}

		M0_ASSERT(eosrc == eodst);
		M0_ASSERT(!eomap);
	} while (!eosrc);

	M0_LOG(M0_DEBUG, "frags=%d frags_not_empty=%d",
			(int)frags, (int)frags_not_empty);

	stob_ad_cursors_fini(it, src, dst, car);

	rc = stob_ad_vec_alloc(io->si_obj, back, frags_not_empty);
	if (rc != 0)
		return rc;

	rc = stob_ad_cursors_init(io, adom, it, src, dst, car);
	if (rc != 0)
		return rc;

	for (idx = i = 0; i < frags; ++i) {
		void        *buf;
		m0_bindex_t  off;

		buf = io->si_user.ov_buf[src->vc_seg] + src->vc_offset;
		off = io->si_stob.iv_index[dst->vc_seg] + dst->vc_offset;

		M0_ASSERT(off >= car->ct_index);
		eomap = m0_be_emap_caret_move_sync(car, off - car->ct_index);
		if (eomap < 0) {
			M0_STOB_FUNC_FAIL(AD_READ_LAUNCH_4, eomap);
			return eomap;
		}
		M0_ASSERT(!eomap);
		M0_ASSERT(m0_ext_is_in(&seg->ee_ext, off));

		frag_size = min3(m0_vec_cursor_step(src),
				 m0_vec_cursor_step(dst),
				 m0_be_emap_caret_step(car));

		M0_LOG(M0_DEBUG, "%2d: sz=%lx buf=%p off=%lx "
			"ext="EXT_F" val=%lx",
			idx, (unsigned long)frag_size, buf,
			(unsigned long)off, EXT_P(&seg->ee_ext),
			(unsigned long)seg->ee_val);
		if (seg->ee_val == AET_NONE || seg->ee_val == AET_HOLE) {
			/*
			 * Read of a hole or unallocated space (beyond
			 * end of the file).
			 */
			memset(stob_ad_addr_open(buf, bshift),
			       0, frag_size << bshift);
			io->si_count += frag_size;
		} else {
			M0_ASSERT(seg->ee_val < AET_MIN);

			back->si_user.ov_vec.v_count[idx] = frag_size;
			back->si_user.ov_buf[idx] = buf;

			back->si_stob.iv_index[idx] = seg->ee_val +
				(off - seg->ee_ext.e_start);
			idx++;
		}
		m0_vec_cursor_move(src, frag_size);
		m0_vec_cursor_move(dst, frag_size);
		rc = m0_be_emap_caret_move_sync(car, frag_size);
		if (rc < 0) {
			M0_STOB_FUNC_FAIL(AD_READ_LAUNCH_5, eomap);
			break;
		}
		M0_ASSERT(rc == 0);
	}
	M0_ASSERT(ergo(rc == 0, idx == frags_not_empty));
	return rc;
}

/**
   A linked list of allocated extents.
 */
struct stob_ad_write_ext {
	struct m0_ext             we_ext;
	struct stob_ad_write_ext *we_next;
};

/**
   A cursor over allocated extents.
 */
struct stob_ad_wext_cursor {
	const struct stob_ad_write_ext *wc_wext;
	m0_bcount_t                     wc_done;
};

static void stob_ad_wext_cursor_init(struct stob_ad_wext_cursor *wc,
				     struct stob_ad_write_ext *wext)
{
	wc->wc_wext = wext;
	wc->wc_done = 0;
}

static m0_bcount_t stob_ad_wext_cursor_step(struct stob_ad_wext_cursor *wc)
{
	M0_PRE(wc->wc_wext != NULL);
	M0_PRE(wc->wc_done < m0_ext_length(&wc->wc_wext->we_ext));

	return m0_ext_length(&wc->wc_wext->we_ext) - wc->wc_done;
}

static bool stob_ad_wext_cursor_move(struct stob_ad_wext_cursor *wc,
				     m0_bcount_t count)
{
	while (count > 0 && wc->wc_wext != NULL) {
		m0_bcount_t step;

		step = stob_ad_wext_cursor_step(wc);
		if (count >= step) {
			wc->wc_wext = wc->wc_wext->we_next;
			wc->wc_done = 0;
			count -= step;
		} else {
			wc->wc_done += count;
			count = 0;
		}
	}
	return wc->wc_wext == NULL;
}

/**
   Calculates how many fragments this IO request contains.

   @note extent map and dst are not used here, because write allocates new space
   for data, ignoring existing allocations in the overwritten extent of the
   file.
 */
static uint32_t stob_ad_write_count(struct m0_vec_cursor *src,
				    struct stob_ad_wext_cursor *wc)
{
	m0_bcount_t frag_size;
	bool        eosrc;
	bool        eoext;
	uint32_t    frags = 0;

	do {
		frag_size = min_check(m0_vec_cursor_step(src),
				      stob_ad_wext_cursor_step(wc));
		M0_ASSERT(frag_size > 0);
		M0_ASSERT(frag_size <= (size_t)~0ULL);

		eosrc = m0_vec_cursor_move(src, frag_size);
		eoext = stob_ad_wext_cursor_move(wc, frag_size);

		M0_ASSERT(ergo(eosrc, eoext));
		++frags;
	} while (!eoext);
	return frags;
}

/**
   Fills back IO request with information about fragments.
 */
static void stob_ad_write_back_fill(struct m0_stob_io *io,
				    struct m0_stob_io *back,
				    struct m0_vec_cursor *src,
				    struct stob_ad_wext_cursor *wc)
{
	m0_bcount_t    frag_size;
	uint32_t       idx;
	bool           eosrc;
	bool           eoext;

	idx = 0;
	do {
		void *buf;

		frag_size = min_check(m0_vec_cursor_step(src),
				      stob_ad_wext_cursor_step(wc));

		buf = io->si_user.ov_buf[src->vc_seg] + src->vc_offset;

		back->si_user.ov_vec.v_count[idx] = frag_size;
		back->si_user.ov_buf[idx] = buf;

		back->si_stob.iv_index[idx] =
			wc->wc_wext->we_ext.e_start + wc->wc_done;

		eosrc = m0_vec_cursor_move(src, frag_size);
		eoext = stob_ad_wext_cursor_move(wc, frag_size);
		idx++;
		M0_ASSERT(eosrc == eoext);
	} while (!eoext);
	M0_ASSERT(idx == back->si_stob.iv_vec.v_nr);
}

/**
 * Helper function used by ad_write_map_ext() to free sub-segment "ext" from
 * allocated segment "seg".
 */
static int stob_ad_seg_free(struct m0_dtx *tx,
			    struct m0_stob_ad_domain *adom,
			    const struct m0_be_emap_seg *seg,
			    const struct m0_ext *ext,
			    uint64_t val)
{
	m0_bcount_t   delta = ext->e_start - seg->ee_ext.e_start;
	struct m0_ext tocut = {
		.e_start = val + delta,
		.e_end   = val + delta + m0_ext_length(ext)
	};
	return val < AET_MIN ? stob_ad_bfree(adom, tx, &tocut) : 0;
}

/**
   Inserts allocated extent into AD storage object allocation map, possibly
   overwriting a number of existing extents.

   @param offset - an offset in AD stob name-space;
   @param ext - an extent in the underlying object name-space.

   This function updates extent mapping of AD storage to map an extent in its
   logical name-space, starting with offset to an extent ext in the underlying
   storage object name-space.
 */
static int stob_ad_write_map_ext(struct m0_stob_io *io,
				 struct m0_stob_ad_domain *adom,
				 m0_bindex_t off,
				 struct m0_be_emap_cursor *orig,
				 const struct m0_ext *ext)
{
	int                    result;
	int                    rc = 0;
	struct m0_be_emap_cursor  it;
	/* an extent in the logical name-space to be mapped to ext. */
	struct m0_ext          todo = {
		.e_start = off,
		.e_end   = off + m0_ext_length(ext)
	};

	M0_ENTRY("ext="EXT_F" val=0x%llx", EXT_P(&todo),
		 (unsigned long long)ext->e_start);

	m0_be_op_init(&it.ec_op);
	m0_be_emap_lookup(orig->ec_map, &orig->ec_seg.ee_pre, off, &it);
	m0_be_op_wait(&it.ec_op);
	M0_ASSERT(m0_be_op_state(&it.ec_op) == M0_BOS_SUCCESS);
	result = it.ec_op.bo_u.u_emap.e_rc;
	m0_be_op_fini(&it.ec_op);

	if (result != 0)
		return M0_RC(result);
	/*
	 * Insert a new segment into extent map, overwriting parts of the map.
	 *
	 * Some existing segments are deleted completely, others are
	 * cut. m0_emap_paste() invokes supplied call-backs to notify the caller
	 * about changes in the map.
	 *
	 * Call-backs are used to free space from overwritten parts of the file.
	 *
	 * Each call-back takes a segment argument, seg. seg->ee_ext is a
	 * logical extent of the segment and seg->ee_val is the starting offset
	 * of the corresponding physical extent.
	 */
	m0_be_op_init(&it.ec_op);
	m0_be_emap_paste(&it, &io->si_tx->tx_betx, &todo, ext->e_start,
	 LAMBDA(void, (struct m0_be_emap_seg *seg) {
			/* handle extent deletion. */
			if (stob_ad_stob2ad(io->si_obj)->ad_overwrite) {
				M0_LOG(M0_DEBUG, "del: val=0x%llx",
					(unsigned long long)seg->ee_val);
				M0_ASSERT_INFO(seg->ee_val != ext->e_start,
				"Delete of the same just allocated block");
				rc = rc ?:
				     stob_ad_seg_free(io->si_tx, adom, seg,
						      &seg->ee_ext, seg->ee_val);
			}
		 }),
	 LAMBDA(void, (struct m0_be_emap_seg *seg, struct m0_ext *ext,
		       uint64_t val) {
			/* cut left */
			M0_ASSERT(ext->e_start > seg->ee_ext.e_start);

			seg->ee_val = val;
			if (stob_ad_stob2ad(io->si_obj)->ad_overwrite)
				rc = rc ?:
				     stob_ad_seg_free(io->si_tx, adom, seg,
						      ext, val);
		}),
	 LAMBDA(void, (struct m0_be_emap_seg *seg, struct m0_ext *ext,
		       uint64_t val) {
			/* cut right */
			M0_ASSERT(seg->ee_ext.e_end > ext->e_end);
			if (val < AET_MIN) {
				seg->ee_val = val +
					(ext->e_end - seg->ee_ext.e_start);
				/*
				 * Free physical sub-extent, but only when
				 * sub-extent starts at the left boundary of the
				 * logical extent, because otherwise "cut left"
				 * already freed it.
				 */
				if (stob_ad_stob2ad(io->si_obj)->ad_overwrite &&
				    ext->e_start == seg->ee_ext.e_start)
					rc = rc ?:
					     stob_ad_seg_free(io->si_tx, adom,
							      seg, ext, val);
			} else
				seg->ee_val = val;
		}));
	M0_ASSERT(m0_be_op_state(&it.ec_op) == M0_BOS_SUCCESS);
	result = it.ec_op.bo_u.u_emap.e_rc;
	m0_be_op_fini(&it.ec_op);
	m0_be_emap_close(&it);

	return M0_RC(result ?: rc);
}

static int stob_ad_fol_frag_alloc(struct m0_fol_frag *frag, uint32_t frags)
{
	struct stob_ad_rec_frag *arp;

	M0_PRE(frag != NULL);

	M0_ALLOC_PTR(arp);
	if (arp == NULL)
		return -ENOMEM;
	m0_fol_frag_init(frag, arp, &stob_ad_rec_frag_type);

	arp->arp_seg.ps_segments = frags;

	M0_ALLOC_ARR(arp->arp_seg.ps_old_data, frags);
	if (arp->arp_seg.ps_old_data == NULL) {
		m0_free(arp);
		return -ENOMEM;
	}
	return 0;
}

static void stob_ad_fol_frag_free(struct m0_fol_frag *frag)
{
	struct stob_ad_rec_frag *arp = frag->rp_data;

	m0_free(arp->arp_seg.ps_old_data);
	m0_free(arp);
}

/**
   Updates extent map, inserting newly allocated extents into it.

   @param dst - target extents in AD storage object;
   @param wc - allocated extents.

   Total size of extents in dst and wc is the same, but their boundaries not
   necessary match. Iterate over both sequences at the same time, mapping
   contiguous chunks of AD stob name-space to contiguous chunks of the
   underlying object name-space.

 */
static int stob_ad_write_map(struct m0_stob_io *io,
			     struct m0_stob_ad_domain *adom,
			     struct m0_ivec_cursor *dst,
			     struct m0_be_emap_caret *map,
			     struct stob_ad_wext_cursor *wc,
			     uint32_t frags)
{
	int			 rc;
	m0_bcount_t		 frag_size;
	m0_bindex_t		 off;
	bool			 eodst;
	bool			 eoext;
	struct m0_ext		 todo;
	struct m0_fol_frag	*frag = io->si_fol_frag;
	struct stob_ad_rec_frag	*arp;
	uint32_t		 i = 0;

	rc = stob_ad_fol_frag_alloc(frag, frags);
	if (rc != 0)
		return rc;
	arp = frag->rp_data;
	arp->arp_stob_fid = *m0_stob_fid_get(io->si_obj);
	arp->arp_dom_id   =  m0_stob_domain_id_get(&adom->sad_base);

	do {
		off = m0_ivec_cursor_index(dst);
		frag_size = min_check(m0_ivec_cursor_step(dst),
				      stob_ad_wext_cursor_step(wc));

		todo.e_start = wc->wc_wext->we_ext.e_start + wc->wc_done;
		todo.e_end   = todo.e_start + frag_size;

		M0_ASSERT(i < frags);
		arp->arp_seg.ps_old_data[i] = (struct m0_be_emap_seg) {
			.ee_ext = {
				.e_start = off,
				.e_end   = off + m0_ext_length(&todo)
			},
			.ee_val = todo.e_start,
			.ee_pre = map->ct_it->ec_seg.ee_pre
		};
		++i;

		rc = stob_ad_write_map_ext(io, adom, off, map->ct_it, &todo);
		if (rc != 0)
			break;

		eodst = m0_ivec_cursor_move(dst, frag_size);
		eoext = stob_ad_wext_cursor_move(wc, frag_size);

		M0_ASSERT(eodst == eoext);
	} while (!eodst);

	if (rc == 0)
		m0_fol_frag_add(&io->si_tx->tx_fol_rec, frag);
	else
		stob_ad_fol_frag_free(frag);

	return rc;
}

/**
   Frees wext list.
 */
static void stob_ad_wext_fini(struct stob_ad_write_ext *wext)
{
	struct stob_ad_write_ext *next;

	for (wext = wext->we_next; wext != NULL; wext = next) {
		next = wext->we_next;
		m0_free(wext);
	}
}

/**
   AD write.

   @li allocates space for data to be written (first loop);

   @li calculates number of fragments (ad_write_count());

   @li constructs back IO (ad_write_back_fill());

   @li updates extent map for this AD object with allocated extents
       (ad_write_map()).
 */
static int stob_ad_write_launch(struct m0_stob_io *io,
				struct m0_stob_ad_domain *adom,
				struct m0_vec_cursor *src,
				struct m0_ivec_cursor *dst,
				struct m0_be_emap_caret *map)
{
	m0_bcount_t                 todo;
	uint32_t                    frags;
	int                         rc;
	struct stob_ad_write_ext    head;
	struct stob_ad_write_ext   *wext;
	struct stob_ad_write_ext   *next;
	struct m0_stob_io          *back;
	struct m0_stob_ad_io       *aio = io->si_stob_private;
	struct stob_ad_wext_cursor  wc;

	M0_PRE(io->si_opcode == SIO_WRITE);

	todo = m0_vec_count(&io->si_user.ov_vec);
	M0_ENTRY("op=%d sz=%lu", io->si_opcode, (unsigned long)todo);
	back = &aio->ai_back;
        M0_SET0(&head);
	wext = &head;
	wext->we_next = NULL;
	while (1) {
		m0_bcount_t got;

		rc = stob_ad_balloc(adom, io->si_tx, todo, &wext->we_ext);
		if (rc != 0)
			break;
		got = m0_ext_length(&wext->we_ext);
		M0_ASSERT(todo >= got);
		todo -= got;
		if (todo > 0) {
			M0_ALLOC_PTR(next);
			if (next != NULL) {
				wext->we_next = next;
				wext = next;
			} else {
				M0_STOB_OOM(AD_WRITE_LAUNCH);
				rc = -ENOMEM;
				break;
			}
		} else
			break;
	}

	if (rc == 0) {
		stob_ad_wext_cursor_init(&wc, &head);
		frags = stob_ad_write_count(src, &wc);
		rc = stob_ad_vec_alloc(io->si_obj, back, frags);
		if (rc == 0) {
			struct m0_indexvec *ivec;
			m0_vec_cursor_init(src, &io->si_user.ov_vec);
			stob_ad_wext_cursor_init(&wc, &head);
			stob_ad_write_back_fill(io, back, src, &wc);
			m0_ivec_cursor_init(dst, &io->si_stob);
			stob_ad_wext_cursor_init(&wc, &head);
			ivec = container_of(dst->ic_cur.vc_vec,
					    struct m0_indexvec, iv_vec);
			frags = stob_ad_write_map_count(adom, ivec);
			rc = stob_ad_write_map(io, adom, dst, map, &wc, frags);
		}
	}
	stob_ad_wext_fini(&head);
	return M0_RC(rc);
}

/**
   Launch asynchronous IO.

   Call ad_write_launch() or ad_read_launch() to do the bulk of work, then
   launch back IO just constructed.
 */
static int stob_ad_io_launch(struct m0_stob_io *io)
{
	struct m0_stob_ad_domain *adom;
	struct m0_stob_ad_io     *aio     = io->si_stob_private;
	struct m0_be_emap_cursor  it;
	struct m0_vec_cursor      src;
	struct m0_ivec_cursor     dst;
	struct m0_be_emap_caret   map;
	struct m0_stob_io        *back    = &aio->ai_back;
	int                       rc;
	bool                      wentout = false;

	M0_PRE(io->si_stob.iv_vec.v_nr > 0);
	M0_PRE(!m0_vec_is_empty(&io->si_user.ov_vec));

	/* prefix fragments execution mode is not yet supported */
	M0_ASSERT((io->si_flags & SIF_PREFIX) == 0);
	/* only read-write at the moment */
	M0_ASSERT(io->si_opcode == SIO_READ || io->si_opcode == SIO_WRITE);

	M0_ENTRY("op=%d", io->si_opcode);

	adom = stob_ad_domain2ad(m0_stob_dom_get(io->si_obj));
	rc = stob_ad_cursors_init(io, adom, &it, &src, &dst.ic_cur, &map);
	if (rc != 0)
		return M0_RC(rc);

	back->si_opcode	  = io->si_opcode;
	back->si_flags	  = io->si_flags;
	back->si_fol_frag = io->si_fol_frag;

	switch (io->si_opcode) {
	case SIO_READ:
		rc = stob_ad_read_launch(io, adom, &src, &dst.ic_cur, &map);
		break;
	case SIO_WRITE:
		rc = stob_ad_write_launch(io, adom, &src, &dst, &map);
		break;
	default:
		M0_IMPOSSIBLE("Invalid io type.");
	}
	stob_ad_cursors_fini(&it, &src, &dst.ic_cur, &map);
	if (rc == 0) {
		if (back->si_stob.iv_vec.v_nr > 0) {
			/**
			 * Sorts index vecs in incremental order.
			 * @todo : Needs to check performance impact
			 *        of sorting each stobio on ad stob.
			 */
			m0_stob_iovec_sort(back);
			rc = m0_stob_io_launch(back, adom->sad_bstore,
					       io->si_tx, io->si_scope);
			wentout = rc == 0;
		} else {
			/*
			 * Back IO request was constructed OK, but is empty (all
			 * IO was satisfied from holes). Notify caller about
			 * completion.
			 */
			M0_ASSERT(io->si_opcode == SIO_READ);
			stob_ad_endio(&aio->ai_clink);
		}
	}
	if (!wentout)
		stob_ad_io_release(aio);
	return M0_RC(rc);
}

static bool stob_ad_endio(struct m0_clink *link)
{
	struct m0_stob_ad_io *aio;
	struct m0_stob_io    *io;

	aio = container_of(link, struct m0_stob_ad_io, ai_clink);
	io = aio->ai_fore;

	M0_ASSERT(io->si_state == SIS_BUSY);
	M0_ASSERT(aio->ai_back.si_state == SIS_IDLE);

	io->si_rc     = aio->ai_back.si_rc;
	io->si_count += aio->ai_back.si_count;
	io->si_state  = SIS_IDLE;
	stob_ad_io_release(aio);
	m0_chan_broadcast_lock(&io->si_wait);
	return true;
}

/**
    Implementation of m0_fol_frag_ops::rpo_undo_credit and
                      m0_fol_frag_ops::rpo_redo_credit().
 */
static void
stob_ad_rec_frag_undo_redo_op_cred(const struct m0_fol_frag *frag,
				   struct m0_be_tx_credit   *accum)
{
	struct stob_ad_rec_frag  *arp  = frag->rp_data;
	struct m0_stob_domain    *dom  = m0_stob_domain_find(arp->arp_dom_id);
	struct m0_stob_ad_domain *adom = stob_ad_domain2ad(dom);

	M0_PRE(dom != NULL);
	m0_be_emap_credit(&adom->sad_adata, M0_BEO_UPDATE,
			  arp->arp_seg.ps_segments, accum);
}

/**
    Implementation of m0_fol_frag_ops::rpo_undo and
                      m0_fol_frag_ops::rpo_redo ().
 */
static int stob_ad_rec_frag_undo_redo_op(struct m0_fol_frag *frag,
					 struct m0_be_tx    *tx)
{
	struct stob_ad_rec_frag  *arp  = frag->rp_data;
	struct m0_stob_domain    *dom  = m0_stob_domain_find(arp->arp_dom_id);
	struct m0_stob_ad_domain *adom = stob_ad_domain2ad(dom);
	struct m0_be_emap_seg    *old_data = arp->arp_seg.ps_old_data;
	struct m0_be_emap_cursor  it;
	int		          i;
	int		          rc = 0;

	M0_PRE(dom != NULL);

	for (i = 0; rc == 0 && i < arp->arp_seg.ps_segments; ++i) {
		m0_be_op_init(&it.ec_op);
		m0_be_emap_lookup(&adom->sad_adata,
				  &old_data[i].ee_pre,
				   old_data[i].ee_ext.e_start,
				  &it);
		m0_be_op_wait(&it.ec_op);
		M0_ASSERT(m0_be_op_state(&it.ec_op) == M0_BOS_SUCCESS);
		rc = it.ec_op.bo_u.u_emap.e_rc;
		m0_be_op_fini(&it.ec_op);

		if (rc == 0) {
			M0_LOG(M0_DEBUG, "%3d: ext="EXT_F" val=0x%llx",
				i, EXT_P(&old_data[i].ee_ext),
				(unsigned long long)old_data[i].ee_val);
			rc = M0_BE_OP_SYNC_RET_WITH(
				&it.ec_op,
				m0_be_emap_extent_update(&it, tx, &old_data[i]),
				bo_u.u_emap.e_rc);
			m0_be_emap_close(&it);
		}
	}
	return rc;
}

static const struct m0_stob_io_op stob_ad_io_op = {
	.sio_launch  = stob_ad_io_launch,
	.sio_fini    = stob_ad_io_fini,
};

/** @} end group stobad */

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
