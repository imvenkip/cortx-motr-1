/* -*- c -*- */
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 04-Mar-2012
 */

#include "conf/rpc.h"
#include "xcode/bufvec_xcode.h"  /* c2_xcode_fop_size_get */
#include "fop/fop_base.h"        /* c2_fop_type_ops */
#include "fop/fop_format.h"      /* C2_FOP_TYPE_DECLARE */
#include "rpc/rpc_opcodes.h"

static const struct c2_fop_type_ops fopt_ops = {
	.fto_size_get = c2_xcode_fop_size_get
};

C2_FOP_TYPE_DECLARE(c2_conf_fetch, "c2_conf_fetch", &fopt_ops,
		    C2_CONF_FETCH_OPCODE, C2_RPC_ITEM_TYPE_REQUEST);

C2_FOP_TYPE_DECLARE(c2_conf_fetch_resp, "c2_conf_fetch_resp", &fopt_ops,
		    C2_CONF_FETCH_RESP_OPCODE, C2_RPC_ITEM_TYPE_REPLY);
