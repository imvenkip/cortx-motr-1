/* -*- C -*- */
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
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>,
 *                  Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 04/12/2011
 */
#ifndef __COLIBRI_NET_BULK_MEM_PING_H__
#define __COLIBRI_NET_BULK_MEM_PING_H__

struct ping_ctx;
struct ping_ops {
	int (*pf)(const char *format, ...)
		__attribute__ ((format (printf, 1, 2)));
	void (*pqs)(struct ping_ctx *ctx, bool reset);
};

/**
   Context for a ping client or server.
 */
struct ping_ctx {
	const struct ping_ops		     *pc_ops;
	struct c2_net_xprt		     *pc_xprt;
	struct c2_net_domain		      pc_dom;
	const char		             *pc_hostname; /* dotted decimal */
	short				      pc_port;
	uint32_t			      pc_id;
	int32_t				      pc_status;
	const char			     *pc_rhostname; /* dotted decimal */
	short				      pc_rport;
	uint32_t			      pc_rid;
	uint32_t		              pc_nr_bufs;
	uint32_t		              pc_segments;
	uint32_t		              pc_seg_size;
	int32_t				      pc_passive_size;
	struct c2_net_buffer		     *pc_nbs;
	const struct c2_net_buffer_callbacks *pc_buf_callbacks;
	struct c2_bitmap		      pc_nbbm;
	struct c2_net_transfer_mc	      pc_tm;
	struct c2_mutex			      pc_mutex;
	struct c2_cond			      pc_cond;
	struct c2_list			      pc_work_queue;
	const char		             *pc_ident;
	const char		             *pc_compare_buf;
	int                                   pc_sunrpc_ep_delay;
	int                                   pc_passive_bulk_timeout;
	int                                   pc_server_bulk_delay;
	int                                   pc_sunrpc_skulker_period;
};

enum {
	//PING_PORT1 = 12345,
	PING_PORT1 = C2_NET_SUNRPC_PORT,
	PING_PORT2 = 27183,
	PART3_SERVER_ID = 141421,
};

/* Debug printf macro */
#ifdef __KERNEL__
#define PING_ERR(fmt, ...) printk(KERN_ERR fmt , ## __VA_ARGS__)
#define PRId64 "lld" /* from <inttypes.h> */
#else
#include <stdio.h>
#define PING_ERR(fmt, ...) fprintf(stderr, fmt , ## __VA_ARGS__)
#endif

void ping_server(struct ping_ctx *ctx);
void ping_server_should_stop(struct ping_ctx *ctx);
int ping_client_init(struct ping_ctx *ctx, struct c2_net_end_point **server_ep);
int ping_client_fini(struct ping_ctx *ctx, struct c2_net_end_point *server_ep);
int ping_client_msg_send_recv(struct ping_ctx *ctx,
			      struct c2_net_end_point *server_ep,
			      const char *data);
int ping_client_passive_recv(struct ping_ctx *ctx,
			     struct c2_net_end_point *server_ep);
int ping_client_passive_send(struct ping_ctx *ctx,
			     struct c2_net_end_point *server_ep,
			     const char *data);

#endif /* __COLIBRI_NET_BULK_MEM_PING_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
