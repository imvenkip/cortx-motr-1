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
#include "lib/locality.h"	/* m0_locality0_get */
#include "lib/string.h"		/* m0_streq */
#include "module/instance.h"	/* m0_get */
#include "be/domain.h"
#include "be/seg0.h"
#include "be/seg.h"
#include "be/seg_internal.h"	/* m0_be_seg_hdr */
#include "stob/stob.h"		/* m0_stob_find_by_key */
#include "stob/domain.h"	/* m0_stob_domain_init */

M0_TL_DESCR_DEFINE(zt, "m0_be_domain::bd_0type_list[]", M0_INTERNAL,
			   struct m0_be_0type, b0_linkage, b0_magic,
			   M0_BE_0TYPE_MAGIC, M0_BE_0TYPE_MAGIC);
M0_TL_DEFINE(zt, static, struct m0_be_0type);

M0_TL_DESCR_DEFINE(seg, "m0_be_domain::bd_seg_list[]", M0_INTERNAL,
			   struct m0_be_seg, bs_linkage, bs_magic,
			   M0_BE_SEG_MAGIC, M0_BE_SEG_MAGIC);
M0_TL_DEFINE(seg, static, struct m0_be_seg);


/**
 * @addtogroup be
 *
 * @{
 */

static void be_domain_lock(struct m0_be_domain *dom)
{
	m0_mutex_lock(&dom->bd_lock);
}

static void be_domain_unlock(struct m0_be_domain *dom)
{
	m0_mutex_unlock(&dom->bd_lock);
}

