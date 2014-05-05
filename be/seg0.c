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
#include "be/op.h"
#include "be/alloc.h"

#include "lib/mutex.h"
#include "lib/buf.h"


static bool be_0type_invariant(const struct m0_be_0type *zt)
{
	return zt->b0_name != NULL &&
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
	snprintf(keyname, keyname_len, "%s%s", zt->b0_name, suffix);
}

void m0_be_0type_register(struct m0_be_domain *dom, struct m0_be_0type *zt)
{
	M0_PRE(be_0type_invariant(zt));

	m0_be_domain__0type_register(dom, zt);
}

int m0_be_0type_add(struct m0_be_0type *zt, struct m0_be_domain *dom,
		    struct m0_be_tx *tx, const char *suffix,
		    const struct m0_buf *data)
{
	struct m0_be_seg *seg;
	struct m0_buf    *opt;
	char              keyname[256] = {};
	int rc;

	M0_PRE(dom_is_locked(dom));
	M0_PRE(be_0type_invariant(zt));
	M0_PRE(m0_be_tx__is_exclusive(tx));

	seg = seg0_get(dom);
	keyname_format(zt, suffix, keyname, ARRAY_SIZE(keyname));

	M0_BE_ALLOC_PTR_SYNC(opt, seg, tx);
	M0_BE_OP_SYNC(op, m0_be_alloc(m0_be_seg_allocator(seg), tx, &op,
				      &opt->b_addr, data->b_nob));
	opt->b_nob = data->b_nob;
	memcpy(opt->b_addr, data->b_addr, data->b_nob);
	rc = m0_be_seg_dict_insert(seg, tx, keyname, (void*)data);
	if (rc != 0)
		return rc;

	return zt->b0_init(dom, suffix, data);
}

int m0_be_0type_del(struct m0_be_0type *zt, struct m0_be_domain *dom,
		    struct m0_be_tx *tx, const char *suffix,
		    const struct m0_buf *data)
{
	struct m0_be_seg *seg;
	struct m0_buf    *opt;
	char              keyname[256] = {};
	int               rc;

	M0_PRE(dom_is_locked(dom));
	M0_PRE(be_0type_invariant(zt));
	M0_PRE(m0_be_tx__is_exclusive(tx));

	seg = seg0_get(dom);
	keyname_format(zt, suffix, keyname, ARRAY_SIZE(keyname));

	rc = m0_be_seg_dict_lookup(seg, keyname, (void**)&opt);
	if (rc != 0)
		return rc; /* keyname is not found -- nothing to delete */

	zt->b0_fini(dom, suffix, data);
	M0_BE_FREE_PTR_SYNC(opt->b_addr, seg, tx);
	M0_BE_FREE_PTR_SYNC(opt, seg, tx);
	return m0_be_seg_dict_delete(seg, tx, keyname);
}

void m0_be_0type_add_credit(const struct m0_be_domain *dom,
			    const struct m0_be_0type  *zt,
			    const char		      *suffix,
			    const struct m0_buf       *data,
			    struct m0_be_tx_credit    *credit)
{
	struct m0_be_seg *seg = m0_be_domain_seg0_get(dom);
	char keyname[256] = {};

	keyname_format(zt, suffix, keyname, ARRAY_SIZE(keyname));
	m0_be_seg_dict_insert_credit(seg, keyname, credit);
	M0_BE_ALLOC_CREDIT_PTR(data, seg, credit);
	m0_be_allocator_credit(m0_be_seg_allocator(seg), M0_BAO_ALLOC,
			       data->b_nob, 0, credit);
}

void m0_be_0type_del_credit(const struct m0_be_domain *dom,
			    const struct m0_be_0type  *zt,
			    const char		      *suffix,
			    const struct m0_buf       *data,
			    struct m0_be_tx_credit    *credit)
{
	struct m0_be_seg *seg = m0_be_domain_seg0_get(dom);
	char keyname[256] = {};

	keyname_format(zt, suffix, keyname, ARRAY_SIZE(keyname));
	m0_be_seg_dict_delete_credit(seg, keyname, credit);
	M0_BE_FREE_CREDIT_PTR(data, seg, credit);
	m0_be_allocator_credit(m0_be_seg_allocator(seg), M0_BAO_FREE,
			       data->b_nob, 0, credit);
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
