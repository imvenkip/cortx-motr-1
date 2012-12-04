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
 */
/*
 * Packet loss testing tool.
 *
 * This tool is used to investigate the behavior of transport layer to simulate
 * the packet loss, reorder, duplication over the network.
 *
 * Written by Jay <jinshan.xiong@clusterstor.com>
 */

#include <memory.h> /* for memset */
#include "pl.h"

/* Default timeout can be changed using clnt_control() */
static struct timeval TIMEOUT = { 25, 0 };

enum clnt_stat
ping_1(struct m0_pl_ping *argp, struct m0_pl_ping_res *clnt_res, CLIENT *clnt)
{
	return (clnt_call(clnt, PING,
		(xdrproc_t) xdr_m0_pl_ping, (caddr_t) argp,
		(xdrproc_t) xdr_m0_pl_ping_res, (caddr_t) clnt_res,
		TIMEOUT));
}

enum clnt_stat
setconfig_1(struct m0_pl_config *argp, struct m0_pl_config_res *clnt_res, CLIENT *clnt)
{
	return (clnt_call(clnt, SETCONFIG,
		(xdrproc_t) xdr_m0_pl_config, (caddr_t) argp,
		(xdrproc_t) xdr_m0_pl_config_res, (caddr_t) clnt_res,
		TIMEOUT));
}

enum clnt_stat
getconfig_1(struct m0_pl_config *argp, struct m0_pl_config_res *clnt_res, CLIENT *clnt)
{
	return (clnt_call(clnt, GETCONFIG,
		(xdrproc_t) xdr_m0_pl_config, (caddr_t) argp,
		(xdrproc_t) xdr_m0_pl_config_res, (caddr_t) clnt_res,
		TIMEOUT));
}