static int segobj_opt_iterate(struct m0_be_seg         *dict,
			      const struct m0_be_0type *objtype,
			      struct m0_buf            *opt,
			      char                    **suffix,
			      bool                      begin)
{
	struct m0_buf *buf;
	int	       rc;

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

static const char *id_cut(const char *prefix, const char *key)
{
	size_t len;
	char  *p;

	if (key == NULL)
		return key;

	p = strstr(key, prefix);
	len = strlen(prefix);

	return p == NULL || len >= strlen(key) ? NULL : p + len;
}

static int _0types_visit(struct m0_be_domain *dom, bool init)
{
	int		    rc = 0;
	int                 left;
	char               *suffix;
	const char         *id;
	struct m0_buf       opt;
	struct m0_be_seg   *dict;
	struct m0_be_0type *objtype;

	dict = m0_be_domain_seg0_get(dom);

        m0_tl_for(zt, &dom->bd_0type_list, objtype) {
		for (left = segobj_opt_begin(dict, objtype, &opt, &suffix);
		     left > 0 && rc == 0;
		     left = segobj_opt_next(dict, objtype, &opt, &suffix)) {
			id = id_cut(objtype->b0_name, suffix);
			rc = init ? objtype->b0_init(dom, id, &opt) :
				(objtype->b0_fini(dom, id, &opt), 0);

		}
	} m0_tl_endfor;

	return rc;
}

static int be_domain_stob_open(struct m0_be_domain  *dom,
			       uint64_t		     stob_key,
			       struct m0_stob	   **out,
			       bool		     create)
{
	int rc;

	rc = m0_stob_find_by_key(dom->bd_stob_domain, stob_key, out);
	if (rc == 0) {
		rc = m0_stob_state_get(*out) == CSS_UNKNOWN ?
		     m0_stob_locate(*out) : 0;
		rc = rc ?: create && m0_stob_state_get(*out) == CSS_NOENT ?
		     m0_stob_create(*out, NULL, NULL) : 0;
		rc = rc ?: m0_stob_state_get(*out) == CSS_EXISTS ? 0 : -ENOENT;
		if (rc != 0)
			m0_stob_put(*out);
	}
	M0_POST(ergo(rc == 0, m0_stob_state_get(*out) == CSS_EXISTS));
	return M0_RC(rc);
}

static int be_domain_seg_structs_create(struct m0_be_domain *dom,
					struct m0_be_tx	    *tx,
					struct m0_be_seg    *seg)
{
	struct m0_be_tx_credit	cred = {};
	struct m0_be_tx		tx_ = {};
	struct m0_sm_group     *grp = m0_locality0_get()->lo_grp;
	bool			use_local_tx = tx == NULL;
	bool			tx_is_open;
	int			rc;

	if (use_local_tx) {
		tx = &tx_;
		m0_sm_group_lock(grp);
		m0_be_tx_init(tx, 0, dom, grp, NULL, NULL, NULL, NULL);
		m0_be_allocator_credit(m0_be_seg_allocator(seg), M0_BAO_CREATE,
				       0, 0, &cred);
		m0_be_seg_dict_create_credit(seg, &cred);
		m0_be_tx_prep(tx, &cred);
		rc = m0_be_tx_open_sync(tx);
		tx_is_open = rc == 0;
	} else {
		rc = 0;
		tx_is_open = false;
	}
	rc = rc ?: m0_be_allocator_create(m0_be_seg_allocator(seg), tx);
	if (rc == 0)
		m0_be_seg_dict_create(seg, tx);
	if (use_local_tx) {
		if (tx_is_open)
			m0_be_tx_close_sync(tx);
		m0_be_tx_fini(tx);
		m0_sm_group_unlock(grp);
	}
	return M0_RC(rc);
}

/**
 * @post ergo(!destroy, rc == 0)
 */
static int be_domain_seg_close(struct m0_be_domain *dom,
			       struct m0_be_seg	   *seg,
			       bool		    destroy)
{
	int rc;

	be_domain_lock(dom);
	seg_tlink_del_fini(seg);
	be_domain_unlock(dom);

	m0_be_seg_dict_fini(seg);
	m0_be_allocator_fini(m0_be_seg_allocator(seg));
	m0_be_seg_close(seg);
	rc = destroy ? m0_be_seg_destroy(seg) : 0;
	m0_be_seg_fini(seg);
	return M0_RC(rc);
}

static int be_domain_seg_open(struct m0_be_domain *dom,
			      struct m0_be_seg	  *seg,
			      uint64_t		   stob_key)
{
	struct m0_stob *stob;
	int		rc;

	rc = be_domain_stob_open(dom, stob_key, &stob, false);
	if (rc == 0) {
		m0_be_seg_init(seg, stob, dom);
		m0_stob_put(stob);
		rc = m0_be_seg_open(seg);
		if (rc == 0) {
			(void)m0_be_allocator_init(m0_be_seg_allocator(seg),
						   seg);
			m0_be_seg_dict_init(seg);

			be_domain_lock(dom);
			seg_tlink_init_at_tail(seg, &dom->bd_seg_list);
			be_domain_unlock(dom);
		} else {
			m0_be_seg_fini(seg);
		}
	}
	return M0_RC(rc);
}

static int be_domain_seg_destroy(struct m0_be_domain *dom,
				 uint64_t	      seg_id)
{
	struct m0_be_seg seg;

	return be_domain_seg_open(dom, &seg, seg_id) ?:
	       be_domain_seg_close(dom, &seg, true);
}

static int be_domain_seg_create(struct m0_be_domain		 *dom,
				struct m0_be_tx			 *tx,
				struct m0_be_seg		 *seg,
				const struct m0_be_0type_seg_cfg *seg_cfg)
{
	struct m0_stob *stob;
	int		rc;
	int		rc1;

	rc = be_domain_stob_open(dom, seg_cfg->bsc_stob_key, &stob, true);
	if (rc != 0)
		goto out;
	m0_be_seg_init(seg, stob, dom);
	m0_stob_put(stob);
	rc = m0_be_seg_create(seg, seg_cfg->bsc_size, seg_cfg->bsc_addr);
	m0_be_seg_fini(seg);
	if (rc != 0)
		goto out;
	rc = be_domain_seg_open(dom, seg, seg_cfg->bsc_stob_key);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "can't open segment after successful "
		       "creation. seg_cfg->bsc_stob_key = %"PRIu64", rc = %d",
		       seg_cfg->bsc_stob_key, rc);
		goto seg_destroy;
	}
	goto out;

