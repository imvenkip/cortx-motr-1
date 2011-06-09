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
 * Original author: Alexey Lyashkov <Alexey_Lyashkov@xyratex.com>
 * Original creation date: 04/22/2010
 */
#include "rpc/rpclib.h"
#if 0

#include "lib/errno.h"
#include "lib/atomic.h"
#include "lib/memory.h"

#include "net/net.h"

#include "rpc/session_srv.h"

int c2_server_session_init(struct c2_rpc_server *srv)
{
	int rc;

	rc = c2_cache_init(&srv->rs_sessions);
	if (rc < 0)
		goto out;
}

void c2_server_session_fini(struct c2_rpc_server *srv)
{
	c2_cache_fini(&srv->rs_session);
}


/*** ***/
int c2_srv_session_init(struct c2_rpc_server *srv, uint32_t highslot,
			struct c2_service_id *cli_id;
			struct c2_srv_session **sess)
{
}

void c2_srv_session_unlink(struct c2_rpc_server *srv, struct c2_srv_session *sess)
{
}

struct c2_srv_session *c2_srv_session_find_by_id(struct c2_rpc_server *srv,
						 const c2_session_id *ss_id)
{
}

#endif
