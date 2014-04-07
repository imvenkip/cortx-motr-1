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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 18-Jul-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "be/domain.h"
#include "be/seg0.h"
#include "be/seg.h"

M0_TL_DESCR_DEFINE(zt, "m0_be_domain::bd_0type_list[]", M0_INTERNAL,
			   struct m0_be_0type, b0_linkage, b0_magic,
			   M0_BE_0TYPE_MAGIC, M0_BE_0TYPE_MAGIC);
M0_TL_DEFINE(zt, M0_INTERNAL, struct m0_be_0type);

M0_TL_DESCR_DEFINE(seg, "m0_be_domain::bd_seg_list[]", M0_INTERNAL,
			   struct m0_be_seg, bs_linkage, bs_magic,
			   M0_BE_SEG_MAGIC, M0_BE_SEG_MAGIC);
M0_TL_DEFINE(seg, M0_INTERNAL, struct m0_be_seg);


/**
 * @addtogroup be
 *
 * @{
 */

static void m0_be_domain__0types_fini(struct m0_be_domain *dom);
static int m0_be_domain__0types_init(struct m0_be_domain *dom);

/* check that we're in mkfs mode */
static bool is_mkfs_mode(const struct m0_be_seg *dict)
{
	return dict == NULL || dict->bs_domain->bd_seg0_stob == NULL;
}

static int segobj_opt_iterate(struct m0_be_seg         *dict,
			      const struct m0_be_0type *objtype,
			      struct m0_buf            *opt,
			      char                    **suffix,
			      bool                      begin)
{
	struct m0_buf *buf;
	int	       rc;

	if (is_mkfs_mode(dict))
		return begin;

	rc = begin ?
		m0_be_seg_dict_begin(dict, objtype->b0_name,
				     (const char **)suffix, (void**) &buf) :
		m0_be_seg_dict_next(dict, objtype->b0_name, *suffix,
				    (const char**) suffix, (void**) &buf);

	if (rc == -ENOENT)
		return 0;
	else if (rc == 0) {
		if (buf != NULL)
			*opt = *buf;
		return +1;
	}

	return rc;
}

static int segobj_opt_next(struct m0_be_seg         *dict,
			   const struct m0_be_0type *objtype,
			   struct m0_buf            *opt,
			   char                    **suffix)
{
	return segobj_opt_iterate(dict, objtype, opt, suffix, false);
}

static int segobj_opt_begin(struct m0_be_seg         *dict,
			    const struct m0_be_0type *objtype,
			    struct m0_buf            *opt,
			    char                    **suffix)
{
	return segobj_opt_iterate(dict, objtype, opt, suffix, true);
}

static int _0types_visit(struct m0_be_domain *dom, bool init)
{
	int		    rc = 0;
	int                 left;
	struct m0_buf       opt;
	struct m0_be_seg   *dict;
	struct m0_be_0type *objtype;
	char               *suffix;

	dict = m0_be_domain_seg0_get(dom);

        m0_tl_for(zt, &dom->bd_0type_list, objtype) {
		for (left = segobj_opt_begin(dict, objtype, &opt, &suffix);
		     left > 0 && rc == 0;
		     left = segobj_opt_next(dict, objtype, &opt, &suffix)) {
			rc = init ? objtype->b0_init(dom, suffix, &opt) :
				(objtype->b0_fini(dom, suffix, &opt), 0);

		}
	} m0_tl_endfor;

	return rc;
}

static int m0_be_domain__seg0_init(struct m0_be_domain *dom)
{
	struct m0_be_seg *seg;
	int		  rc;

	M0_PRE(dom->bd_seg0_stob != NULL);

	M0_ALLOC_PTR(seg);
	if (seg == NULL)
		return -ENOMEM;

	m0_be_seg_init(seg, dom->bd_seg0_stob, dom);
	rc = m0_be_seg_open(seg);
	if (rc != 0) {
		m0_free(seg);
		return rc;
	}

	m0_be_seg_dict_init(seg);
	seg_tlist_add(&dom->bd_seg_list, seg);

	return rc;
}

static void m0_be_domain__seg0_fini(struct m0_be_domain *dom)
{
	struct m0_be_seg *seg;

	M0_PRE(dom->bd_seg0_stob != NULL);

	seg = m0_be_domain_seg0_get(dom);
	seg_tlist_del(seg);
	m0_be_seg_dict_fini(seg);
	m0_be_seg_close(seg);
	m0_be_seg_fini(seg);
	m0_free(seg);
}

static int m0_be_domain__0types_init(struct m0_be_domain *dom)
{
	int		    rc;

	M0_PRE(m0_be_domain_is_locked(dom));

	if (dom->bd_seg0_stob != NULL) { /* means mkfs */
		rc = m0_be_domain__seg0_init(dom);
		if (rc != 0)
			return rc;
	}

	rc = _0types_visit(dom, true);
	if (rc != 0)
		m0_be_domain__0types_fini(dom);

	return rc;
}

static void m0_be_domain__0types_fini(struct m0_be_domain *dom)
{
	M0_PRE(m0_be_domain_is_locked(dom));

	(void) _0types_visit(dom, false);

	if (dom->bd_seg0_stob != NULL)
		m0_be_domain__seg0_fini(dom);
}

M0_INTERNAL int m0_be_domain_init(struct m0_be_domain *dom)
{
	zt_tlist_init(&dom->bd_0type_list);
	seg_tlist_init(&dom->bd_seg_list);

	return 0;
}

M0_INTERNAL int m0_be_domain_start(struct m0_be_domain *dom,
				   struct m0_be_domain_cfg *cfg)
{
	struct m0_be_engine *en = &dom->bd_engine;
	int		     rc;

	dom->bd_cfg = *cfg;

	rc = m0_be_domain__0types_init(dom);
	if (rc != 0)
		return rc;

	rc = m0_be_engine_init(en, &dom->bd_cfg.bc_engine);
	if (rc != 0) {
		m0_be_domain__0types_fini(dom);
		return rc;
	}

	rc = m0_be_engine_start(en);

	return rc;
}

M0_INTERNAL void m0_be_domain_fini(struct m0_be_domain *dom)
{
	struct m0_be_0type *zt;
	struct m0_be_seg   *seg;

	m0_be_engine_stop(&dom->bd_engine);
	m0_be_engine_fini(&dom->bd_engine);
	m0_be_domain__0types_fini(dom);

	while((zt = zt_tlist_head(&dom->bd_0type_list)) != NULL)
		zt_tlist_del(zt);

	while((seg = seg_tlist_head(&dom->bd_seg_list)) != NULL)
		seg_tlist_del(seg);

	zt_tlist_fini(&dom->bd_0type_list);
	seg_tlist_fini(&dom->bd_seg_list);
}

M0_INTERNAL struct m0_be_engine *m0_be_domain_engine(struct m0_be_domain *dom)
{
	return &dom->bd_engine;
}

M0_INTERNAL
struct m0_be_seg *m0_be_domain_seg0_get(const struct m0_be_domain *dom)
{
	return seg_tlist_head(&dom->bd_seg_list);
}

M0_INTERNAL bool m0_be_domain_is_locked(const struct m0_be_domain *dom)
{
	/* XXX: return m0_mutex_is_locked(&dom->bd_engine.eng_lock); */
	return true;
}

#undef M0_TRACE_SUBSYSTEM
/** @} end of be group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