seg_destroy:
	rc1 = be_domain_seg_destroy(dom, seg_cfg->bsc_stob_key);
	if (rc1 != 0) {
		M0_LOG(M0_ERROR, "can't destroy segment after successful "
		       "creation. seg_cfg->bsc_stob_key = %"PRIu64", rc = %d",
		       seg_cfg->bsc_stob_key, rc1);
	}
out:
	return M0_RC(rc);
}

static int be_0type_seg_init(struct m0_be_domain *dom,
			     const char		 *suffix,
			     const struct m0_buf *data)
{
	struct m0_be_0type_seg_cfg *cfg =
			(struct m0_be_0type_seg_cfg *)data->b_addr;
	struct m0_be_seg	   *seg;
	int			    rc;

	M0_ENTRY("suffix='%s', stob_key=%"PRIu64, suffix, cfg->bsc_stob_key);

	/* seg0 is already initialized */
	if (m0_streq(suffix, "0"))
		return 0;

	M0_ALLOC_PTR(seg);
	if (seg != NULL) {
	       rc = be_domain_seg_open(dom, seg, cfg->bsc_stob_key);
	       if (rc != 0)
		       m0_free(seg);
	}
	rc = seg == NULL ? -ENOMEM : rc;
	return M0_RC(rc);
}

static void be_0type_seg_fini(struct m0_be_domain *dom,
			      const char	  *suffix,
			      const struct m0_buf *data)
{
	struct m0_be_seg *seg;

	M0_ENTRY("dom: %p, suffix: %s", dom, suffix);

	if (m0_streq(suffix, "0")) /* seg0 is finied separately */
		return;

	seg = m0_be_domain_seg_by_id(dom, m0_strtou64(suffix, NULL, 10));
	M0_ASSERT(seg != NULL);
	M0_LOG(M0_DEBUG, "seg: %p, suffix: %s", seg, suffix);

	be_domain_seg_close(dom, seg, false);
	m0_free(seg);
	M0_LEAVE();
}

/*
 * Current implementations assumes
 * m0_be_seg::bs_id == m0_stob_key_get(seg->bs_stob)
 */
static const struct m0_be_0type m0_be_0type_seg = {
	.b0_name = "M0_BE:SEG",
	.b0_init = be_0type_seg_init,
	.b0_fini = be_0type_seg_fini,
};

static int be_domain_log_init(struct m0_be_domain *dom,
			      const struct m0_be_0type_log_cfg *log_cfg,
			      bool create)
{
	struct m0_be_log *log = m0_be_domain_log(dom);
	struct m0_stob	 *log_stob;
	int		  rc;

	M0_ENTRY("BE dom = %p, log stob key = %"PRIu64,
		 dom, log_cfg->blc_stob_key);

	rc = be_domain_stob_open(dom, log_cfg->blc_stob_key, &log_stob, create);
	if (rc == 0) {
		m0_be_log_init(log, log_stob, m0_be_engine_got_log_space_cb);
		m0_stob_put(log_stob);
		rc = m0_be_log_create(log, log_cfg->blc_size);
	}
	return M0_RC(rc);
}

static void be_domain_log_fini(struct m0_be_domain *dom)
{
	struct m0_be_engine *en = m0_be_domain_engine(dom);

	m0_be_log_fini(&en->eng_log);
}

static int be_0type_log_init(struct m0_be_domain *dom,
			     const char *suffix,
			     const struct m0_buf *data)
{
	const struct m0_be_0type_log_cfg *log_cfg;

	M0_ASSERT_INFO(data->b_nob == sizeof(*log_cfg),
		       "data->b_nob = %lu, sizeof(*log_cfg) = %zu",
		       data->b_nob, sizeof(*log_cfg));

	/* Log is already initalized in mkfs mode */
	if (dom->bd_cfg.bc_mkfs_mode)
		return 0;

	log_cfg = (const struct m0_be_0type_log_cfg *)data->b_addr;
	return M0_RC(be_domain_log_init(dom, log_cfg, false));
}

