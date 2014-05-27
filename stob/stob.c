/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 04/28/2010
 */

/*
 * Define the ADDB types in this file.
 */
#undef M0_ADDB_CT_CREATE_DEFINITION
#define M0_ADDB_CT_CREATE_DEFINITION
#undef M0_ADDB_RT_CREATE_DEFINITION
#define M0_ADDB_RT_CREATE_DEFINITION
#include "stob/stob_addb.h"

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_STOB
#include "lib/trace.h"

#include "lib/misc.h"    /* M0_SET0 */
#include "lib/arith.h"   /* M0_3WAY */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/string.h"  /* m0_strdup */
#include "lib/types.h"	 /* PRIu64 */
#include "mero/magic.h"

#include "stob/type.h"
#include "stob/domain.h"
#include "stob/stob.h"
#include "stob/stob_internal.h"

/**
   @addtogroup stob
   @{
 */

M0_INTERNAL struct m0_stob_cache *
m0_stob_domain__cache(struct m0_stob_domain *dom)
{
	return &dom->sd_cache;
}

M0_INTERNAL struct m0_stob *
m0_stob_domain__stob_alloc(struct m0_stob_domain *dom, uint64_t stob_key)
{
	return dom->sd_ops->sdo_stob_alloc(dom, stob_key);
}

M0_INTERNAL void m0_stob_domain__stob_free(struct m0_stob_domain *dom,
					   struct m0_stob *stob)
{
	dom->sd_ops->sdo_stob_free(dom, stob);
}

/** @todo move allocation out of cache lock if needed */
M0_INTERNAL int m0_stob_find_by_key(struct m0_stob_domain *dom,
				    uint64_t stob_key,
				    struct m0_stob **out)
{
	struct m0_stob_cache *cache = m0_stob_domain__cache(dom);
	struct m0_stob	     *stob;

	m0_stob_cache_lock(cache);
	stob = m0_stob_cache_lookup(cache, stob_key);
	if (stob != NULL)
		M0_CNT_INC(stob->so_ref);
	else {
		stob = m0_stob_domain__stob_alloc(dom, stob_key);
		if (stob != NULL) {
			/* XXX M0_PRE(M0_IS_ZEROED(stob)); */
			/* so_ops is set by domain */
			/* so_private is set by domain */
			stob->so_domain = dom;
			m0_stob__fid_set(stob, dom, stob_key);
			stob->so_state = CSS_UNKNOWN;
			stob->so_ref = 1;
			m0_stob_cache_add(cache, stob);
		}
	}
	m0_stob_cache_unlock(cache);

	*out = stob;
	return stob == NULL ? -ENOMEM : 0;
}

M0_INTERNAL int m0_stob_find(struct m0_fid *fid, struct m0_stob **out)
{
	struct m0_stob_domain *dom = m0_stob_domain_find_by_stob_fid(fid);

	return dom == NULL ? -EINVAL :
	       m0_stob_find_by_key(dom, m0_stob_fid_key_get(fid), out);
}

M0_INTERNAL int m0_stob_lookup_by_key(struct m0_stob_domain *dom,
				      uint64_t stob_key,
				      struct m0_stob **out)
{
	struct m0_stob_cache *cache = m0_stob_domain__cache(dom);
	struct m0_stob	     *stob;

	m0_stob_cache_lock(cache);
	stob = m0_stob_cache_lookup(cache, stob_key);
	if (stob != NULL)
		M0_CNT_INC(stob->so_ref);
	m0_stob_cache_unlock(cache);

	*out = stob;
	return stob == NULL ? -ENOENT : 0;
}

M0_INTERNAL int m0_stob_lookup(struct m0_fid *fid, struct m0_stob **out)
{
	struct m0_stob_domain *dom = m0_stob_domain_find_by_stob_fid(fid);

	return dom == NULL ? -EINVAL :
	       m0_stob_lookup_by_key(dom, m0_stob_fid_key_get(fid), out);
}

M0_INTERNAL int m0_stob_locate(struct m0_stob *stob)
{
	struct m0_stob_domain *dom = m0_stob_dom_get(stob);
	int		       rc;

	M0_PRE(stob->so_ref > 0);
	M0_PRE(m0_stob_state_get(stob) == CSS_UNKNOWN);

	rc = dom->sd_ops->sdo_stob_init(stob, dom, m0_stob_key_get(stob));
	m0_stob__state_set(stob,
			   rc == 0	 ? CSS_EXISTS :
			   rc == -ENOENT ? CSS_NOENT : m0_stob_state_get(stob));
	return M0_IN(rc, (0, -ENOENT)) ? 0 : rc;
}

M0_INTERNAL void m0_stob_create_credit(struct m0_stob_domain *dom,
				       struct m0_be_tx_credit *accum)
{
	dom->sd_ops->sdo_stob_create_credit(dom, accum);
}

