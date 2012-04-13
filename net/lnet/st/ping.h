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
#ifndef __COLIBRI_NET_LNET_PING_H__
#define __COLIBRI_NET_LNET_PING_H__

struct c2_nlx_ping_ctx;
struct c2_nlx_ping_ops {
	int (*pf)(const char *format, ...)
		__attribute__ ((format (printf, 1, 2)));
	void (*pqs)(struct c2_nlx_ping_ctx *ctx, bool reset);
};

/**
   Context for a ping client or server.
 */
struct c2_nlx_ping_ctx {
	const struct c2_nlx_ping_ops	     *pc_ops;
	struct c2_net_xprt		     *pc_xprt;
	struct c2_net_domain		      pc_dom;
	const char		             *pc_network; /* "addr@interface" */
	uint32_t                              pc_pid;
	uint32_t			      pc_portal;
	int32_t			              pc_tmid; /* initialized to < 0 */
	const char			     *pc_rnetwork;
	uint32_t                              pc_rpid;
	uint32_t			      pc_rportal;
	int32_t			              pc_rtmid;
	int32_t				      pc_status;
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
	int                                   pc_passive_bulk_timeout;
	int                                   pc_server_bulk_delay;
};

enum {
	PING_CLIENT_PORTAL = 39,
	PING_CLIENT_DYNAMIC_TMID = -1,
	PING_SERVER_PORTAL = 39,
	PING_SERVER_TMID = 12,
};

/* Debug printf macro */
#ifdef __KERNEL__
#define PING_ERR(fmt, ...) printk(KERN_ERR fmt , ## __VA_ARGS__)
#define PRId64 "lld" /* from <inttypes.h> */
#else
#include <stdio.h>
#define PING_ERR(fmt, ...) fprintf(stderr, fmt , ## __VA_ARGS__)
#endif

void c2_nlx_ping_server(struct c2_nlx_ping_ctx *ctx);
void c2_nlx_ping_server_should_stop(struct c2_nlx_ping_ctx *ctx);
int c2_nlx_ping_client_init(struct c2_nlx_ping_ctx *ctx,
			    struct c2_net_end_point **server_ep);
int c2_nlx_ping_client_fini(struct c2_nlx_ping_ctx *ctx,
			    struct c2_net_end_point *server_ep);
int c2_nlx_ping_client_msg_send_recv(struct c2_nlx_ping_ctx *ctx,
				     struct c2_net_end_point *server_ep,
				     const char *data);
int c2_nlx_ping_client_passive_recv(struct c2_nlx_ping_ctx *ctx,
				    struct c2_net_end_point *server_ep);
int c2_nlx_ping_client_passive_send(struct c2_nlx_ping_ctx *ctx,
				    struct c2_net_end_point *server_ep,
				    const char *data);

#endif /* __COLIBRI_NET_LNET_PING_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