static void be_0type_log_fini(struct m0_be_domain *dom,
			      const char *suffix,
			      const struct m0_buf *data)
{
	M0_ENTRY();
	be_domain_log_fini(dom);
	M0_LEAVE();
}

static const struct m0_be_0type m0_be_0type_log = {
	.b0_name = "M0_BE:LOG",
	.b0_init = be_0type_log_init,
	.b0_fini = be_0type_log_fini,
};

M0_INTERNAL int m0_be_domain_init(struct m0_be_domain *dom)
{
	zt_tlist_init(&dom->bd_0type_list);
	seg_tlist_init(&dom->bd_seg_list);
	m0_mutex_init(&dom->bd_lock);

	dom->bd_0type_log = m0_be_0type_log;
	dom->bd_0type_seg = m0_be_0type_seg;
	m0_be_0type_register(dom, &dom->bd_0type_log);
	m0_be_0type_register(dom, &dom->bd_0type_seg);

	return 0;
}

static void be_domain_mkfs_progress(struct m0_be_domain *dom,
				    const char		*fmt,
				    ...)
{
	unsigned stage_nr = dom->bd_mkfs_stage_nr;
	unsigned stage	  = dom->bd_mkfs_stage;
	va_list	 ap;
	char	 msg[256];
	size_t	 len;

	va_start(ap, fmt);
	len = vsnprintf(msg, ARRAY_SIZE(msg), fmt, ap);
	va_end(ap);

	if (len < 0 || len >= ARRAY_SIZE(msg))
		msg[0] = '\0';

	M0_LOG(M0_DEBUG, "mkfs: stages_nr = %u, stage index = %u: %s",
	       stage_nr, stage, (const char *)msg);
	if (dom->bd_cfg.bc_mkfs_progress_cb != NULL)
		dom->bd_cfg.bc_mkfs_progress_cb(stage, stage_nr, msg);
	++dom->bd_mkfs_stage;
}

static int be_domain_mkfs_seg0_log(struct m0_be_domain		    *dom,
				   const struct m0_be_0type_seg_cfg *seg0_cfg,
				   const struct m0_be_0type_log_cfg *log_cfg)
{
	struct m0_be_tx_credit	cred	     = {};
	const struct m0_buf	seg0_cfg_buf = M0_BUF_INIT_PTR_CONST(seg0_cfg);
	const struct m0_buf	log_cfg_buf  = M0_BUF_INIT_PTR_CONST(log_cfg);
	struct m0_sm_group     *grp	     = m0_locality0_get()->lo_grp;
	struct m0_be_tx		tx	     = {};
	int			rc;

	M0_ENTRY();

	m0_sm_group_lock(grp);
	m0_be_tx_init(&tx, 0, dom, grp, NULL, NULL, NULL, NULL);
	m0_be_0type_add_credit(dom, &dom->bd_0type_log, "0",
			       &log_cfg_buf, &cred);
	m0_be_0type_add_credit(dom, &dom->bd_0type_seg, "0",
			       &seg0_cfg_buf, &cred);
	m0_be_tx_prep(&tx, &cred);
	rc = m0_be_tx_exclusive_open_sync(&tx);
	if (rc == 0) {
		rc = m0_be_0type_add(&dom->bd_0type_log, dom, &tx, "0",
				     &log_cfg_buf);
		if (rc == 0) {
			rc = m0_be_0type_add(&dom->bd_0type_seg, dom, &tx, "0",
					     &seg0_cfg_buf);
		}
		m0_be_tx_close_sync(&tx);
	}
	m0_be_tx_fini(&tx);
	m0_sm_group_unlock(grp);
	return M0_RC(rc);
}

static int be_domain_start_normal_pre(struct m0_be_domain     *dom,
				      struct m0_be_domain_cfg *cfg)
{
	struct m0_be_seg *seg0 = m0_be_domain_seg0_get(dom);
	int		  rc;

