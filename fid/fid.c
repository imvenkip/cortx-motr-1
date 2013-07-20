/* -*- C -*- */
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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 09/09/2010
 */

#ifdef __KERNEL__
#include <linux/string.h>  /* memcmp */
#else
#include <string.h>
#endif

#include "lib/cdefs.h"         /* M0_EXPORTED */
#include "lib/assert.h"        /* M0_PRE() */
#include "fid/fid_xc.h"
#include "fid/fid.h"

/**
   @addtogroup fid

   @{
 */

M0_INTERNAL bool m0_fid_is_valid(const struct m0_fid *fid)
{
	return true; /* XXX TODO */
}
M0_EXPORTED(m0_fid_is_valid);

M0_INTERNAL bool m0_fid_is_set(const struct m0_fid *fid)
{
	static const struct m0_fid zero = {
		.f_container = 0,
		.f_key = 0
	};
	return !m0_fid_eq(fid, &zero);
}
M0_EXPORTED(m0_fid_is_set);

M0_INTERNAL void m0_fid_set(struct m0_fid *fid, uint64_t container,
			    uint64_t key)
{
	M0_PRE(fid != NULL);

	fid->f_container = container;
	fid->f_key = key;
}
M0_EXPORTED(m0_fid_set);

M0_INTERNAL bool m0_fid_eq(const struct m0_fid *fid0, const struct m0_fid *fid1)
{
	return memcmp(fid0, fid1, sizeof *fid0) == 0;
}
M0_EXPORTED(m0_fid_eq);

M0_INTERNAL int m0_fid_cmp(const struct m0_fid *fid0, const struct m0_fid *fid1)
{
	const struct m0_uint128 u0 = {
		.u_hi = fid0->f_container,
		.u_lo = fid0->f_key
	};

	const struct m0_uint128 u1 = {
		.u_hi = fid1->f_container,
		.u_lo = fid1->f_key
	};

	return m0_uint128_cmp(&u0, &u1);
}
M0_EXPORTED(m0_fid_cmp);

M0_INTERNAL int m0_fid_init(void)
{
	m0_xc_fid_init();
	return 0;
}
M0_EXPORTED(m0_fid_init);

M0_INTERNAL void m0_fid_fini(void)
{
	m0_xc_fid_fini();
}
M0_EXPORTED(m0_fid_fini);

/** @} end of fid group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
