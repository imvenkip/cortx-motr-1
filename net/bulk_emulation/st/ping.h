/* -*- C -*- */
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
	const char		             *pc_hostname;
	short				      pc_port;
	uint32_t			      pc_id;
	int32_t				      pc_status;
	const char			     *pc_rhostname;
	short				      pc_rport;
	uint32_t			      pc_rid;
	uint32_t		              pc_nr_bufs;
	uint32_t		              pc_segments;
	uint32_t		              pc_seg_size;
	int32_t				      pc_passive_size;
	struct c2_net_buffer		     *pc_nbs;
	const struct c2_net_buffer_callbacks *pc_buf_callbacks;
	struct c2_bitmap		      pc_nbbm;
	struct c2_net_end_point		     *pc_ep;
	struct c2_net_transfer_mc	      pc_tm;
	struct c2_mutex			      pc_mutex;
	struct c2_cond			      pc_cond;
	struct c2_list			      pc_work_queue;
	const char		             *pc_ident;
	const char		             *pc_compare_buf;
};

enum {
	PING_PORT1 = 31416,
	PING_PORT2 = 27183,
	PART3_SERVER_ID = 141421,
};

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