	rc = m0_stob_domain_init(cfg->bc_stob_domain_location,
				 cfg->bc_stob_domain_cfg_init,
				 &dom->bd_stob_domain);
	if (rc == 0) {
		rc = be_domain_seg_open(dom, seg0,
					dom->bd_cfg.bc_seg0_stob_key);
		if (rc == 0) {
			rc = _0types_visit(dom, true);
			if (rc != 0)
				be_domain_seg_close(dom, seg0, false);
		}
	}
	return M0_RC(rc);
}

static void be_domain_stop_normal_pre(struct m0_be_domain *dom)
{
	(void)_0types_visit(dom, false);
	be_domain_seg_close(dom, m0_be_domain_seg0_get(dom), false);
}

static int be_domain_start_mkfs_pre(struct m0_be_domain	    *dom,
				    struct m0_be_domain_cfg *cfg)
{
	int rc;
	int rc1;

	dom->bd_mkfs_stage    = 0;
	dom->bd_mkfs_stage_nr = 4 /* in be_domain_start_mkfs_pre() */ +
				4 /* in be_domain_start_mkfs_post() */ +
				cfg->bc_seg_nr /* 1 per segment created */;
	be_domain_mkfs_progress(dom, "destroying stob domain: location = %s",
				cfg->bc_stob_domain_location);
	rc = m0_stob_domain_destroy_location(cfg->bc_stob_domain_location);
	/* can't use ?: here because first argument should be constant */
	if (M0_IN(rc, (-ENOENT, 0))) {
		M0_LOG(M0_DEBUG, "rc = %d", rc);
	} else {
		M0_LOG(M0_WARN, "rc = %d", rc);
	}
	be_domain_mkfs_progress(dom, "creating stob domain: location = %s",
				cfg->bc_stob_domain_location);
	rc = m0_stob_domain_create(cfg->bc_stob_domain_location,
				   cfg->bc_stob_domain_cfg_init,
				   cfg->bc_stob_domain_key,
				   cfg->bc_stob_domain_cfg_create,
				   &dom->bd_stob_domain);
	/*
	 * Log is initalized here. m0_be_0type::b0_init() handler for log
	 * will not initialize log again in mkfs mode.
	 */
	if (rc == 0) {
		be_domain_mkfs_progress(dom, "initializing BE log");
		rc = be_domain_log_init(dom, &cfg->bc_log_cfg, true);
		if (rc != 0) {
			rc1 = m0_stob_domain_destroy(dom->bd_stob_domain);
			if (rc1 == 0) {
				M0_LOG(M0_DEBUG, "rc = %d", rc);
			} else {
				M0_LOG(M0_WARN, "rc = %d", rc);
			}
		}
	}
	if (rc == 0)
		be_domain_mkfs_progress(dom, "starting BE engine");
	return M0_RC(rc);
}

static void be_domain_stop_mkfs_pre(struct m0_be_domain *dom)
{
	be_domain_log_fini(dom);
	m0_stob_domain_fini(dom->bd_stob_domain);
}

static int be_domain_start_mkfs_post(struct m0_be_domain     *dom,
				     struct m0_be_domain_cfg *cfg)
{
	struct m0_be_seg *seg;
	int		  rc;
	unsigned	  i;

	/*
	 * seg0 can only be created after BE engine started because
	 * m0_be_allocator_create() and m0_be_seg_dict_create() need
	 * engine to process a transaction.
	 */
	be_domain_mkfs_progress(dom, "creating seg0 segment");
	rc = be_domain_seg_create(dom, NULL, m0_be_domain_seg0_get(dom),
				  &cfg->bc_seg0_cfg);
	if (rc == 0) {
		be_domain_mkfs_progress(dom, "creating seg0 allocator "
					"and dictionary");
		rc = be_domain_seg_structs_create(dom, NULL,
						  m0_be_domain_seg0_get(dom));
	}
	if (rc == 0) {
		be_domain_mkfs_progress(dom, "saving seg0 and log "
					"0type records in seg0");
		rc = be_domain_mkfs_seg0_log(dom, &cfg->bc_seg0_cfg,
					     &cfg->bc_log_cfg);
	}
	if (rc == 0) {
		be_domain_mkfs_progress(dom, "creating %u segments",
					dom->bd_cfg.bc_seg_nr);
		for (i = 0; i < dom->bd_cfg.bc_seg_nr; ++i) {
			be_domain_mkfs_progress(dom, "creating segment %u", i);
			/*
			 * Currently there is one transaction per segment
			 * created. It may be changed in the future.
			 */
			rc = m0_be_domain_seg_create(dom, NULL,
						     &dom->bd_cfg.bc_seg_cfg[i],
						     &seg);
			if (rc != 0)
				break;
		}
	}
	/*
	 * Segments and log are not destroyed in case of failure.
	 * It should help to determine cause of failure.
	 */
	M0_ASSERT(ergo(rc == 0, dom->bd_mkfs_stage == dom->bd_mkfs_stage_nr));
	return M0_RC(rc);
}

