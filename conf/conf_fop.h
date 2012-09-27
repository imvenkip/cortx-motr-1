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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 05/05/2012
 */
#pragma once
#ifndef __COLIBRI_CONF_FOP_H__
#define __COLIBRI_CONF_FOP_H__

#include "fop/fop.h"
#include "rpc/rpc_opcodes.h"

/**
 * @defgroup confd_fop_dfspec Configuration service FOP definitions
 *
 * @{
 */

int c2_conf_fops_init(void);
void c2_conf_fops_fini(void);

/*
 * Confd fetch reply and request FOP definitions.
 */
extern struct c2_fop_type_format c2_conf_fetch_tfmt;
extern struct c2_fop_type_format c2_conf_fetch_resp_tfmt;

extern struct c2_fop_type c2_conf_fetch_fopt;
extern struct c2_fop_type c2_conf_fetch_resp_fopt;

extern const struct c2_fop_type_ops c2_conf_fetch_ops;
extern const struct c2_fop_type_ops c2_conf_fetch_resp_ops;

extern const struct c2_rpc_item_type c2_rpc_item_type_fetch;
extern const struct c2_rpc_item_type c2_rpc_item_type_fetch_resp;

/*
 * Confd update reply and request FOP definitions.
 */
extern struct c2_fop_type_format c2_conf_update_tfmt;
extern struct c2_fop_type_format c2_conf_update_resp_tfmt;

extern struct c2_fop_type c2_conf_update_fopt;
extern struct c2_fop_type c2_conf_update_resp_fopt;

extern const struct c2_fop_type_ops c2_conf_update_ops;
extern const struct c2_fop_type_ops c2_conf_update_resp_ops;

extern const struct c2_rpc_item_type c2_rpc_item_type_update;
extern const struct c2_rpc_item_type c2_rpc_item_type_update_resp;

/** @} confd_fop_dfspec */
#endif /* __COLIBRI_CONF_FOP_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
