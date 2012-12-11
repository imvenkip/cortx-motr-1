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
 * Original author: Huang Hua <Hua_Huang@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 09/27/2011
 */

#include "lib/arith.h"
#include "lib/misc.h"
#include "cfg/cfg.h"

/**
 * @addtogroup conf_schema
 * @{
 */

/* DB Table ops */
static int dev_key_cmp(struct m0_table *table, const void *key0,
			const void *key1)
{
	const struct m0_cfg_storage_device__key *dev_key0 = key0;
	const struct m0_cfg_storage_device__key *dev_key1 = key1;

	return memcmp(dev_key0, dev_key1, sizeof *dev_key0);
}

/* Table ops for disk table */
const struct m0_table_ops m0_cfg_storage_device_table_ops = {
        .to = {
                [TO_KEY] = {
			.max_size = sizeof(struct m0_cfg_storage_device__key)
		},
                [TO_REC] = {
			.max_size = sizeof(struct m0_cfg_storage_device__val)
		}
        },
        .key_cmp = dev_key_cmp
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
