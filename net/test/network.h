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
 * Original author Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 03/22/2012
 */

#ifndef __NET_TEST_NETWORK_H__
#define __NET_TEST_NETWORK_H__

#include "net/net.h"

/**
   @defgroup NetTestNetworkDFS Colibri Network Benchmark Network \
			       Detailed Functional Specification

   @see
   @ref net-test

   FIXME c2_net_test_network_ prefix is too long. rename and align.
   @todo s/uint32_t/size_t/ ?

   @{
 */

enum c2_net_test_network_buf_type {
	C2_NET_TEST_BUF_BULK,	/**< Buffer for the bulk transfers. */
	C2_NET_TEST_BUF_PING,	/**< Buffer for the message transfers. */
};

struct c2_net_test_network_ctx;

/**
   Callback for a network context buffer operations.
   @param ctx Network context.
   @param buf_index Buffer index within network context.
   @param ev Buffer event.
 */
typedef void (*c2_net_test_network_buffer_cb_proc_t)
	(struct c2_net_test_network_ctx	  *ctx,
	 const uint32_t			   buf_index,
	 enum c2_net_queue_type		   q,
	 const struct c2_net_buffer_event *ev);

/** Callbacks for a network context buffers. */
struct c2_net_test_network_buffer_callbacks {
	c2_net_test_network_buffer_cb_proc_t ntnbc_cb[C2_NET_QT_NR];
};

/** Timeouts for each type of transfer machine queue */
struct c2_net_test_network_timeouts {
	c2_time_t ntnt_timeout[C2_NET_QT_NR];
};

/**
   Net-test network context structure.
   Contains transfer machine, tm and buffer callbacks, endpoints,
   ping and bulk message buffers.
 */
struct c2_net_test_network_ctx {
	/** Network domain. */
	struct c2_net_domain			    ntc_dom;
	/** Transfer machine callbacks. */
	struct c2_net_tm_callbacks		    ntc_tm_cb;
	/** Transfer machine. */
	struct c2_net_transfer_mc		    ntc_tm;
	/** Buffer callbacks. */
	struct c2_net_test_network_buffer_callbacks ntc_buf_cb;
	/** Array of message buffers. Used for message send/recv. */
	struct c2_net_buffer			   *ntc_buf_ping;
	/** Number of message buffers. */
	uint32_t				    ntc_buf_ping_nr;
	/** Array of buffers for bulk transfer. */
	struct c2_net_buffer			   *ntc_buf_bulk;
	/** Number of buffers for bulk transfer. */
	uint32_t				    ntc_buf_bulk_nr;
	/**
	   Array of pointers to endpoints.
	   Initially this array have no endpoints, but they can
	   be added to this array sequentually, one by one using
	   c2_net_test_network_ep_add().
	   Endpoints are freed in c2_net_test_network_ctx_fini().
	 */
	struct c2_net_end_point			  **ntc_ep;
	/**
	   Current number of endpoints in ntc_ep array.
	 */
	uint32_t				    ntc_ep_nr;
	/**
	   Maximum number of endponts in ntc_ep array.
	 */
	uint32_t				    ntc_ep_max;
	/**
	   Timeouts for every type of network buffer queue.
	   Used when buffer is added to queue.
	 */
	struct c2_net_test_network_timeouts	    ntc_timeouts;
};

/**
   Initialize net-test network module.
   Calls c2_net_xprt_init(), c2_net_domain_init().
 */
int c2_net_test_network_init(void);

/**
   Finalize net-test network module.
   Calls c2_net_xprt_fini(), c2_net_domain_fini().
 */
void c2_net_test_network_fini(void);

/**
   Initialize c2_net_test_network_ctx structure.
   Allocate ping and bulk buffers.
   @param timeouts Timeouts for each type of tm queue. Can be NULL, in
   this case it is assumed than all timeouts is C2_TIME_NEVER.
   @see c2_net_test_network_ctx
   @pre ctx     != NULL
   @pre tm_addr != NULL
   @pre tm_cb   != NULL
   @pre buf_cb  != NULL
   @post c2_net_test_network_ctx_invariant(ctx)
 */
int c2_net_test_network_ctx_init(struct c2_net_test_network_ctx *ctx,
				 const char *tm_addr,
				 const struct c2_net_tm_callbacks *tm_cb,
				 const struct
				 c2_net_test_network_buffer_callbacks *buf_cb,
				 c2_bcount_t buf_size_ping,
				 uint32_t buf_ping_nr,
				 c2_bcount_t buf_size_bulk,
				 uint32_t buf_bulk_nr,
				 uint32_t ep_max,
				 const struct c2_net_test_network_timeouts
				 *timeouts);
void c2_net_test_network_ctx_fini(struct c2_net_test_network_ctx *ctx);
bool c2_net_test_network_ctx_invariant(struct c2_net_test_network_ctx *ctx);

/**
   Add entry point to c2_net_test_network_ctx structure.
   @return entry point number.
   @return -E2BIG ctx->ntc_ep already contains maximum number of endpoints.
   @return -errno (if failure)

   @see c2_net_test_network_init()
 */
int c2_net_test_network_ep_add(struct c2_net_test_network_ctx *ctx,
			       const char *ep_addr);

