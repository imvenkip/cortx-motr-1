/* -*- C -*- */

#include <lib/cdefs.h>
#include <lib/rwlock.h>
#include <lib/memory.h>
#include <lib/assert.h>
#include <lib/c2list.h>
#include <lib/queue.h>
#include <lib/thread.h>
#include <lib/cond.h>
#include <net/net.h>

#include "sunrpc.h"
#include "sunrpc_internal.h"

/**
   @addtogroup ksunrpc Sun RPC

   Kernel User level Sunrpc-based implementation of C2 networking interfaces.

  @{
 */

static const struct c2_net_conn_ops kernel_sunrpc_conn_ops;
static const struct c2_service_id_ops kernel_sunrpc_service_id_ops;

/*
 * Client code.
 */

struct rpc_procinfo c2t0_procedures[] = {
};


struct rpc_version  c2t0_sunrpc_version = {
        .number     = 1,
        .nrprocs    = ARRAY_SIZE(c2t0_procedures),
        .procs      = c2t0_procedures
};

static struct rpc_version *c2t0_versions[2] = {
        [1]         = &c2t0_sunrpc_version,
};


struct rpc_program nfs_program = {
        .name                   = "c2t0",
        .number                 = C2_SESSION_PROGRAM,
        .nrvers                 = ARRAY_SIZE(c2t0_versions),
        .version                = c2t0_versions,
        .stats                  = &c2t0_rpcstat,
        .pipe_dir_name          = "/c2t0",
};

struct rpc_stat c2t0_rpcstat = {
        .program                = &c2t0_program
};

static void kernel_conn_fini_internal(struct sunrpc_conn *xconn)
{
	size_t i;

	if (xconn == NULL)
		return;

	if (xconn->nsc_pool != NULL) {
		for (i = 0; i < KERN_CONN_CLIENT_COUNT; ++i) {
			if (xconn->nsc_pool[i].c.k.nsx_client != NULL) {
				struct rpc_client *clnt;
				clnt = xconn->nsc_pool[i].c.k.nsx_client;

				rpc_shutdown_client(clnt);
				xconn->nsc_pool[i].c.k.nsx_client = NULL;
			}
		}
		c2_free(xconn->nsc_pool);
		xconn->nsc_pool = NULL;
	}

	c2_free(xconn);
}

/* TODO: replace these NFS_* with C2_* */
static int kernel_conn_init_one(struct sunrpc_service_id *xsid,
			        struct sunrpc_conn *xconn,
				struct sunrpc_xprt *xprt)
{
        struct rpc_clnt         *clnt = NULL;
        struct rpc_timeout       timeparms = {
                .to_retries   = NFS_DEF_TCP_RETRANS,
                .to_initval   = NFS_DEF_TCP_TIMEO * HZ / 10,
                .to_increment = NFS_DEF_TCP_TIMEO * HZ / 10,
                .to_maxval    = NFS_DEF_TCP_TIMEO * HZ / 10 +
			       (NFS_DEF_TCP_TIMEO * HZ / 10 * NFS_DEF_TCP_RETRANS),
                .to_exponential = 0
	};
        struct rpc_create_args args = {
                .protocol       = XPRT_TRANSPORT_TCP,
                .address        = (struct sockaddr *)xsid->ssi_sockaddr,
                .addrsize       = xsid->ssi_addrlen,
                .timeout        = &timeparms,
                .servername     = xsid->ssi_host,
                .program        = &c2t0_program,
                .version        = 1,
                .authflavor     = RPC_AUTH_NULL,
                .flags          = 0,
        };

        clnt = rpc_create(&args);
        if (IS_ERR(clnt)) {
                printk("%s: cannot create RPC client. Error = %ld\n",
                                __func__, PTR_ERR(clnt));
                return PTR_ERR(clnt);
        }

	xprt->c.k.client = clnt;
        return 0;
}

