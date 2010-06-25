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

#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>

#include "lib/assert.h"
#include "lib/cdefs.h"
#include "lib/queue.h"
#include "lib/memory.h"

#include "net.h"

/**
   @addtogroup net Networking.

   @{
 */

static const struct c2_addb_ctx_type c2_net_service_addb_ctx = {
	.act_name = "net-service"
};

int c2_service_start(struct c2_service *service,
		     struct c2_service_id *sid,
		     struct c2_rpc_op_table *ops)
{
	int                   result;
	struct c2_net_domain *dom;

	C2_ASSERT(service->s_id == NULL);

	dom = sid->si_domain;
	service->s_id     = sid;
	service->s_domain = dom;
	service->s_table  = ops;
	c2_list_link_init(&service->s_linkage);
	result = service->s_domain->nd_xprt->nx_ops->xo_service_init(service);
	if (result == 0) {
		c2_rwlock_read_lock(&dom->nd_lock);
		c2_list_add(&dom->nd_service, &service->s_linkage);
		c2_rwlock_read_unlock(&dom->nd_lock);
		c2_addb_ctx_init(&service->s_addb, &c2_net_service_addb_ctx,
				 &c2_addb_global_ctx);
	}
	return result;
}

void c2_service_stop(struct c2_service *service)
{
	struct c2_net_domain *dom;

	dom = service->s_domain;
	service->s_ops->so_fini(service);
	c2_addb_ctx_fini(&service->s_addb);
	c2_rwlock_read_lock(&dom->nd_lock);
	c2_list_del(&service->s_linkage);
	c2_rwlock_read_unlock(&dom->nd_lock);
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