/**
   Add message buffer to network messages send queue.
   @param ctx Net-test network context.
   @param buf_ping_index Index of buffer in ctx->ntc_buf_ping array.
   @param ep_index Entry point index in ctx->ntc_ep array. Entry points
	           should be added to this array prior to calling this
		   function using c2_net_test_network_ep_add().
		   Message will be sent to this endpoint.
 */
int c2_net_test_network_msg_send(struct c2_net_test_network_ctx *ctx,
				 uint32_t buf_ping_index,
				 uint32_t ep_index);

/**
   Add message to network messages receive queue.
   @see @ref c2_net_test_network_msg_send()
 */
int c2_net_test_network_msg_recv(struct c2_net_test_network_ctx *ctx,
				 uint32_t buf_ping_index);

/**
   Add bulk buffer to bulk passive/active send/recv queue.
   @param ctx Net-test network context.
   @param buf_bulk_index Index of buffer in ctx->ntc_buf_bulk array.
   @param ep_index Entry point index in ctx->ntc_ep array. Makes sense
		   only for passive send/recv queue.
   @param q Queue type. Should be one of bulk queue types.
 */
int c2_net_test_network_bulk_enqueue(struct c2_net_test_network_ctx *ctx,
				     int32_t buf_bulk_index,
				     int32_t ep_index,
				     enum c2_net_queue_type q);
/**
   Remove network buffer from queue.
   @see c2_net_buffer_del()
   @param ctx Net-test network context.
   @param buf_index Index of buffer in ctx->ntc_buf_bulk or ntc->buf_ping
		    arrays, depending on buf_type parameter.
   @param buf_type Buffer type.
*/
void c2_net_test_network_buffer_dequeue(struct c2_net_test_network_ctx *ctx,
					enum c2_net_test_network_buf_type
					buf_type,
					int32_t buf_index);

/**
   Reset to 0 number of network buffer descriptors in the message buffer.
   @see @ref c2_net_test_network_bd_encode().
 */
void c2_net_test_network_bd_reset(struct c2_net_test_network_ctx *ctx,
				  int32_t buf_ping_index);
/**
   Get the number of network buffer descriptors in the message buffer.
   @see @ref c2_net_test_network_bd_encode().
 */
uint32_t c2_net_test_network_bd_count(struct c2_net_test_network_ctx *ctx,
				      int32_t buf_ping_index);

/**
   Store network buffer descriptor (c2_net_buf_desc) in the message buffer.
   c2_net_buf_desc is serialized and is stored in the message buffer
   one after the other. Buffer length is adjusted on every encoding.
   So, to send some number of network descriptors over the network
   there is a simple steps:
   - @b passive: call c2_net_test_network_bd_reset() to reset to 0 number of
     network descriptors in the ping buffer;
   - @b passive: call c2_net_test_network_bd_encode() as many times as needed
     to serialize bulk buffer descriptors (from buffers, previously added
     to passive bulk queues) to ping buffer;
   - @b passive: send ping buffer to active side;
   - @b active: receive ping buffer;
   - @b active: determine number of network buffer descriptors inside ping
     buffer using c2_net_test_network_bd_count();
   - @b active: reset number of network buffer desriptors in the ping buffer
     using c2_net_test_network_bd_reset();
   - @b active: sequentially call c2_net_test_network_bd_encode() as many times
     as there is network buffer descriptors, encoded in this ping buffer,
     to set appropriate network buffer descriptors for a bulk buffers to
     perform active send/receive.
   @param ctx Net-test network context.
   @param buf_ping_index Index of message buffer in ctx->ntc_buf_ping array.
   @param buf_bulk_index Index of bulk buffer in ctx->ntc_buf_bulk array.
 */
int c2_net_test_network_bd_encode(struct c2_net_test_network_ctx *ctx,
				  int32_t buf_ping_index,
				  int32_t buf_bulk_index);
/**
   Recover a network descriptor from the message buffer.
   @see @ref c2_net_test_network_bd_encode().
 */
int c2_net_test_network_bd_decode(struct c2_net_test_network_ctx *ctx,
				  int32_t buf_ping_index,
				  int32_t buf_bulk_index);

/**
   Accessor to buffers in net-test network context.
 */
struct c2_net_buffer *
c2_net_test_network_buf(struct c2_net_test_network_ctx *ctx,
			enum c2_net_test_network_buf_type buf_type,
			uint32_t buf_index);

/**
   Resize network buffer.
   Calls c2_net_buffer_deregister()/c2_net_buffer_register().
 */
int c2_net_test_network_buf_resize(struct c2_net_test_network_ctx *ctx,
				   enum c2_net_test_network_buf_type buf_type,
				   uint32_t buf_index,
				   c2_bcount_t new_size);

/**
   Fill entire buffer c2_bufvec with char ch.
   Useful for unit tests.
 */
void c2_net_test_network_buf_fill(struct c2_net_test_network_ctx *ctx,
				  enum c2_net_test_network_buf_type buf_type,
				  uint32_t buf_index,
				  uint8_t fill);

/**
   Return c2_net_test_network_timeouts, filled with C2_TIME_NEVER.
   Useful because of C2_TIME_NEVER declared as "extern const".
 */
struct c2_net_test_network_timeouts c2_net_test_network_timeouts_never(void);

/**
   @} end NetTestNetworkDFS
 */

#endif /*  __NET_TEST_NETWORK_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
