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
 * Original author: Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>
 * Original creation date: 11-Feb-2014
 */

#include <stdio.h>
#include "be/domain.h"
#include "be/seg0.h"
#include "be/seg.h"

#include "lib/mutex.h"
#include "lib/buf.h"


static bool be_0type_invariant(const struct m0_be_0type *zt, bool registered)
{
	return ergo(registered, zt_tlink_is_in(zt)) &&
		zt->b0_name != NULL &&
		zt->b0_init != NULL &&
		zt->b0_fini != NULL;
}

static bool dom_is_locked(const struct m0_be_domain *dom)
{
	return m0_be_domain_is_locked(dom);
}

static struct m0_be_seg *seg0_get(const struct m0_be_domain *dom)
{
	return m0_be_domain_seg0_get(dom);
}

static void keyname_format(const struct m0_be_0type *zt, const char *suffix,
			   char *keyname, size_t keyname_len)
{
	snprintf(keyname, keyname_len, "%s%s%s", "M0_BE:", zt->b0_name, suffix);
}

void m0_be_0type_register(struct m0_be_domain *dom, struct m0_be_0type *zt)
{
	M0_PRE(be_0type_invariant(zt, false));
	M0_PRE(dom_is_locked(dom));

	zt_tlink_init_at_tail(zt, &dom->bd_0type_list);
}

int m0_be_0type_add(struct m0_be_0type *zt, const struct m0_be_domain *dom,
		    struct m0_be_tx *tx, const char *suffix,
		    const struct m0_buf *data)
{
	struct m0_be_seg *seg;
	char              keyname[1024] = {};

	M0_PRE(dom_is_locked(dom));
	M0_PRE(be_0type_invariant(zt, true));

	seg = seg0_get(dom);
	keyname_format(zt, suffix, keyname, ARRAY_SIZE(keyname));
#if 0
	return m0_be_seg_dict_insert(seg, tx, keyname, data->b_addr);
#else
	M0_IMPOSSIBLE("Implement BE engine start/stop before uncommenting!");
	return -1;
#endif
}

int m0_be_0type_del(struct m0_be_0type *zt, const struct m0_be_domain *dom,
		    struct m0_be_tx *tx, const char *suffix,
		    const struct m0_buf *data)
{
	struct m0_be_seg *seg;
	char              keyname[1024] = {};

	M0_PRE(dom_is_locked(dom));
	M0_PRE(be_0type_invariant(zt, true));

	seg = seg0_get(dom);
	keyname_format(zt, suffix, keyname, ARRAY_SIZE(keyname));
#if 0
	return m0_be_seg_dict_delete(seg, tx, keyname);
#else
	M0_IMPOSSIBLE("Implement BE engine start/stop before uncommenting!");
	return -1;
#endif

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
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
