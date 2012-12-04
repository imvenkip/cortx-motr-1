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

#include "lib/cdefs.h"
#include "lib/types.h"
#include "lib/memory.h"
#include "lib/assert.h"
#include "rpc/rpc.h"
#include "net/net.h"
#include "fop/fop.h"

#include "rpc/rpclib.h"


static int init_cob(struct m0_rpc_client_ctx *cctx)
{
	int rc;
	struct m0_cob_domain_id   cob_dom_id = { .id = cctx->rcx_cob_dom_id };

	rc = m0_dbenv_init(cctx->rcx_dbenv, cctx->rcx_db_name, 0);
	if (rc != 0)
		return rc;

	rc = m0_cob_domain_init(cctx->rcx_cob_dom, cctx->rcx_dbenv, &cob_dom_id);
	if (rc != 0)
		goto dbenv_fini;

	return rc;

dbenv_fini:
	m0_dbenv_fini(cctx->rcx_dbenv);
	M0_ASSERT(rc != 0);
	return rc;
}

static void fini_cob(struct m0_rpc_client_ctx *cctx)
{
	m0_cob_domain_fini(cctx->rcx_cob_dom);
	m0_dbenv_fini(cctx->rcx_dbenv);
}

int m0_rpc_client_init(struct m0_rpc_client_ctx *cctx)
{
	int rc;

	rc = init_cob(cctx);
	if (rc != 0)
		return rc;

	rc = m0_rpc_client_start(cctx);
	if (rc != 0)
		fini_cob(cctx);
	return rc;
}
M0_EXPORTED(m0_rpc_client_init);

int m0_rpc_client_fini(struct m0_rpc_client_ctx *cctx)
{
	int rc;

	rc = m0_rpc_client_stop(cctx);
	if (rc != 0)
		return rc;

	fini_cob(cctx);

	return rc;
}
M0_EXPORTED(m0_rpc_client_fini);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
