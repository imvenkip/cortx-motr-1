/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <max.medved@seagate.com>
 * Original creation date: 11-Mar-2015
 */

#pragma once

#ifndef __MERO_STOB_FID_CONVERT_H__
#define __MERO_STOB_FID_CONVERT_H__

/**
 * @defgroup fidconvert
 *
 *                       8 bits                     120 bits
 *                 +-----------------+---------------------------------------+
 *     AD stob     | AD stob domain  |                                       |
 *   domain fid    |    type id      |                                       |
 *                 +-----------------+---------------------------------------+
 *                                                      ||
 *                                                      \/
 *  AD stob domain +-----------------+---------------------------------------+
 *  backing store  |   linux stob    |                                       |
 *    stob fid     |     type id     |                                       |
 *                 +-----------------+---------------------------------------+
 * @{
 */

struct m0_fid;

M0_INTERNAL void
m0_fid_convert_bstore2adstob(const struct m0_fid *bstore_fid,
			     struct m0_fid       *stob_domain_fid);
M0_INTERNAL void
m0_fid_convert_adstob2bstore(const struct m0_fid *stob_domain_fid,
			     struct m0_fid       *bstore_fid);

M0_INTERNAL bool m0_fid_validate_bstore(const struct m0_fid *stob_fid);


/** @} end of fidconvert group */
#endif /* __MERO_STOB_FID_CONVERT_H__ */

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
