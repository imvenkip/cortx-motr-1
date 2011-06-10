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
 * Original creation date: 05/18/2010
 */

#include <net/net.h>
#include <rpc/rpclib.h>
#include <rpc/rpc_ops.h>
#include <rpc/pcache.h>


int main(void)
{
	struct c2_service_id srv_id = { .si_uuid = "srv-1" };
	struct c2_rpc_server *srv;
	struct c2_service	s;
	int rc;

	c2_rpclib_init();

	rc = c2_service_start(&s, &srv_id);

	srv = c2_rpc_server_create(&srv_id);

	/* init storage if need */

	/* service want pcache support */
	c2_pcache_init(srv);

	/* service want session support */
	/* need add commands to service */
	// c2_server_session_init(srv);

	/* add compound rpc in list*/
	// c2_server_compond_init(srv);

	//c2_server_register(srv);
	return 0;
}
