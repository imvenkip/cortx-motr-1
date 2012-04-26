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
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>,
 *                  Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 04/12/2011
 * Adapted for LNet: 04/11/2012
 */
#ifndef __COLIBRI_NET_LNET_PING_H__
#define __COLIBRI_NET_LNET_PING_H__

struct nlx_ping_ctx;
struct nlx_ping_ops {
	int (*pf)(const char *format, ...)
		__attribute__ ((format (printf, 1, 2)));
	void (*pqs)(struct nlx_ping_ctx *ctx, bool reset);
};

/**
   Context for a ping client or server.
 */
struct nlx_ping_ctx {
	const struct nlx_ping_ops	     *pc_ops;
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
        uint32_t                              pc_seg_shift;
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
	int                                   pc_bulk_timeout;
	int                                   pc_msg_timeout;
	int                                   pc_server_bulk_delay;
	int                                   pc_dom_debug;
	int                                   pc_tm_debug;
	bool                                  pc_ready;
	char * const *                        pc_interfaces;
	bool                                  pc_sync_events;
	struct c2_chan                        pc_work_chan;
	struct c2_clink                       pc_work_clink;
	bool                                  pc_work_signal;
	unsigned                              pc_work_signal_count;
	struct c2_chan                        pc_net_chan;
	struct c2_clink                       pc_net_clink;
	bool                                  pc_net_signal;
	unsigned                              pc_net_signal_count;
};

struct nlx_ping_client_params {
	const struct nlx_ping_ops *ops;
	bool verbose;
	int loops;
	unsigned int nr_bufs;
	int client_id;
	int passive_size;
	int bulk_timeout;
	int msg_timeout;
	const char *client_network;
	uint32_t    client_pid;
	uint32_t    client_portal;
	int32_t	    client_tmid;
	const char *server_network;
	uint32_t    server_pid;
	uint32_t    server_portal;
	int32_t	    server_tmid;
	int         debug;
};

enum {
	PING_CLIENT_PORTAL = 39,
	PING_CLIENT_DYNAMIC_TMID = -1,
	PING_SERVER_PORTAL = 39,
	PING_SERVER_TMID = 12,

	PING_DEF_BUFS = 20,
	PING_MIN_BUFS = 4,
	PING_DEF_CLIENT_THREADS = 1,
	PING_MAX_CLIENT_THREADS = 32,
	PING_DEF_LOOPS = 1,

	PING_DEF_MSG_TIMEOUT = 5,
	PING_DEF_BULK_TIMEOUT = 10,

	PING_CLIENT_SEGMENTS = 8,
	PING_CLIENT_SEGMENT_SIZE = 4096,
	PING_CLIENT_SEGMENT_SHIFT = 12,
	PING_SERVER_SEGMENTS = 8,
	PING_SERVER_SEGMENT_SIZE = 4096,
	PING_SERVER_SEGMENT_SHIFT = 12,
	/* leave some room for overhead */
	PING_MAX_PASSIVE_SIZE =
		PING_SERVER_SEGMENTS * PING_SERVER_SEGMENT_SIZE - 1024,

	ONE_MILLION = 1000000ULL,
	SEC_PER_HR = 60 * 60,
	SEC_PER_MIN = 60,
};

/* Debug printf macro */
#ifdef __KERNEL__
#define PING_ERR(fmt, ...) printk(KERN_ERR fmt , ## __VA_ARGS__)
#define PRId64 "lld" /* from <inttypes.h> */
#else
#include <stdio.h>
#define PING_ERR(fmt, ...) fprintf(stderr, fmt , ## __VA_ARGS__)
#endif

void nlx_ping_server_spawn(struct c2_thread *server_thread,
			   struct nlx_ping_ctx *sctx);
void nlx_ping_server_should_stop(struct nlx_ping_ctx *ctx);
void nlx_ping_client(struct nlx_ping_client_params *params);

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
