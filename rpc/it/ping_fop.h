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
 * Original author: Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 07/07/2011
 */

#pragma once

#ifndef __COLIBRI_PING_FOP_H__
#define __COLIBRI_PING_FOP_H__

#include "fop/fop.h"
#include "rpc/rpc_opcodes.h"

int c2_ping_fop_init(void);
void c2_ping_fop_fini(void);

/**
 * FOP definitions and corresponding fop type formats
 */
extern struct c2_fop_type_format c2_fop_ping_tfmt;
extern struct c2_fop_type_format c2_fop_ping_rep_tfmt;

extern struct c2_fop_type c2_fop_ping_fopt;
extern struct c2_fop_type c2_fop_ping_rep_fopt;

extern const struct c2_fop_type_ops c2_fop_ping_ops;
extern const struct c2_fop_type_ops c2_fop_ping_rep_ops;

extern const struct c2_rpc_item_type c2_rpc_item_type_ping;
extern const struct c2_rpc_item_type c2_rpc_item_type_ping_rep;

/* __COLIBRI_PING_FOP_H__ */
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

