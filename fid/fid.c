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

#include "lib/cdefs.h"         /* C2_EXPORTED */
#include "fid/fid.h"
#include "fop/fop.h"
#include "fop/fop_format_def.h"

#ifndef __KERNEL__
#include "fid/fid_u.h"
#else
#include "fid/fid_k.h"
#endif

#include "fid/fid.ff"

/**
   @addtogroup fid

   @{
 */

bool c2_fid_is_valid(const struct c2_fid *fid)
{
	return true;
}

bool c2_fid_is_set(const struct c2_fid *fid)
{
	static const struct c2_fid zero = {
	        .f_container = 0,
	        .f_key = 0
	};
	return !c2_fid_eq(fid, &zero);
}

bool c2_fid_eq(const struct c2_fid *fid0, const struct c2_fid *fid1)
{
	return memcmp(fid0, fid1, sizeof *fid0) == 0;
}

int c2_fid_cmp(const struct c2_fid *fid0, const struct c2_fid *fid1)
{
        const struct c2_uint128 u0 = {
                .u_hi = fid0->f_container,
                .u_lo = fid0->f_key
        };

        const struct c2_uint128 u1 = {
                .u_hi = fid1->f_container,
                .u_lo = fid1->f_key
        };

        return c2_uint128_cmp(&u0, &u1);
}

static struct c2_fop_type_format *c2_fid_fmts[] = {
        &c2_fop_fid_tfmt
};

void c2_fid_unregister(void)
{
        c2_fop_type_format_fini_nr(c2_fid_fmts, ARRAY_SIZE(c2_fid_fmts));
}

int c2_fid_register(void)
{
	int rc;

	rc = c2_fop_type_format_parse_nr(c2_fid_fmts, ARRAY_SIZE(c2_fid_fmts));
	return rc;
}

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
