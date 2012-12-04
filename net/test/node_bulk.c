/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 09/03/2012
 */

#include "lib/errno.h"		/* ENOSYS */

#include "net/test/network.h"	/* m0_net_test_network_ctx */
#include "net/test/node.h"	/* m0_net_test_node_ctx */
#include "net/test/service.h"	/* m0_net_test_service */

#include "net/test/node_bulk.h"

/**
   @defgroup NetTestBulkNodeInternals Bulk Node
   @ingroup NetTestInternals

   @{
 */

static void node_bulk_tm_event_cb(const struct m0_net_tm_event *ev)
{
	/* nothing for now */
}

static const struct m0_net_tm_callbacks node_bulk_tm_cb = {
	.ntc_event_cb = node_bulk_tm_event_cb
};

static void node_bulk_msg_cb(struct m0_net_test_network_ctx *net_ctx,
			     const uint32_t buf_index,
			     enum m0_net_queue_type q,
			     const struct m0_net_buffer_event *ev)
{
}

/**
@todo static
*/
struct m0_net_test_network_buffer_callbacks node_bulk_buf_cb = {
	.ntnbc_cb = {
		[M0_NET_QT_MSG_RECV]		= node_bulk_msg_cb,
		[M0_NET_QT_MSG_SEND]		= node_bulk_msg_cb,
		[M0_NET_QT_PASSIVE_BULK_RECV]	= node_bulk_msg_cb,
		[M0_NET_QT_PASSIVE_BULK_SEND]	= node_bulk_msg_cb,
		[M0_NET_QT_ACTIVE_BULK_RECV]	= node_bulk_msg_cb,
		[M0_NET_QT_ACTIVE_BULK_SEND]	= node_bulk_msg_cb,
	}
};

static void *node_bulk_init_fini(struct m0_net_test_service *svc,
				 void *ctx_,
				 bool init)
{
	return NULL;
}

static void *node_bulk_init(struct m0_net_test_service *svc)
{
	return node_bulk_init_fini(svc, NULL, true);
}

static void node_bulk_fini(void *ctx_)
{
	void *rc = node_bulk_init_fini(NULL, ctx_, false);
	M0_POST(rc == NULL);
}

static int node_bulk_step(void *ctx_)
{
	return -ENOSYS;
}

static int node_bulk_cmd_init(void *ctx_,
			      const struct m0_net_test_cmd *cmd,
			      struct m0_net_test_cmd *reply)
{
	return -ENOSYS;
}

static int node_bulk_cmd_start(void *ctx,
			       const struct m0_net_test_cmd *cmd,
			       struct m0_net_test_cmd *reply)
{
	return -ENOSYS;
}

static int node_bulk_cmd_stop(void *ctx,
			      const struct m0_net_test_cmd *cmd,
			      struct m0_net_test_cmd *reply)
{
	return -ENOSYS;
}

static int node_bulk_cmd_status(void *ctx,
				const struct m0_net_test_cmd *cmd,
				struct m0_net_test_cmd *reply)
{
	return -ENOSYS;
}

static struct m0_net_test_service_cmd_handler node_bulk_cmd_handler[] = {
	{
		.ntsch_type    = M0_NET_TEST_CMD_INIT,
		.ntsch_handler = node_bulk_cmd_init,
	},
	{
		.ntsch_type    = M0_NET_TEST_CMD_START,
		.ntsch_handler = node_bulk_cmd_start,
	},
	{
		.ntsch_type    = M0_NET_TEST_CMD_STOP,
		.ntsch_handler = node_bulk_cmd_stop,
	},
	{
		.ntsch_type    = M0_NET_TEST_CMD_STATUS,
		.ntsch_handler = node_bulk_cmd_status,
	},
};

struct m0_net_test_service_ops m0_net_test_node_bulk_ops = {
	.ntso_init	     = node_bulk_init,
	.ntso_fini	     = node_bulk_fini,
	.ntso_step	     = node_bulk_step,
	.ntso_cmd_handler    = node_bulk_cmd_handler,
	.ntso_cmd_handler_nr = ARRAY_SIZE(node_bulk_cmd_handler),
};

/**
   @} end of NetTestBulkNodeInternals group
 */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
