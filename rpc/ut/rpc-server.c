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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <net/net.h>
#include <rpc/rpclib.h>
#include <rpc/rpc_ops.h>
#include <rpc/pcache.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "lib/misc.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "fop/fop.h"
#include "net/net.h"
#include "net/usunrpc/usunrpc.h"

#include "stob/stob.h"
#include "stob/linux.h"
#include "stob/ad.h"
#include "colibri/init.h"

#include "rpc/session_fops.h"
#include "rpc/session_u.h"
#include "rpc/session_foms.h"

static struct c2_fol fol;

static int rpc_handler(struct c2_service *service, struct c2_fop *fop,
			void *cookie)
{
	int rc;
	printf("in rpc_handler \n");
	rc = c2_rpc_dummy_req_handler(service, fop, cookie, &fol, NULL);
	return rc;
}
static struct c2_fop_type *fopt[] = {
	&c2_rpc_conn_create_fopt,
	&c2_rpc_conn_terminate_fopt,
	&c2_rpc_session_create_fopt,
	&c2_rpc_session_destroy_fopt,
	&c2_rpc_conn_create_rep_fopt,
	&c2_rpc_conn_terminate_rep_fopt,
	&c2_rpc_session_create_rep_fopt,
	&c2_rpc_session_destroy_rep_fopt,
};

static bool stop = false;

int main()
{
	int         result;
	const char *path;
	char       opath[64];
	char       dpath[64];
	int        port;
	int        i = 0;
       
	struct c2_stob_domain   *bdom;
	struct c2_stob_id        backid;
	struct c2_stob          *bstore;
	struct c2_service_id     sid = { .si_uuid = "ABCDEFG" };
	struct c2_service        service;
	struct c2_net_domain     ndom;
	struct c2_dbenv          db;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	backid.si_bits.u_hi = 0xF;
	backid.si_bits.u_lo = 0xFFFF;
	
	port = 1300;
	path = "temp";
	printf("path = %s, back store object=%llx.%llx, tcp_port = %d\n",
		path, (unsigned long long) backid.si_bits.u_hi,
		(unsigned long long) backid.si_bits.u_lo, port);
       
	result = c2_init();
	C2_ASSERT(result == 0);
	
	result = c2_rpc_session_fop_init();
        C2_ASSERT(result == 0);

	 C2_ASSERT(strlen(path) < ARRAY_SIZE(opath) - 8);
	
	result = mkdir(path, 0700);
	C2_ASSERT(result == 0 || (result == -1 && errno == EEXIST));
	sprintf(opath, "%s/o", path);
	result = mkdir(opath, 0700);
	C2_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	sprintf(dpath, "%s/d", path);
	
	/*
         * Initialize the data-base and fol.
         */
	result = c2_dbenv_init(&db, dpath, 0);
	C2_ASSERT(result == 0);

	result = c2_fol_init(&fol, &db);
	C2_ASSERT(result == 0);
	
	result = linux_stob_type.st_op->sto_domain_locate(&linux_stob_type,
                                                          path, &bdom);
        C2_ASSERT(result == 0);

	result = bdom->sd_ops->sdo_stob_find(bdom, &backid, &bstore);
	C2_ASSERT(result == 0);
	C2_ASSERT(bstore->so_state == CSS_UNKNOWN);

	result = c2_stob_create(bstore, NULL);
	C2_ASSERT(result == 0);
	C2_ASSERT(bstore->so_state == CSS_EXISTS);

	c2_stob_put(bstore);

        /*
         * Set up the service.
         */
	C2_SET0(&service);

	service.s_table.not_start = fopt[0]->ft_code;
	service.s_table.not_nr    = ARRAY_SIZE(fopt);
	service.s_table.not_fopt  = fopt;
	service.s_handler         = &rpc_handler;

	result = c2_net_xprt_init(&c2_net_usunrpc_xprt);
	C2_ASSERT(result == 0);

	result = c2_net_domain_init(&ndom, &c2_net_usunrpc_xprt);
	C2_ASSERT(result == 0);

	result = c2_service_id_init(&sid, &ndom, "127.0.0.1", port);
	C2_ASSERT(result == 0);

	result = c2_service_start(&service, &sid);
	C2_ASSERT(result >= 0);

	while (!stop) {
		sleep(1);
		if (i++ % 5 == 0)
			printf("busy: in=%5.2f out=%5.2f\n",
				(float)c2_net_domain_stats_get(&ndom, NS_STATS_IN) / 100,
				(float)c2_net_domain_stats_get(&ndom, NS_STATS_OUT) / 100);
	}

	c2_service_stop(&service);
	c2_service_id_fini(&sid);
	c2_net_domain_fini(&ndom);
	c2_net_xprt_fini(&c2_net_usunrpc_xprt);

	bdom->sd_ops->sdo_fini(bdom);
	c2_rpc_session_fop_fini();
	c2_fol_fini(&fol);
	c2_dbenv_fini(&db);
	c2_fini();			
	return 0;
}

