/* -*- C -*- */
/*
 * COPYRIGHT 2015 SEAGATE TECHNOLOGY LIMITED
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
 * Original author: Facundo Domínguez <facundo.d.laumann@seagate.com>
 * Original creation date: 11-May-2015
 */

#include <stdio.h>

#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE  /* required for basename, see man basename(3) */
#endif
#include <string.h>  /* basename, strerror */

#include "ha/note.h"
#include "ha/note_fops.h"
#include "lib/getopts.h"
#include "lib/finject.h"
#include "mero/init.h"
#include "module/instance.h"      /* m0 */
#include "rpc/rpclib.h"


static struct m0                instance;
static struct m0_net_domain     client_net_dom;
static struct m0_rpc_client_ctx cctx;

extern void m0_stob_ut_adieu_linux(void);

int main(int argc, char *argv[])
{
	int rc;
	const char *local_endpoint = NULL, *ha_endpoint = NULL;

	rc = m0_init(&instance);
	if (rc != 0) {
		fprintf(stderr, "m0_init: %i %s\n", -rc, strerror(-rc));
		goto end;
	}

	rc = M0_GETOPTS(basename(argv[0]), argc, argv,
		    M0_HELPARG('h'),
		    M0_STRINGARG('l', "local rpc address",
				LAMBDA(void, (const char *str) {
					local_endpoint = str;
				})),
		    M0_STRINGARG('a', "ha rpc address",
				LAMBDA(void, (const char *str) {
					ha_endpoint = str;
				})),
		    );
	if (local_endpoint == NULL) {
                fprintf(stderr, "Missing -l argument.\n");
		rc = -1;
		goto end;
	}
	if (ha_endpoint == NULL) {
                fprintf(stderr, "Missing -a argument.\n");
		rc = -1;
		goto end;
	}
	if (rc != 0)
		goto end;

	static struct m0_fid process_fid = M0_FID_TINIT('r', 0, 1);

	cctx.rcx_net_dom               = &client_net_dom;
	cctx.rcx_local_addr            = local_endpoint;
	cctx.rcx_remote_addr           = ha_endpoint;
	cctx.rcx_max_rpcs_in_flight    = 32;
	cctx.rcx_recv_queue_min_length = M0_NET_TM_RECV_QUEUE_DEF_LEN;
	cctx.rcx_max_rpc_msg_size      = M0_RPC_DEF_MAX_RPC_MSG_SIZE;
	cctx.rcx_fid                   = &process_fid;

	rc = m0_net_domain_init(&client_net_dom, &m0_net_lnet_xprt);
	if (rc != 0) {
		fprintf(stderr, "m0_net_domain_init failed %i %s\n", -rc, strerror(-rc));
		goto fini;
	}


	rc = m0_rpc_client_start(&cctx);
	if (rc != 0) {
		fprintf(stderr, "rpc client init failed %i %s\n", -rc, strerror(-rc));
		goto net_dom_fini;
	}

	rc = m0_ha_state_init(&cctx.rcx_session);
	if (rc != 0)
		goto rpc_fini;

	m0_fi_enable_once("ioq_complete", "ioq_timeout");
	m0_stob_ut_adieu_linux();

	m0_ha_state_fini();

rpc_fini:
	rc = m0_rpc_client_stop(&cctx);
	if (rc != 0)
		printf("rpc client stop failed %i %s\n", -rc, strerror(-rc));
net_dom_fini:
	m0_net_domain_fini(&client_net_dom);
fini:
	m0_fini();
end:
	return rc < 0 ? -rc : rc;
}