M0_INTERNAL int m0_stob_create(struct m0_stob *stob,
			       struct m0_dtx *dtx,
			       const char *str_cfg)
{
	struct m0_stob_domain		*dom	  = m0_stob_dom_get(stob);
	const struct m0_stob_domain_ops *dom_ops  = dom->sd_ops;
	uint64_t			 stob_key = m0_stob_key_get(stob);
	void				*cfg;
	int				 rc;

	M0_ENTRY();
	M0_PRE(stob->so_ref > 0);

	rc = m0_stob_state_get(stob) == CSS_UNKNOWN ? m0_stob_locate(stob) : 0;

	rc = rc ?: dom_ops->sdo_stob_cfg_parse(str_cfg, &cfg);
	if (rc == 0) {
		rc = m0_stob_state_get(stob) == CSS_EXISTS ? -EEXIST :
		    dom->sd_ops->sdo_stob_create(stob, dom, dtx, stob_key, cfg);
		dom_ops->sdo_stob_cfg_free(cfg);
	}
	m0_stob__state_set(stob,
			   rc == 0 ? CSS_EXISTS : m0_stob_state_get(stob));
	return M0_RC(rc);
}

M0_INTERNAL int m0_stob_destroy_credit(struct m0_stob *stob,
				       struct m0_be_tx_credit *accum)
{
	return stob->so_ops->sop_destroy_credit(stob, accum);
}

M0_INTERNAL int m0_stob_destroy(struct m0_stob *stob, struct m0_dtx *dtx)
{
	int rc;

	M0_ENTRY();
	M0_ASSERT_INFO(stob->so_ref == 1,
		       "stob->so_ref = %"PRIu64, stob->so_ref);

	rc = m0_stob_state_get(stob) == CSS_UNKNOWN ? m0_stob_locate(stob) : 0;
	if (rc != 0)
		return rc;
	M0_ASSERT(m0_stob_state_get(stob) == CSS_EXISTS);
	rc = stob->so_ops->sop_destroy(stob, dtx);
	if (rc == 0 || rc == -EAGAIN) {
		if (rc == 0)
			m0_stob__state_set(stob, CSS_NOENT);
		m0_stob_put(stob);
		rc = 0;
	}
	return M0_RC(rc);
}

M0_INTERNAL void m0_stob_write_credit(struct m0_stob_domain *dom,
				      struct m0_indexvec *iv,
				      struct m0_be_tx_credit *accum)
{
	dom->sd_ops->sdo_stob_write_credit(dom, iv, accum);
}

M0_INTERNAL uint64_t m0_stob_dom_id_get(struct m0_stob *stob)
{
	return m0_stob_fid_dom_id_get(m0_stob_fid_get(stob));
}

M0_INTERNAL uint64_t m0_stob_key_get(struct m0_stob *stob)
{
	return m0_stob_fid_key_get(m0_stob_fid_get(stob));
}

M0_INTERNAL const struct m0_fid *m0_stob_fid_get(struct m0_stob *stob)
{
	return &stob->so_fid;
}

M0_INTERNAL uint64_t m0_stob_fid_dom_id_get(const struct m0_fid *stob_fid)
{
	return stob_fid->f_container;
}

M0_INTERNAL uint64_t m0_stob_fid_key_get(const struct m0_fid *stob_fid)
{
	return stob_fid->f_key;
}

M0_INTERNAL enum m0_stob_state m0_stob_state_get(struct m0_stob *stob)
{
	return stob->so_state;
}

M0_INTERNAL uint32_t m0_stob_block_shift(struct m0_stob *stob)
{
	return stob->so_ops->sop_block_shift(stob);
}

M0_INTERNAL void m0_stob_get(struct m0_stob *stob)
{
	struct m0_stob_cache *cache;

	cache = m0_stob_domain__cache(m0_stob_dom_get(stob));

	m0_stob_cache_lock(cache);
	M0_ASSERT(stob->so_ref > 0);
	M0_CNT_INC(stob->so_ref);
	m0_stob_cache_unlock(cache);
}

M0_INTERNAL void m0_stob_put(struct m0_stob *stob)
{
	struct m0_stob_cache *cache;

	cache = m0_stob_domain__cache(m0_stob_dom_get(stob));

	m0_stob_cache_lock(cache);
	M0_CNT_DEC(stob->so_ref);
	if (stob->so_ref == 0)
		m0_stob_cache_idle(cache, stob);
	m0_stob_cache_unlock(cache);
}

M0_INTERNAL void m0_stob__fid_set(struct m0_stob *stob,
				  struct m0_stob_domain *dom,
				  uint64_t stob_key)
{
	stob->so_fid = (struct m0_fid){
		.f_container = m0_stob_domain_id_get(dom),
		.f_key	     = stob_key,
	};
}

M0_INTERNAL void m0_stob__cache_evict(struct m0_stob *stob)
{
	struct m0_stob_domain *dom = m0_stob_dom_get(stob);

	stob->so_ops->sop_fini(stob);
	dom->sd_ops->sdo_stob_free(dom, stob);
}

M0_INTERNAL void m0_stob__key_set(struct m0_stob *stob, uint64_t stob_key)
{
	stob->so_fid.f_key = stob_key;
}

M0_INTERNAL void m0_stob__state_set(struct m0_stob *stob,
				    enum m0_stob_state state)
{
	stob->so_state = state;
}

M0_INTERNAL struct m0_stob_domain *m0_stob_dom_get(struct m0_stob *stob)
{
	return stob->so_domain;
}

M0_INTERNAL void m0_stob_fid_make(struct m0_fid *stob_fid,
				  uint64_t dom_id,
				  uint64_t stob_key)
{
	*stob_fid = M0_FID_INIT(dom_id, stob_key);
}

/** @} end group stob */
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
