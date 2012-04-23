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
 *                  Nikita Danilov <Nikita_Danilov@xyratex.com>,
 *                  Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 04/11/2010
 */

#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/cdefs.h"
#include "lib/queue.h"
#include "lib/memory.h"
#include "fop/fop.h"

#include "net/net.h"

/**
   @addtogroup netDep Networking (Deprecated Interfaces)

   @{
 */

static const struct c2_addb_ctx_type c2_net_service_addb_ctx = {
	.act_name = "net-service"
};

int c2_service_start(struct c2_service *service, struct c2_service_id *sid)
{
	int                   result;
	int                   i;
	struct c2_net_domain *dom;

	C2_ASSERT(service->s_id == NULL);
	C2_ASSERT(service->s_table.not_nr > 0);

	for (i = 0; i < service->s_table.not_nr; ++i) {
		C2_ASSERT(
		service->s_table.not_fopt[i]->ft_rpc_item_type.rit_opcode ==
		service->s_table.not_start + i);
	}

	dom = sid->si_domain;
	service->s_id     = sid;
	service->s_domain = dom;
	c2_list_link_init(&service->s_linkage);
	c2_addb_ctx_init(&service->s_addb, &c2_net_service_addb_ctx,
			 &c2_addb_global_ctx);
	result = service->s_domain->nd_xprt->nx_ops->xo_service_init(service);
	if (result == 0) {
		c2_rwlock_write_lock(&dom->nd_lock);
		c2_list_add(&dom->nd_service, &service->s_linkage);
		c2_rwlock_write_unlock(&dom->nd_lock);
	}
	return result;
}

void c2_service_stop(struct c2_service *service)
{
	struct c2_net_domain *dom;

	dom = service->s_domain;
	service->s_ops->so_fini(service);
	c2_addb_ctx_fini(&service->s_addb);
	c2_rwlock_write_lock(&dom->nd_lock);
	c2_list_del(&service->s_linkage);
	c2_rwlock_write_unlock(&dom->nd_lock);
}

void c2_net_reply_post(struct c2_service *service,
		       struct c2_fop *fop, void *cookie)
{
	service->s_ops->so_reply_post(service, fop, cookie);
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
