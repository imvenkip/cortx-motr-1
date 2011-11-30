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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 11/23/2011
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>

#include "lib/cdefs.h"
#include "lib/types.h"
#include "lib/memory.h"
#include "lib/assert.h"
#include "rpc/rpc2.h"
#include "net/net.h"
#include "fop/fop.h"
#include "reqh/reqh.h"

#include "rpc/rpclib.h"


static int init_cob(struct c2_rpc_ctx *rctx)
{
	int rc;
	struct c2_cob_domain_id   cob_dom_id = { .id = rctx->rx_cob_dom_id };

	rc = c2_dbenv_init(rctx->rx_dbenv, rctx->rx_db_name, 0);
	if (rc != 0)
		return rc;

	rc = c2_cob_domain_init(rctx->rx_cob_dom, rctx->rx_dbenv, &cob_dom_id);
	if (rc != 0)
		goto dbenv_fini;

	return rc;

dbenv_fini:
	c2_dbenv_fini(rctx->rx_dbenv);
	C2_ASSERT(rc != 0);
	return rc;
}

static void fini_cob(struct c2_rpc_ctx *rctx)
{
	c2_cob_domain_fini(rctx->rx_cob_dom);
	c2_dbenv_fini(rctx->rx_dbenv);

	return;
}

static int init_helper(int (*start_func)(struct c2_rpc_ctx *rctx),
		       struct c2_rpc_ctx *rctx)
{
	int rc;

	rc = init_cob(rctx);
	if (rc != 0)
		goto fini_cob;

	rc = start_func(rctx);

	return rc;

fini_cob:
	fini_cob(rctx);
	C2_ASSERT(rc != 0);
	return rc;
}

int c2_rpc_server_init(struct c2_rpc_ctx *rctx)
{
	return init_helper(c2_rpc_server_start, rctx);
}

void c2_rpc_server_fini(struct c2_rpc_ctx *rctx)
{
	c2_rpc_server_stop(rctx);
	fini_cob(rctx);
}

int c2_rpc_client_init(struct c2_rpc_ctx *rctx)
{
	return init_helper(c2_rpc_client_start, rctx);
}
C2_EXPORTED(c2_rpc_client_init);

int c2_rpc_client_fini(struct c2_rpc_ctx *rctx)
{
	int rc;

	rc = c2_rpc_client_stop(rctx);
	if (rc != 0)
		return rc;

	fini_cob(rctx);

	return rc;
}
C2_EXPORTED(c2_rpc_client_fini);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
