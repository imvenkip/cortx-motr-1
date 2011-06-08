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
 * Original author: Alexey Lyashkov
 * Original creation date: 04/22/2010
 */

#include <errno.h>

#include "lib/errno.h"
#include "lib/memory.h"
#include "net/net.h"

#include "rpc/rpc_ops.h"

#include "rpc/session_types.h"
#include "rpc/session_svc.h"
#include "rpc/xdr/session.h"

static struct c2_rpc_op  create_session = {
	.ro_op = C2_SESSION_CREATE,
	.ro_arg_size = sizeof(struct c2_session_create_arg),
	.ro_xdr_arg = (c2_xdrproc_t)c2_xdr_session_create_arg,
	.ro_result_size = sizeof(struct c2_session_create_ret),
	.ro_xdr_result = (c2_xdrproc_t)c2_xdr_session_create_ret,
	.ro_handler = c2_session_create_svc
};

static struct c2_rpc_op  destroy_session = {
	.ro_op = C2_SESSION_DESTROY,
	.ro_arg_size = sizeof(struct c2_session_destroy_arg),
	.ro_xdr_arg = (c2_xdrproc_t)c2_xdr_session_destroy_arg,
	.ro_result_size = sizeof(struct c2_session_destroy_ret),
	.ro_xdr_result = (c2_xdrproc_t)c2_xdr_session_destroy_ret,
	.ro_handler = c2_session_destroy_svc
};

/** rpc handlers */
bool c2_session_create_svc(const struct c2_rpc_op *op, void *in, void **out)
{
	return true;
}

bool c2_session_destroy_svc(const struct c2_rpc_op *op, void *in, void **out)
{
	return true;
}

