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
 * Original author: Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 08/01/2013
 */

#pragma once

#ifndef __MERO_SNS_SPARE_MAP_H__
#define __MERO_SNS_SPARE_MAP_H__

#include "layout/pdclust.h"
#include "fid/fid.h"
#include "pool/pool.h"

/**
 * Map the {failed device, spare slot} pair of a specified device.
 * @param pm pool machine.
 * @param fid Global file id.
 * @param pl pdclust layout instance.
 * @param group_number Parity group number for a given file.
 * @param unit_number Unit number in the parity group.
 * @param spare_slot_out the output spair slot.
 */
M0_INTERNAL int m0_sns_repair_spare_map(struct m0_poolmach *pm,
					const struct m0_fid *fid,
					struct m0_pdclust_layout *pl,
					uint64_t group_number,
					uint64_t unit_number,
					uint32_t *spare_slot_out);

#endif /* __MERO_SNS_SPARE_MAP_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
