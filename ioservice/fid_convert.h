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

#ifndef __MERO_IOSERVICE_FID_CONVERT_H__
#define __MERO_IOSERVICE_FID_CONVERT_H__

#include "lib/types.h"          /* uint32_t */

/**
 * @defgroup fidconvert
 *
 *                     8 bits           24 bits             96 bits
 *                 +-----------------+-----------+---------------------------+
 *    GOB fid      |   GOB type id   |  zeroed   |                           |
 *                 +-----------------+-----------+---------------------------+
 *                                                            ||
 *                                                            \/
 *                 +-----------------+-----------+---------------------------+
 *    COB fid      |   COB type id   | device id |                           |
 *                 +-----------------+-----------+---------------------------+
 *                                        ||                  ||
 *                                        \/                  \/
 *    AD stob      +-----------------+-----------+---------------------------+
 *      fid        | AD stob type id | device id |                           |
 *                 +-----------------+-----------+---------------------------+
 *
 *                       8 bits                96 bits             24 bits
 *                 +-----------------+---------------------------+-----------+
 *     AD stob     | AD stob domain  |          zeroed           | device id |
 *   domain fid    |    type id      |                           |           |
 *                 +-----------------+---------------------------------------+
 * @{
 */

struct m0_fid;
struct m0_stob_id;

M0_INTERNAL void m0_fid_convert_gob2cob(const struct m0_fid *gob_fid,
					struct m0_fid       *cob_fid,
					uint32_t             device_id);
M0_INTERNAL void m0_fid_convert_cob2gob(const struct m0_fid *cob_fid,
					struct m0_fid       *gob_fid);

M0_INTERNAL void m0_fid_convert_cob2adstob(const struct m0_fid *cob_fid,
					   struct m0_stob_id   *stob_id);
M0_INTERNAL void m0_fid_convert_adstob2cob(const struct m0_stob_id *stob_id,
					   struct m0_fid           *cob_fid);

M0_INTERNAL bool m0_fid_validate_gob(const struct m0_fid *gob_fid);
M0_INTERNAL bool m0_fid_validate_cob(const struct m0_fid *cob_fid);
M0_INTERNAL bool m0_fid_validate_adstob(const struct m0_stob_id *stob_id);

/** @} end of fidconvert group */
#endif /* __MERO_IOSERVICE_FID_CONVERT_H__ */

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
