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

#ifndef __COLIBRI_RPC_HELPERS_H__
#define __COLIBRI_RPC_HELPERS_H__

#include "lib/vec.h"
#include "rpc/item.h"

enum c2_bufvec_what {
	C2_BUFVEC_ENCODE = 0,
	C2_BUFVEC_DECODE = 1,
};

/**
    Encodes the rpc item header into a bufvec
    @param ioh RPC item header to be encoded
    @param cur Current bufvec cursor position
    @retval 0 (success)
    @retval -errno  (failure)
*/
int c2_rpc_item_header_encode(struct c2_rpc_item_onwire_header *ioh,
			      struct c2_bufvec_cursor          *cur);

/**
    Decodes the rpc item header into a bufvec
    @param ioh RPC item header to be decoded
    @param cur Current bufvec cursor position
    @retval 0 (success)
    @retval -errno  (failure)
*/
int c2_rpc_item_header_decode(struct c2_rpc_item_onwire_header *ioh,
			      struct c2_bufvec_cursor          *cur);

int c2_rpc_item_slot_ref_encdec(struct c2_bufvec_cursor *cur,
				struct c2_rpc_slot_ref  *slot_ref,
				enum c2_bufvec_what      what);

#endif /* __COLIBRI_RPC_HELPERS_H__ */