static int be_domain_start_stop(struct m0_be_domain	*dom,
				struct m0_be_domain_cfg *cfg)
{
	struct m0_be_engine *en = &dom->bd_engine;
	bool		     mkfs_mode;
	int		     rc;

	/* stop is always in the normal mode */
	mkfs_mode = cfg == NULL ? false : cfg->bc_mkfs_mode;
	if (cfg == NULL)
		goto stop;

	dom->bd_cfg = *cfg;
	M0_ASSERT(equi(cfg->bc_seg_cfg == NULL, cfg->bc_seg_nr == 0));

	if (mkfs_mode) {
		rc = be_domain_start_mkfs_pre(dom, cfg);
	} else {
		rc = be_domain_start_normal_pre(dom, cfg);
	}
	if (rc != 0)
		goto out;

	rc = m0_be_engine_init(en, &dom->bd_cfg.bc_engine);
	if (rc != 0)
		goto stop_pre;
	rc = m0_be_engine_start(en);
	if (rc != 0)
		goto engine_fini;

	if (mkfs_mode)
		rc = be_domain_start_mkfs_post(dom, cfg);
	if (rc != 0)
		goto engine_stop;

	if (rc == 0) {
		if (m0_get()->i_be_dom != NULL) {
			m0_get()->i_be_dom_save = m0_get()->i_be_dom;
			m0_get()->i_be_dom = NULL;
		} else if (m0_get()->i_be_dom_save == NULL) {
			   m0_get()->i_be_dom = dom;
		}
	}
	goto out;

stop:
	rc = 0;
	if (m0_get()->i_be_dom == dom || m0_get()->i_be_dom_save == dom) {
		m0_get()->i_be_dom = NULL;
		m0_get()->i_be_dom_save = NULL;
	}
engine_stop:
	m0_be_engine_stop(&dom->bd_engine);
engine_fini:
	m0_be_engine_fini(en);
stop_pre:
	if (mkfs_mode) {
		be_domain_stop_mkfs_pre(dom);
	} else {
		be_domain_stop_normal_pre(dom);
	}
out:
	return M0_RC(rc);
}

M0_INTERNAL int m0_be_domain_start(struct m0_be_domain	   *dom,
				   struct m0_be_domain_cfg *cfg)
{
	return M0_RC(be_domain_start_stop(dom, cfg));
}

M0_INTERNAL void m0_be_domain_fini(struct m0_be_domain *dom)
{
	struct m0_be_0type *zt;

	(void)be_domain_start_stop(dom, NULL);

	m0_be_0type_unregister(dom, &dom->bd_0type_seg);
	m0_be_0type_unregister(dom, &dom->bd_0type_log);

	m0_stob_domain_fini(dom->bd_stob_domain);

	m0_tl_teardown(zt, &dom->bd_0type_list, zt);

	m0_mutex_fini(&dom->bd_lock);
	zt_tlist_fini(&dom->bd_0type_list);
	seg_tlist_fini(&dom->bd_seg_list);
}

M0_INTERNAL struct m0_be_tx *m0_be_domain_tx_find(struct m0_be_domain *dom,
						  uint64_t id)
{
	return m0_be_engine__tx_find(m0_be_domain_engine(dom), id);
}

