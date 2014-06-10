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
 * Original author: Dmytro Podgornyi <dmytro_podgornyi@xyratex.com>
 * Original creation date: 17-Mar-2014
 */

//#include <stdio.h>		/* snprintf */

#include "be/ut/helper.h"
#include "lib/errno.h"
#include "lib/memory.h"		/* m0_free */
#include "ut/ut.h"		/* M0_UT_ASSERT */
#include "ut/stob.h"		/* m0_ut_stob_linux_get */

#include "stob/ad.h"		/* m0_stob_ad_cfg_make */
#include "stob/domain.h"
#include "stob/stob.h"
#include "stob/stob_internal.h"

static void stob_ut_stob_domain(const char *location, const char *cfg)
{
	struct m0_stob_domain *dom;
	uint64_t               dom_key = 0xba5ec0de;
	uint64_t               dom_id;
	int		       rc;

	rc = m0_stob_domain_init(location, NULL, &dom);
	M0_UT_ASSERT(rc == -ENOENT);
	rc = m0_stob_domain_destroy_location(location);
	M0_UT_ASSERT(rc == -ENOENT);
	rc = m0_stob_domain_create(location, NULL, dom_key, cfg, &dom);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(dom != NULL);
	m0_stob_domain_fini(dom);

	rc = m0_stob_domain_create(location, NULL, dom_key, cfg, &dom);
	M0_UT_ASSERT(rc == -EEXIST);

	rc = m0_stob_domain_init(location, NULL, &dom);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(dom != NULL);
	M0_UT_ASSERT(m0_stob_domain__dom_key(m0_stob_domain_id_get(dom)) ==
		     dom_key);

	/* Find existent domain */
	dom_id = m0_stob_domain_id_get(dom);
	M0_UT_ASSERT(m0_stob_domain_find(dom_id) == dom);

	/* Find non-existent domain */
	dom_id ^= (1ULL << 56) - 1;
	M0_UT_ASSERT(m0_stob_domain_find(dom_id) == NULL);

	rc = m0_stob_domain_destroy(dom);
	M0_UT_ASSERT(rc == 0);

	rc = m0_stob_domain_create(location, NULL, dom_key, cfg, &dom);
	M0_UT_ASSERT(rc == 0);
	m0_stob_domain_fini(dom);
	rc = m0_stob_domain_destroy_location(location);
	M0_UT_ASSERT(rc == 0);
}

void m0_stob_ut_stob_domain_null(void)
{
	stob_ut_stob_domain("nullstob:./__s", NULL);
}

#ifndef __KERNEL__
void m0_stob_ut_stob_domain_linux(void)
{
	stob_ut_stob_domain("linuxstob:./__s", NULL);
}

extern void m0_stob_ut_ad_init(struct m0_be_ut_backend *ut_be,
			       struct m0_be_ut_seg     *ut_seg);
extern void m0_stob_ut_ad_fini(struct m0_be_ut_backend *ut_be,
			       struct m0_be_ut_seg     *ut_seg);

void m0_stob_ut_stob_domain_ad(void)
{
	struct m0_be_ut_backend  ut_be;
	struct m0_be_ut_seg      ut_seg;
	struct m0_stob          *stob;
	char                    *cfg;

	m0_stob_ut_ad_init(&ut_be, &ut_seg);
	stob = m0_ut_stob_linux_get();
	M0_UT_ASSERT(stob != NULL);
	m0_stob_ad_cfg_make(&cfg, ut_seg.bus_seg, m0_stob_fid_get(stob));
	M0_UT_ASSERT(cfg != NULL);

	stob_ut_stob_domain("adstob:some_suffix", cfg);

	m0_free(cfg);
	m0_ut_stob_put(stob, true);
	m0_stob_ut_ad_fini(&ut_be, &ut_seg);
}
#endif

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
