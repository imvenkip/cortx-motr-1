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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 06/15/2010
 */

#ifndef __COLIBRI_NET_SUNRPC_SUNRPC_H__
#define __COLIBRI_NET_SUNRPC_SUNRPC_H__

#include <rpc/xdr.h>

#include "lib/cdefs.h"

/**
   @addtogroup usunrpc User Level Sun RPC
   @{
 */

extern struct c2_net_xprt c2_net_usunrpc_xprt;

struct c2_fop_field_type;

bool_t c2_fop_type_uxdr(const struct c2_fop_field_type *ftype,
			XDR *xdrs, void *obj);
bool_t c2_fop_uxdrproc(XDR *xdrs, struct c2_fop *fop);
int usunrpc_init(void);
void usunrpc_fini(void);
/** @} end of group usunrpc */

/* __COLIBRI_NET_SUNRPC_SUNRPC_H__ */
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
