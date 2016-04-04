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
 * Original author: Nikita Danilov <nikita.danilov@seagate.com>
 * Original creation date: 16-Apr-2015
 */

#pragma once

#ifndef __MERO_RPC_ADDB2_H__
#define __MERO_RPC_ADDB2_H__

/**
 * @defgroup rpc
 *
 * @{
 */

#include "addb2/identifier.h"

enum {
	M0_AVI_RPC_LOCK = M0_AVI_RPC_RANGE_START + 1,
	M0_AVI_RPC_REPLIED,
	M0_AVI_RPC_OUT_PHASE,
	M0_AVI_RPC_IN_PHASE,
	M0_AVI_RPC_MACH_LOCK_WAIT,
	M0_AVI_RPC_MACH_LOCK_HOLD
};

/** @} end of rpc group */
#endif /* __MERO_RPC_ADDB2_H__ */

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
