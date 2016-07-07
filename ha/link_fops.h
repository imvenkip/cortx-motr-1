/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 18-Apr-2016
 */

#pragma once

#ifndef __MERO_HA_LINK_FOPS_H__
#define __MERO_HA_LINK_FOPS_H__

#include "lib/types.h"          /* m0_uint128 */
#include "xcode/xcode_attr.h"   /* M0_XCA_RECORD */
#include "fop/fop.h"            /* m0_fop_type */
#include "ha/msg.h"             /* m0_ha_msg */

#include "lib/types_xc.h"       /* m0_uint128_xc */
#include "ha/msg_xc.h"          /* m0_ha_msg_xc */

/**
 * @defgroup ha
 *
 * @{
 */

struct m0_ha_link_msg_fop {
	struct m0_ha_msg lmf_msg;
} M0_XCA_RECORD;

struct m0_ha_link_msg_rep_fop {
	int lmr_rc;
} M0_XCA_RECORD;

struct m0_ha_link_params {
	struct m0_uint128 hlp_id_local;
	struct m0_uint128 hlp_id_remote;
	/* bool flag */
	uint64_t          hlp_tag_even;
} M0_XCA_RECORD;

extern struct m0_fop_type m0_ha_link_msg_fopt;
extern struct m0_fop_type m0_ha_link_msg_rep_fopt;

M0_INTERNAL int  m0_ha_link_fops_init(void);
M0_INTERNAL void m0_ha_link_fops_fini(void);

/** @} end of ha group */
#endif /* __MERO_HA_LINK_FOPS_H__ */

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
