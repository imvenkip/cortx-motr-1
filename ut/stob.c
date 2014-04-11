/* -*- C -*- */
/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 20-Mar-2014
 */

#include "ut/stob.h"

#include "lib/misc.h"		/* ARRAY_SIZE */
#include "lib/mutex.h"		/* m0_mutex */
#include "lib/memory.h"		/* M0_ALLOC_PTR */
#include "lib/errno.h"		/* ENOMEM */

#include "module/instance.h"	/* m0_get */
#include "be/tx_credit.h"	/* m0_be_tx_credit */
#include "be/ut/helper.h"	/* m0_be_ut_backend_sm_group_lookup */
#include "dtm/dtm.h"		/* m0_dtx */
#include "db/db.h"		/* m0_dbenv */

#include "stob/domain.h"	/* m0_stob_domain */
#include "stob/stob.h"		/* m0_stob_find */

/**
 * @addtogroup utstob
 *
 * @{
 */

enum {
	UT_STOB_KEY_BEGIN = 42,
};

struct ut_stob_module {
	struct m0_mutex	       usm_lock;
	uint64_t	       usm_stob_key;
	struct m0_stob_domain *usm_dom_linux;
};

static int level_ut_stob_enter(struct m0_module *module);
static void level_ut_stob_leave(struct m0_module *module);

struct m0_modlev m0_levels_ut_stob[] = {
	[M0_LEVEL_UT_STOB] = {
		.ml_name = "stob UT helper is initialised",
		.ml_enter = level_ut_stob_enter,
		.ml_leave = level_ut_stob_leave,
	},
};
const unsigned m0_levels_ut_stob_nr = ARRAY_SIZE(m0_levels_ut_stob);

/* linux stob domain configuratin */
static const char     *ut_stob_domain_location	 = "linuxstob:./ut_stob";
static const uint64_t  ut_stob_domain_key	 = 0x42;
static const char     *ut_stob_domain_init_cfg	 = "";
static const char     *ut_stob_domain_create_cfg = "";

static int level_ut_stob_enter(struct m0_module *module)
{
	return m0_ut_stob_init();
}

static void level_ut_stob_leave(struct m0_module *module)
{
	m0_ut_stob_fini();
}

static struct ut_stob_module *ut_stob_module_get(void)
{
	return m0_get()->i_ut_stob_module.usm_private;
}

M0_INTERNAL int m0_ut_stob_init(void)
{
	struct ut_stob_module *usm;
	int		       rc;

	M0_ALLOC_PTR(usm);
	rc = usm == NULL ? -ENOMEM : 0;
	if (usm != NULL) {
		rc = m0_stob_domain_create_or_init(ut_stob_domain_location,
						   ut_stob_domain_init_cfg,
						   ut_stob_domain_key,
						   ut_stob_domain_create_cfg,
						   &usm->usm_dom_linux);
		if (rc != 0)
			m0_free(usm);
	}
	if (rc == 0) {
		m0_mutex_init(&usm->usm_lock);
		usm->usm_stob_key = UT_STOB_KEY_BEGIN;
		m0_get()->i_ut_stob_module.usm_private = usm;
	}
	return rc;
}

M0_INTERNAL void m0_ut_stob_fini(void)
{
	struct ut_stob_module *usm = ut_stob_module_get();

	if (usm != NULL) {
		m0_stob_domain_fini(usm->usm_dom_linux);
		m0_mutex_fini(&usm->usm_lock);
		m0_free(usm);
	}
}

M0_INTERNAL struct m0_stob *m0_ut_stob_linux_get(void)
{
	struct ut_stob_module *usm = ut_stob_module_get();
	uint64_t stob_key;

	m0_mutex_lock(&usm->usm_lock);
	stob_key = usm->usm_stob_key++;
	m0_mutex_unlock(&usm->usm_lock);
	return m0_ut_stob_linux_get_by_key(stob_key);
}

M0_INTERNAL struct m0_stob *m0_ut_stob_linux_get_by_key(uint64_t stob_key)
{
	struct ut_stob_module *usm = ut_stob_module_get();
	struct m0_stob	      *stob;
	int		       rc;

	m0_mutex_lock(&usm->usm_lock);

	rc = m0_stob_find_by_key(usm->usm_dom_linux, stob_key, &stob);
	M0_ASSERT(rc == 0);
	rc = m0_stob_state_get(stob) == CSS_UNKNOWN ? m0_stob_locate(stob) : 0;
	M0_ASSERT_INFO(rc == 0, "rc = %d", rc);
	rc = m0_stob_state_get(stob) == CSS_NOENT ?
	     m0_stob_create(stob, NULL, NULL) : 0;
	M0_ASSERT_INFO(rc == 0, "rc = %d", rc);
	M0_ASSERT(m0_stob_state_get(stob) == CSS_EXISTS);

	m0_mutex_unlock(&usm->usm_lock);
	return stob;
}

