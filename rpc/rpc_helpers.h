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
 * Original author: Manish Honap <manish_honap@xyratex.com>
 * Original creation date: 09/20/2012
 */

#pragma once

#ifndef __MERO_RPC_HELPERS_H__
#define __MERO_RPC_HELPERS_H__

#include "lib/vec.h"  /* m0_bufvec_what */

struct m0_rpc_slot_ref;

/**
 * @addtogroup rpc
 * @{
 */

/**
 * Encodes or decodes onwire parts of m0_rpc_slot_refs.
 *
 * For every x in `slot_refs' array, encodes or decodes, depending on
 * `what' argument, x->sr_ow.
 */
M0_INTERNAL int m0_rpc_slot_refs_encdec(struct m0_bufvec_cursor *cur,
					struct m0_rpc_slot_ref *slot_refs,
					int nr_slot_refs,
					enum m0_bufvec_what what);

/** @} */

#endif /* __MERO_RPC_HELPERS_H__ */
