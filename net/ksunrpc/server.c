/* -*- C -*- */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/svcsock.h>

#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/thread.h"
#include "fop/fop.h"
#include "addb/addb.h"

#include "ksunrpc.h"

/**
   @addtogroup ksunrpc Sun RPC

   Sunrpc server implementation.  Based on the design of user-level
   sunrpc server implementation.

   @{
 */

enum {
	SERVER_THR_NR = 8,
	MIN_SERVER_THR_NR = 2
};

struct ksunrpc_service {
	/**
	   SUNRPC transport handle for this service
	 */
	struct svc_xprt	       *s_transp;

	/**
	   scheduler thread handle
	 */
	struct c2_thread	s_scheduler_thread;

	/**
	   number of worker threads
	 */
	int			s_nr_workers;
	/**
	   worker thread array
	 */
	struct c2_thread       *s_workers;
	/**
	   The service is being shut down.
	 */
	bool			s_shutdown;
	/**
	   Service mutex, must be held across various sunrpc svc calls.
	 */
	struct c2_mutex         s_svc_mutex;
};

static const struct c2_addb_loc ksunrpc_addb_server = {
	.al_name = "ksunrpc-server"
};

static void ksunrpc_service_worker(struct c2_service *service)
{
	C2_IMPOSSIBLE("implement me");
}

static void ksunrpc_op(struct c2_service *service,
		       struct c2_fop_type *fopt, struct svc_xprt *transp)
{
	C2_IMPOSSIBLE("implement me");
}

static void ksunrpc_dispatch(struct svc_rqst *req)
{
	C2_IMPOSSIBLE("implement me");
	ksunrpc_op(NULL, NULL, NULL);
}

static int ksunrpc_scheduler_init(struct c2_service *service)
{
	C2_IMPOSSIBLE("implement me");
	ksunrpc_dispatch(NULL);
	return -ENOSYS;
}

static void ksunrpc_scheduler(struct c2_service *service)
{
	C2_IMPOSSIBLE("implement me");
}

static void ksunrpc_service_stop(struct ksunrpc_service *xs)
{
	C2_IMPOSSIBLE("implement me");
}

static int ksunrpc_service_start(struct c2_service *service,
				 struct ksunrpc_service_id *xid, int nr_workers)
{
	C2_IMPOSSIBLE("implement me");
	ksunrpc_scheduler_init(service);
	ksunrpc_scheduler(service);
	ksunrpc_service_worker(NULL);
	return -ENOSYS;
}

static void ksunrpc_service_fini(struct c2_service *service)
{
	struct ksunrpc_service *xs;

	xs = service->s_xport_private;

	ksunrpc_service_stop(xs);

	C2_ASSERT(xs->s_workers == NULL);
	c2_mutex_fini(&xs->s_svc_mutex);
	c2_free(xs);
}

int ksunrpc_service_init(struct c2_service *service)
{
	struct ksunrpc_service    *xservice;
	struct ksunrpc_service_id *xid;
	int                        result;

	C2_ALLOC_PTR(xservice);
	if (xservice != NULL) {
		int num_threads;
		c2_mutex_init(&xservice->s_svc_mutex);
		service->s_xport_private = xservice;
		service->s_ops = &ksunrpc_service_ops;
		xid = service->s_id->si_xport_private;
		C2_ASSERT(service->s_id->si_ops == &ksunrpc_service_id_ops);
		if (service->s_domain->nd_xprt == &c2_net_ksunrpc_minimal_xprt)
			num_threads = MIN_SERVER_THR_NR;
		else
			num_threads = SERVER_THR_NR;
		result = ksunrpc_service_start(service, xid, num_threads);
	} else {
		C2_ADDB_ADD(&service->s_domain->nd_addb, &ksunrpc_addb_server,
			    c2_addb_oom);
		result = -ENOMEM;
	}
	return result;
}

/**
   Implementation of c2_service_ops::sio_reply_post.
 */
static void ksunrpc_reply_post(struct c2_service *service,
			       struct c2_fop *fop, void *cookie)
{
	struct c2_fop **ret = cookie;

	C2_ASSERT(*ret == NULL);
	*ret = fop;
}

int ksunrpc_server_init(void)
{
	return 0;
}

void ksunrpc_server_fini(void)
{
}

const struct c2_service_ops ksunrpc_service_ops = {
	.so_fini       = ksunrpc_service_fini,
	.so_reply_post = ksunrpc_reply_post
};

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