M0_INTERNAL struct m0_be_engine *m0_be_domain_engine(struct m0_be_domain *dom)
{
	return &dom->bd_engine;
}

M0_INTERNAL struct m0_be_seg *m0_be_domain_seg0_get(struct m0_be_domain *dom)
{
	return &dom->bd_seg0;
}

M0_INTERNAL struct m0_be_log *m0_be_domain_log(struct m0_be_domain *dom)
{
	return &m0_be_domain_engine(dom)->eng_log;
}

M0_INTERNAL struct m0_be_seg *m0_be_domain_seg(const struct m0_be_domain *dom,
					       const void		 *addr)
{
	return m0_be_seg_contains(&dom->bd_seg0, addr) ?
		(struct m0_be_seg *) &dom->bd_seg0 :
		m0_tl_find(seg, seg, &dom->bd_seg_list,
			   m0_be_seg_contains(seg, addr));
}

M0_INTERNAL struct m0_be_seg *
m0_be_domain_seg_by_id(const struct m0_be_domain *dom, uint64_t id)
{
	return dom->bd_seg0.bs_id == id ? (struct m0_be_seg *) &dom->bd_seg0 :
		m0_tl_find(seg, seg, &dom->bd_seg_list, seg->bs_id == id);
}

static void be_domain_seg_suffix_make(const struct m0_be_domain *dom,
				      uint64_t seg_id,
				      char *str,
				      size_t str_size)
{
	int nr = snprintf(str, str_size, "%"PRIu64, seg_id);

	M0_ASSERT(nr < str_size);
}

M0_INTERNAL void
m0_be_domain_seg_create_credit(struct m0_be_domain		*dom,
			       const struct m0_be_0type_seg_cfg *seg_cfg,
			       struct m0_be_tx_credit		*cred)
{
	struct m0_be_seg_hdr fake_seg_header;
	struct m0_be_seg     fake_seg = {
		.bs_addr = &fake_seg_header,
		.bs_size = 1,
	};
	char		     suffix[64];

	be_domain_seg_suffix_make(dom, seg_cfg->bsc_stob_key,
				  suffix, ARRAY_SIZE(suffix));
	/* See next comment. The same applies to m0_be_allocator */
	m0_be_allocator_credit(NULL, M0_BAO_CREATE, 0, 0, cred);
	/*
	 * m0_be_btree credit interface was created a long time ago.
	 * It needs initalized segment allocator (at least as a parameter)
	 * to calculate credit. In m0_be_seg_dict m0_be_btree doesn't need to
	 * be allocated, so credit can be calculated without initialized
	 * allocator. First parameter should be changed to someting after proper
	 * interfaces introduced.
	 */
	m0_be_seg_dict_create_credit(&fake_seg, cred);	/* XXX */
	m0_be_0type_add_credit(dom, &dom->bd_0type_seg, suffix,
			       &M0_BUF_INIT_PTR_CONST(seg_cfg), cred);
	m0_be_0type_del_credit(dom, &dom->bd_0type_seg, suffix, cred);
}

M0_INTERNAL void m0_be_domain_seg_destroy_credit(struct m0_be_domain	*dom,
						 struct m0_be_seg	*seg,
						 struct m0_be_tx_credit *cred)
{
	char suffix[64];

	be_domain_seg_suffix_make(dom, seg->bs_id, suffix, ARRAY_SIZE(suffix));
	m0_be_0type_del_credit(dom, &dom->bd_0type_seg, suffix, cred);
}

M0_INTERNAL int
m0_be_domain_seg_create(struct m0_be_domain		  *dom,
			struct m0_be_tx			  *tx,
			const struct m0_be_0type_seg_cfg  *seg_cfg,
			struct m0_be_seg		 **out)
{
	struct m0_be_tx_credit	cred = {};
	struct m0_be_tx		tx_ = {};
	struct m0_sm_group     *grp = m0_locality0_get()->lo_grp;
	struct m0_be_seg       *seg;
	struct m0_be_seg	seg1 = {};
	bool			use_local_tx = tx == NULL;
	bool			tx_is_open;
	char			suffix[64];
	int			rc;
	int			rc1;

