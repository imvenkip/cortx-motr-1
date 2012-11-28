/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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

#pragma once

#ifndef __MERO_FID_FID_H__
#define __MERO_FID_FID_H__

/**
   @defgroup fid File identifier

   @{
 */

/* import */
#include "lib/types.h"
#include "lib/cdefs.h"
#include "xcode/xcode_attr.h"

/* @todo: add xcode */
struct m0_fid {
        uint64_t f_container;
        uint64_t f_key;
}  M0_XCA_RECORD;

M0_INTERNAL bool m0_fid_is_set(const struct m0_fid *fid);
M0_INTERNAL bool m0_fid_is_valid(const struct m0_fid *fid);
M0_INTERNAL bool m0_fid_eq(const struct m0_fid *fid0,
			   const struct m0_fid *fid1);
M0_INTERNAL int m0_fid_cmp(const struct m0_fid *fid0,
			   const struct m0_fid *fid1);
M0_INTERNAL void m0_fid_set(struct m0_fid *fid,
                            uint64_t container,
			    uint64_t key);

M0_INTERNAL int m0_fid_init(void);
M0_INTERNAL void m0_fid_fini(void);

/** @} end of fid group */

/* __MERO_FID_FID_H__ */
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