M0_INTERNAL void m0_ut_stob_put(struct m0_stob *stob, bool destroy)
{
	struct ut_stob_module *usm = ut_stob_module_get();

	m0_mutex_lock(&usm->usm_lock);
	m0_ut_stob_close(stob, destroy);
	m0_mutex_unlock(&usm->usm_lock);
}

static bool ut_stob_use_dtx(struct m0_stob_domain *dom)
{
	uint8_t type_id = m0_stob_domain__type_id(m0_stob_domain_id_get(dom));

	return !M0_IN(type_id, (m0_stob_type_id_by_name("nullstob"),
				m0_stob_type_id_by_name("linuxstob")));
}

M0_INTERNAL int m0_ut_stob_create(struct m0_stob *stob, const char *str_cfg)
{
	struct m0_be_tx_credit  cred = {};
	struct m0_stob_domain  *dom = m0_stob_dom_get(stob);
	struct m0_dtx	       *dtx = NULL;
	bool			use_dtx;
	int			rc;

	use_dtx = ut_stob_use_dtx(dom);
	if (use_dtx) {
		m0_stob_create_credit(dom, &cred);
		dtx = m0_ut_dtx_open(&cred);
	}
	rc = m0_stob_create(stob, dtx, str_cfg);
	if (use_dtx)
		m0_ut_dtx_close(dtx);
	return rc;
}

M0_INTERNAL int m0_ut_stob_destroy(struct m0_stob *stob)
{
	struct m0_be_tx_credit  cred = {};
	struct m0_dtx	       *dtx = NULL;
	bool			use_dtx;
	int			rc;

	use_dtx = ut_stob_use_dtx(m0_stob_dom_get(stob));
	if (use_dtx) {
		m0_stob_destroy_credit(stob, &cred);
		dtx = m0_ut_dtx_open(&cred);
	}
	rc = m0_stob_destroy(stob, dtx);
	if (use_dtx)
		m0_ut_dtx_close(dtx);
	return rc;
}

M0_INTERNAL struct m0_stob *m0_ut_stob_open(struct m0_stob_domain *dom,
					    uint64_t stob_key,
					    const char *str_cfg)
{
	struct m0_stob *stob;
	int		rc;

	rc = m0_stob_find_by_key(dom, stob_key, &stob);
	M0_ASSERT(rc == 0);
	rc = m0_stob_state_get(stob) == CSS_UNKNOWN ? m0_stob_locate(stob) : 0;
	M0_ASSERT_INFO(rc == 0, "rc = %d", rc);
	if (m0_stob_state_get(stob) == CSS_NOENT)
		m0_ut_stob_create(stob, str_cfg);
	return stob;
}

M0_INTERNAL void m0_ut_stob_close(struct m0_stob *stob, bool destroy)
{
	if (destroy) {
		m0_ut_stob_destroy(stob);
	} else {
		m0_stob_put(stob);
	}
}

M0_INTERNAL int m0_ut_stob_create_by_fid(struct m0_fid *stob_fid,
					 const char *str_cfg)
{
	struct m0_stob *stob;
	int		rc;

	rc = m0_stob_find(stob_fid, &stob);
	if (rc == 0) {
		rc = m0_ut_stob_create(stob, str_cfg);
		m0_stob_put(stob);
	}
	return rc;
}

M0_INTERNAL int m0_ut_stob_destroy_by_fid(struct m0_fid *stob_fid)
{
	struct m0_stob *stob;
	int		rc;

	rc = m0_stob_find(stob_fid, &stob);
	if (rc == 0) {
		rc = m0_ut_stob_destroy(stob);
		if (rc != 0)
			m0_stob_put(stob);
	}
	return rc;
}

M0_INTERNAL struct m0_dtx *m0_ut_dtx_open(struct m0_be_tx_credit *cred)
{
#ifndef __KERNEL__
	struct m0_sm_group *grp;
	struct m0_dtx	   *dtx = NULL;
	int		    rc;

	if (m0_get()->i_be_ut_backend != NULL) {
		M0_ALLOC_PTR(dtx);
		M0_ASSERT(dtx != NULL);
		grp = m0_be_ut_backend_sm_group_lookup(
					m0_get()->i_be_ut_backend);
		m0_dtx_init(dtx, m0_get()->i_be_dom, grp);
		m0_dtx_prep(dtx, cred);
		rc = m0_dtx_open_sync(dtx);
		M0_ASSERT_INFO(rc == 0, "rc = %d", rc);
	}
	return dtx;
#else
	return NULL;
#endif
}

M0_INTERNAL void m0_ut_dtx_close(struct m0_dtx *dtx)
{
	int rc;

	if (dtx != NULL) {
		rc = m0_dtx_done_sync(dtx);
		M0_ASSERT_INFO(rc == 0, "rc = %d", rc);
		m0_dtx_fini(dtx);
		m0_free(dtx);
	}
}

/** @} end of utstob group */

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