static int kernel_sunrpc_conn_init(struct c2_service_id *id, struct c2_net_conn *conn)
{
	struct sunrpc_service_id *xsid = id->si_xport_private;
	struct sunrpc_conn       *xconn;
	struct sunrpc_xprt       *pool;
	int                       result;

	if (dom_is_shutting(conn->nc_domain))
		return -ESHUTDOWN;

	result = -ENOMEM;
	C2_ALLOC_PTR(xconn);

	if (xconn != NULL) {
		C2_ALLOC_ARR(pool, KERN_CONN_CLIENT_COUNT);
		if (pool != NULL) {
			size_t i;

			c2_mutex_init(&xconn->nsc_guard);
			c2_queue_init(&xconn->nsc_idle);
			c2_cond_init(&xconn->nsc_gotfree);
			xconn->nsc_pool = pool;
			xconn->nsc_nr   = KERN_CONN_CLIENT_COUNT;
			conn->nc_ops    = &kernel_sunrpc_conn_ops;
			conn->nc_xprt_private = xconn;

			for (i = 0; i < KERN_CONN_CLIENT_COUNT; ++i) {
				result = kernel_conn_init_one(xsid, xconn,
							      &pool[i]);
				if (result != 0)
					break;
			}
		}
	}

	if (result != 0) {
		/* xconn & pool will be released there */
		kernel_conn_fini_internal(xconn);
	}

	return result;
}

static void kernel_sunrpc_conn_fini(struct c2_net_conn *conn)
{
	kernel_conn_fini_internal(conn->nc_xprt_private);
	conn->nc_xprt_private = NULL;
}

static int kernel_sunrpc_conn_call(struct c2_net_conn *conn,
				   const struct c2_rpc_op *op, void *arg, void *ret)
{
	struct sunrpc_conn *xconn;
	struct sunrpc_xprt *xprt;
	int                 result;

	C2_ASSERT(!dom_is_shutting(conn->nc_domain));

	xconn = conn->nc_xprt_private;
	xprt = &xconn->nsc_pool[0];

	struct rpc_procinfo proc = {
		.p_proc   = op->ro_op,
		.p_encode = (kxdrproc_t) op->ro_xdr_arg,
		.p_decode = (kxdrproc_t) op->ro_xdr_result,
		.p_arglen = op->ro_arg_size,
		.p_replen = op->ro_result_size
		.p_statidx= op->ro_op,
		.p_name   = op->ro_name,
	};

        struct rpc_message msg = {
                .rpc_proc = &proc,
                .rpc_argp = arg,
                .rpc_resp = ret,
        };

        result = rpc_call_sync(xprt->c.k.nsx_client, &msg, 0);

	return result;
}

static void kernel_sunrpc_async_done(struct rpc_task *task, void *data)
{
	struct c2_net_async_call *call = (struct c2_net_async_call *)data;

        call->ac_rc = task->tk_status;
	c2_chan_broadcast(&call->ac_chan);
}


static const struct rpc_call_ops kernel_sunrpc_async_ops = {
        .rpc_call_done = kernel_sunrpc_async_done,
};

static int kernel_sunrpc_conn_send(struct c2_net_conn *conn,
				   struct c2_net_async_call *call)
{
	struct sunrpc_conn *xconn;
	struct sunrpc_xprt *xprt;
	int                 result;

	C2_ASSERT(!dom_is_shutting(conn->nc_domain));

	call->ac_conn = conn;
	xconn = conn->nc_xprt_private;
	xprt = &xconn->nsc_pool[0];

	struct rpc_procinfo proc = {
		.p_proc   = op->ro_op,
		.p_encode = (kxdrproc_t) op->ro_xdr_arg,
		.p_decode = (kxdrproc_t) op->ro_xdr_result,
		.p_arglen = op->ro_arg_size,
		.p_replen = op->ro_result_size
		.p_statidx= op->ro_op,
		.p_name   = op->ro_name,
	};