	M0_PRE(ergo(tx != NULL, m0_be_tx__is_exclusive(tx)));
	M0_ASSERT_INFO(!seg_tlist_is_empty(&dom->bd_seg_list),
		       "seg0 should be added first");

	be_domain_seg_suffix_make(dom, seg_cfg->bsc_stob_key,
				  suffix, ARRAY_SIZE(suffix));

	if (use_local_tx) {
		tx = &tx_;
		m0_sm_group_lock(grp);
		m0_be_tx_init(tx, 0, dom, grp, NULL, NULL, NULL, NULL);
		m0_be_domain_seg_create_credit(dom, seg_cfg, &cred);
		m0_be_tx_prep(tx, &cred);
		rc = m0_be_tx_exclusive_open_sync(tx);
		tx_is_open = rc == 0;
	} else {
		rc = 0;
		tx_is_open = false;
	}
	rc = rc ?: be_domain_seg_create(dom, tx, &seg1, seg_cfg);
	if (rc == 0) {
		be_domain_seg_close(dom, &seg1, false);
		rc = m0_be_0type_add(&dom->bd_0type_seg, dom, tx, suffix,
				     &M0_BUF_INIT_PTR_CONST(seg_cfg));
		if (rc == 0) {
			seg = m0_be_domain_seg(dom, seg_cfg->bsc_addr);
			M0_ASSERT(seg != NULL);
			rc = be_domain_seg_structs_create(dom, tx, seg);
			if (rc != 0) {
				rc1 = m0_be_0type_del(&dom->bd_0type_seg, dom,
						      tx, suffix);
				M0_ASSERT_INFO(rc1 != 0, "rc1 = %d", rc1);
			}
		}
		if (rc != 0) {
			rc1 = be_domain_seg_destroy(dom, seg_cfg->bsc_stob_key);
			M0_LOG(M0_ERROR, "can't destroy segment "
			       "just after creation. "
			       "seg_cfg->bsc_stob_key = %"PRIu64", rc = %d",
			       seg_cfg->bsc_stob_key, rc1);
		}
	}
	if (use_local_tx) {
		if (tx_is_open)
			m0_be_tx_close_sync(tx);
		m0_be_tx_fini(tx);
		m0_sm_group_unlock(grp);
	}
	*out = rc != 0 ? NULL : m0_be_domain_seg(dom, seg_cfg->bsc_addr);
	return M0_RC(rc);
}

M0_INTERNAL int m0_be_domain_seg_destroy(struct m0_be_domain *dom,
					 struct m0_be_tx     *tx,
					 struct m0_be_seg    *seg)
{
	uint64_t seg_id = seg->bs_id;
	char	 suffix[64];
	int	 rc;

	M0_PRE(m0_be_tx__is_exclusive(tx));

	be_domain_seg_suffix_make(dom, seg->bs_id, suffix, ARRAY_SIZE(suffix));
	rc = m0_be_0type_del(&dom->bd_0type_seg, dom, tx, suffix);
	rc = rc ?: be_domain_seg_destroy(dom, seg_id);
	/*
	 * It may be a problem here if 0type record for the segment was removed
	 * but be_domain_seg_destroy() failed.
	 */
	return M0_RC(rc);
}

M0_INTERNAL bool m0_be_domain_is_locked(const struct m0_be_domain *dom)
{
	/* XXX: return m0_mutex_is_locked(&dom->bd_engine.eng_lock); */
	return true;
}

M0_INTERNAL void m0_be_domain__0type_register(struct m0_be_domain *dom,
					      struct m0_be_0type *type)
{
	be_domain_lock(dom);
	zt_tlink_init_at_tail(type, &dom->bd_0type_list);
	be_domain_unlock(dom);
}

M0_INTERNAL void m0_be_domain__0type_unregister(struct m0_be_domain *dom,
						struct m0_be_0type *type)
{
	be_domain_lock(dom);
	zt_tlink_del_fini(type);
	be_domain_unlock(dom);
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
