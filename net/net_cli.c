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
 * Original author: Alexey Lyashkov <Alexey_Lyashkov@xyratex.com>,
 *                  Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 04/01/2010
 */

#include "lib/errno.h"
#include "net/net.h"
#include "lib/memory.h"

/**
   @addtogroup netDep Networking (Deprecated Interfaces)

   @{
 */

static const struct c2_addb_loc net_cli_addb = {
	.al_name = "net-cli"
};

C2_ADDB_EV_DEFINE(net_addb_conn_send, "send", C2_ADDB_EVENT_NET_SEND,
		  C2_ADDB_STAMP);
C2_ADDB_EV_DEFINE(net_addb_conn_call, "call", C2_ADDB_EVENT_NET_CALL,
		  C2_ADDB_STAMP);

#define ADDB_ADD(conn, ev, ...) \
C2_ADDB_ADD(&(conn)->nc_addb, &net_cli_addb, ev , ## __VA_ARGS__)

/**
   Send the request to connection and wait for reply synchronously.
 */
int c2_net_cli_call(struct c2_net_conn *conn, struct c2_net_call *call)
{
	ADDB_ADD(conn, net_addb_conn_call);
	return conn->nc_ops->sio_call(conn, call);
}

/**
   Send the request to connection asynchronously and don't wait for reply.
 */
int c2_net_cli_send(struct c2_net_conn *conn, struct c2_net_call *call)
{
	ADDB_ADD(conn, net_addb_conn_send);
	return conn->nc_ops->sio_send(conn, call);
}

int c2_service_id_init(struct c2_service_id *id, struct c2_net_domain *dom, ...)
{
	va_list varargs;
	int     result;

	id->si_domain = dom;
	va_start(varargs, dom);
	result = dom->nd_xprt->nx_ops->xo_service_id_init(id, varargs);
	va_end(varargs);
	return result;
}

void c2_service_id_fini(struct c2_service_id *id)
{
	id->si_ops->sis_fini(id);
}

/** @} end of net group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
