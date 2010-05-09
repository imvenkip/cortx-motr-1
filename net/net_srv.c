#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <assert.h>

#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>

#include <lib/cdefs.h>
#include <lib/queue.h>
#include <lib/memory.h>
#include "net.h"

int c2_service_start(struct c2_service *service,
		     struct c2_service_id *sid,
		     struct c2_rpc_op_table *ops)
{
	C2_ASSERT(service->s_id == NULL);

	service->s_id     = sid;
	service->s_domain = sid->si_domain;
	service->s_table  = ops;
	return service->s_domain->nd_xprt->nx_ops->xo_service_init(service);
}

void c2_service_stop(struct c2_service *service)
{
	service->s_ops->so_stop(service);
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