        struct rpc_message msg = {
                .rpc_proc = &proc,
                .rpc_argp = arg,
                .rpc_resp = ret,
        };

        result = rpc_call_async(xprt->c.k.nsx_client, &msg, RPC_TASK_SOFT,
                        &kernel_sunrpc_async_ops, (void *)call);

	return result;
}

static void kernel_sunrpc_service_id_fini(struct c2_service_id *sid)
{
	if (sid->si_xport_private != NULL) {
		c2_free(sid->si_xport_private);
		sid->si_xport_private = NULL;
	}
}

static int kernel_sunrpc_service_id_init(struct c2_service_id *sid, va_list varargs)
{
	struct sunrpc_service_id *xsid;
	int                       result = -ENOMEM;

	C2_ALLOC_PTR(xsid);
	if (xsid != NULL) {
		sid->si_xport_private = xsid;
		xsid->ssi_id = sid;

		/* N.B. they have different order than userspace's ones */
		xsid->ssi_host     = va_arg(varargs, char *);
		xsid->ssi_sockaddr = va_arg(varargs, struct sockaddr *);
		xsid->ssi_addrlen = va_arg(varargs, int);
		xsid->ssi_port     = va_arg(varargs, int);

		sid->si_ops = &kernel_sunrpc_service_id_ops;
		result = 0;
	}
	if (result != 0)
		kernel_sunrpc_service_id_fini(sid);
	return result;
}

/*
 * Domain code.
 */

static void kernel_dom_fini(struct c2_net_domain *dom)
{
	struct sunrpc_dom  *xdom;

	C2_ASSERT(c2_list_is_empty(&dom->nd_conn));
	C2_ASSERT(c2_list_is_empty(&dom->nd_service));

	xdom = dom->nd_xprt_private;
	if (xdom != NULL) {
		int i;

		xdom->sd_shutown = true;
		/* wake up all worker threads so that they detect shutdown and
		   exit */
		c2_cond_broadcast(&xdom->sd_gotwork);
		c2_mutex_fini(&xdom->sd_guard);
		c2_queue_fini(&xdom->sd_queue);
		c2_cond_fini(&xdom->sd_gotwork);

		c2_free(xdom);
		dom->nd_xprt_private = NULL;
	}
}

static int kernel_dom_init(struct c2_net_xprt *xprt, struct c2_net_domain *dom)
{
	int i;
	int result = -ENOMEM;
	struct sunrpc_dom *xdom;

	C2_ASSERT(dom->nd_xprt_private == NULL);

	C2_ALLOC_PTR(xdom);
	if (xdom != NULL) {
		dom->nd_xprt_private = xdom;
		c2_cond_init(&xdom->sd_gotwork);
		c2_mutex_init(&xdom->sd_guard);
		c2_queue_init(&xdom->sd_queue);
		result = 0;
	}
	return result;
}

static const struct c2_service_id_ops kernel_sunrpc_service_id_ops = {
	.sis_conn_init = kernel_sunrpc_conn_init,
	.sis_fini      = kernel_sunrpc_service_id_fini
};

static const struct c2_net_conn_ops kernel_sunrpc_conn_ops = {
	.sio_fini = kernel_sunrpc_conn_fini,
	.sio_call = kernel_sunrpc_conn_call,
	.sio_send = kernel_sunrpc_conn_send
};

static const struct c2_net_xprt_ops kernel_sunrpc_xprt_ops = {
	.xo_dom_init        = kernel_dom_init,
	.xo_dom_fini        = kernel_dom_fini,
	.xo_service_id_init = kernel_sunrpc_service_id_init,
	.xo_service_init    = NULL
};

struct c2_net_xprt c2_net_kernel_sunrpc_xprt = {
	.nx_name = "sunrpc/kernel",
	.nx_ops  = &kernel_sunrpc_xprt_ops
};

int kernel_sunrpc_init(void)
{
	return 0;
}

void kernel_sunrpc_fini(void)
{
}

/** @} end of group ksunrpc */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
