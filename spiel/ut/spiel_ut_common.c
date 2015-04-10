/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Mikhail Antropov <mikhail.v.antropov@seagate.com>
 * Original creation date: 02/24/2015
 */
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SPIEL
#include "lib/trace.h"

#include "net/lnet/lnet.h"             /* m0_net_lnet_xprt */
#include "net/net.h"
#include "rpc/rpc.h"
#include "rpc/rpclib.h"
#include "rpc/rpc_machine.h"
#include "reqh/reqh.h"
#include "spiel/spiel.h"
#include "ut/ut.h"
#include "spiel/ut/spiel_ut_common.h"

M0_INTERNAL int m0_spiel__ut_reqh_init(struct m0_spiel_ut_reqh *spl_reqh,
		                       const char              *ep_addr)
{
	struct m0_net_xprt *xprt = &m0_net_lnet_xprt;
	enum { NR_TMS = 1 };
	int rc;

	M0_SET0(spl_reqh);
	rc = m0_net_domain_init(&spl_reqh->sur_net_dom, xprt);
	if (rc != 0)
		return rc;

	rc = m0_rpc_net_buffer_pool_setup(&spl_reqh->sur_net_dom,
	                                  &spl_reqh->sur_buf_pool,
	                                  m0_rpc_bufs_nr(
	                                     M0_NET_TM_RECV_QUEUE_DEF_LEN,
	                                     NR_TMS),
	                                  NR_TMS);
	if (rc != 0)
		goto net;

	rc = M0_REQH_INIT(&spl_reqh->sur_reqh,
			  .rhia_dtm     = (void *)1,
			  .rhia_mdstore = (void *)1);
	if (rc != 0)
		goto buf_pool;
	m0_reqh_start(&spl_reqh->sur_reqh);

	rc = m0_rpc_machine_init(&spl_reqh->sur_rmachine,
	                         &spl_reqh->sur_net_dom, ep_addr,
	                         &spl_reqh->sur_reqh, &spl_reqh->sur_buf_pool,
	                         M0_BUFFER_ANY_COLOUR,
	                         M0_RPC_DEF_MAX_RPC_MSG_SIZE,
	                         M0_NET_TM_RECV_QUEUE_DEF_LEN);
	if (rc == 0) {
		return 0;
	}
buf_pool:
	m0_rpc_net_buffer_pool_cleanup(&spl_reqh->sur_buf_pool);
net:
	m0_net_domain_fini(&spl_reqh->sur_net_dom);
	return rc;
}

M0_INTERNAL void m0_spiel__ut_reqh_fini(struct m0_spiel_ut_reqh *spl_reqh)
{
	m0_reqh_services_terminate(&spl_reqh->sur_reqh);
	m0_rpc_machine_fini(&spl_reqh->sur_rmachine);
	m0_reqh_fini(&spl_reqh->sur_reqh);
	m0_rpc_net_buffer_pool_cleanup(&spl_reqh->sur_buf_pool);
	m0_net_domain_fini(&spl_reqh->sur_net_dom);
}

M0_INTERNAL int m0_spiel__ut_confd_start(struct m0_rpc_server_ctx *rpc_srv,
					 const char               *confd_ep,
					 const char               *confdb_path)
{
	enum {
		LOG_NAME_MAX_LEN     = 128,
		EP_MAX_LEN           = 24,
		RPC_SIZE_MAX_LEN     = 32,
	};

	char                log_name[LOG_NAME_MAX_LEN];
	char                full_ep[EP_MAX_LEN];
	char                max_rpc_size[RPC_SIZE_MAX_LEN];
	struct m0_net_xprt *xprt = &m0_net_lnet_xprt;

	snprintf(full_ep, EP_MAX_LEN, "lnet:%s", confd_ep);
	snprintf(max_rpc_size, RPC_SIZE_MAX_LEN,
		 "%d", M0_RPC_DEF_MAX_RPC_MSG_SIZE);

#define NAME(ext) "ut_spiel" ext
	char                    *argv[] = {
		NAME(""), "-T", "AD", "-D", NAME(".db"),
		"-S", NAME(".stob"), "-A", "linuxstob:"NAME("-addb_stob"),
		"-w", "10",
		"-e", full_ep, "-s", "confd", "-m", max_rpc_size,
		"-c", (char *)confdb_path
	};
#undef NAME

	M0_SET0(rpc_srv);

	rpc_srv->rsx_xprts         = &xprt;
	rpc_srv->rsx_xprts_nr      = 1;
	rpc_srv->rsx_argv          = argv;
	rpc_srv->rsx_argc          = ARRAY_SIZE(argv);
	snprintf(log_name, LOG_NAME_MAX_LEN, "confd_%s.log", confd_ep);
	rpc_srv->rsx_log_file_name = log_name;

	return m0_rpc_server_start(rpc_srv);
}

M0_INTERNAL void m0_spiel__ut_confd_stop(struct m0_rpc_server_ctx *rpc_srv)
{
	m0_rpc_server_stop(rpc_srv);
}

#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
