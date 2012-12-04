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
 * Original creation date: 03/22/2012
 */

#pragma once

#ifndef __MERO_NET_TEST_NETWORK_H__
#define __MERO_NET_TEST_NETWORK_H__

#include "net/net.h"

/**
   @defgroup NetTestNetworkDFS Network
   @ingroup NetTestDFS

   @see
   @ref net-test

   @todo m0_net_test_network_ prefix is too long. rename and align.
   @todo s/uint32_t/size_t/

   @{
 */

enum m0_net_test_network_buf_type {
	M0_NET_TEST_BUF_BULK,	/**< Buffer for the bulk transfers. */
	M0_NET_TEST_BUF_PING,	/**< Buffer for the message transfers. */
};

struct m0_net_test_network_ctx;

/**
   Callback for a network context buffer operations.
   @param ctx Network context.
   @param buf_index Buffer index within network context.
   @param ev Buffer event.
 */
typedef void (*m0_net_test_network_buffer_cb_proc_t)
	(struct m0_net_test_network_ctx	  *ctx,
	 const uint32_t			   buf_index,
	 enum m0_net_queue_type		   q,
	 const struct m0_net_buffer_event *ev);

/** Callbacks for a network context buffers. */
struct m0_net_test_network_buffer_callbacks {
	m0_net_test_network_buffer_cb_proc_t ntnbc_cb[M0_NET_QT_NR];
};

/** Timeouts for each type of transfer machine queue */
struct m0_net_test_network_timeouts {
	m0_time_t ntnt_timeout[M0_NET_QT_NR];
};

/**
   Net-test network context structure.
   Contains transfer machine, tm and buffer callbacks, endpoints,
   ping and bulk message buffers.
 */
struct m0_net_test_network_ctx {
	/** Network domain. */
	struct m0_net_domain			   *ntc_dom;
	/** Transfer machine callbacks. */
	struct m0_net_tm_callbacks		    ntc_tm_cb;
	/** Transfer machine. */
	struct m0_net_transfer_mc		   *ntc_tm;
	/** Buffer callbacks. */
	struct m0_net_test_network_buffer_callbacks ntc_buf_cb;
	/** Array of message buffers. Used for message send/recv. */
	struct m0_net_buffer			   *ntc_buf_ping;
	/** Number of message buffers. */
	uint32_t				    ntc_buf_ping_nr;
	/** Array of buffers for bulk transfer. */
	struct m0_net_buffer			   *ntc_buf_bulk;
	/** Number of buffers for bulk transfer. */
	uint32_t				    ntc_buf_bulk_nr;
	/**
	   Array of pointers to endpoints.
	   Initially this array have no endpoints, but they can
	   be added to this array sequentually, one by one using
	   m0_net_test_network_ep_add().
	   Endpoints are freed in m0_net_test_network_ctx_fini().
	 */
	struct m0_net_end_point			  **ntc_ep;
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
	struct m0_net_test_network_timeouts	    ntc_timeouts;
};

/**
   Initialize net-test network module.
   Calls m0_net_xprt_init(), m0_net_domain_init().
 */
int m0_net_test_network_init(void);

/**
   Finalize net-test network module.
   Calls m0_net_xprt_fini(), m0_net_domain_fini().
 */
void m0_net_test_network_fini(void);

/**
   Initialize m0_net_test_network_ctx structure.
   Allocate ping and bulk buffers.
   @note timeouts parameter can be NULL, in this case it is assumed
   that all timeouts is M0_TIME_NEVER.
   @see m0_net_test_network_ctx
   @pre ctx     != NULL
   @pre tm_addr != NULL
   @pre tm_cb   != NULL
   @pre buf_cb  != NULL
   @post m0_net_test_network_ctx_invariant(ctx)
   @return 0 (success)
   @return -ECONNREFUSED m0_net_tm_start() failed.
   @return -errno (failire)
 */
int m0_net_test_network_ctx_init(struct m0_net_test_network_ctx *ctx,
				 const char *tm_addr,
				 const struct m0_net_tm_callbacks *tm_cb,
				 const struct
				 m0_net_test_network_buffer_callbacks *buf_cb,
				 m0_bcount_t buf_size_ping,
				 uint32_t buf_ping_nr,
				 m0_bcount_t buf_size_bulk,
				 uint32_t buf_bulk_nr,
				 uint32_t ep_max,
				 const struct m0_net_test_network_timeouts
				 *timeouts);
void m0_net_test_network_ctx_fini(struct m0_net_test_network_ctx *ctx);
bool m0_net_test_network_ctx_invariant(struct m0_net_test_network_ctx *ctx);

/**
   Add entry point to m0_net_test_network_ctx structure.
   @return entry point number.
   @return -E2BIG ctx->ntc_ep already contains maximum number of endpoints.
   @return -errno (if failure)

   @see m0_net_test_network_init()
 */
int m0_net_test_network_ep_add(struct m0_net_test_network_ctx *ctx,
			       const char *ep_addr);

/**
   Add message buffer to network messages send queue.
   @param ctx Net-test network context.
   @param buf_ping_index Index of buffer in ctx->ntc_buf_ping array.
   @param ep_index Entry point index in ctx->ntc_ep array. Entry points
	           should be added to this array prior to calling this
		   function using m0_net_test_network_ep_add().
		   Message will be sent to this endpoint.
 */
int m0_net_test_network_msg_send(struct m0_net_test_network_ctx *ctx,
				 uint32_t buf_ping_index,
				 uint32_t ep_index);

/**
   Add message buffer to network messages send queue.
   Use struct m0_net_end_point instead of endpoint index.
   @see m0_net_test_network_msg_send()
 */
int m0_net_test_network_msg_send_ep(struct m0_net_test_network_ctx *ctx,
				    uint32_t buf_ping_index,
				    struct m0_net_end_point *ep);

/**
   Add message to network messages receive queue.
   @see @ref m0_net_test_network_msg_send()
 */
int m0_net_test_network_msg_recv(struct m0_net_test_network_ctx *ctx,
				 uint32_t buf_ping_index);

/**
   Add bulk buffer to bulk passive/active send/recv queue.
   @param ctx Net-test network context.
   @param buf_bulk_index Index of buffer in ctx->ntc_buf_bulk array.
   @param ep_index Entry point index in ctx->ntc_ep array. Makes sense
		   only for passive send/recv queue.
   @param q Queue type. Should be one of bulk queue types.
 */
int m0_net_test_network_bulk_enqueue(struct m0_net_test_network_ctx *ctx,
				     int32_t buf_bulk_index,
				     int32_t ep_index,
				     enum m0_net_queue_type q);
/**
   Remove network buffer from queue.
   @see m0_net_buffer_del()
   @param ctx Net-test network context.
   @param buf_index Index of buffer in ctx->ntc_buf_bulk or ntc->buf_ping
		    arrays, depending on buf_type parameter.
   @param buf_type Buffer type.
*/
void m0_net_test_network_buffer_dequeue(struct m0_net_test_network_ctx *ctx,
					enum m0_net_test_network_buf_type
					buf_type,
					int32_t buf_index);

/**
   Reset to 0 number of network buffer descriptors in the message buffer.
   @see @ref m0_net_test_network_bd_encode().
 */
void m0_net_test_network_bd_reset(struct m0_net_test_network_ctx *ctx,
				  int32_t buf_ping_index);
/**
   Get the number of network buffer descriptors in the message buffer.
   @see @ref m0_net_test_network_bd_encode().
 */
uint32_t m0_net_test_network_bd_count(struct m0_net_test_network_ctx *ctx,
				      int32_t buf_ping_index);

/**
   Store network buffer descriptor (m0_net_buf_desc) in the message buffer.
   m0_net_buf_desc is serialized and is stored in the message buffer
   one after the other. Buffer length is adjusted on every encoding.
   So, to send some number of network descriptors over the network
   there is a simple steps:
   - @b passive: call m0_net_test_network_bd_reset() to reset to 0 number of
     network descriptors in the ping buffer;
   - @b passive: call m0_net_test_network_bd_encode() as many times as needed
     to serialize bulk buffer descriptors (from buffers, previously added
     to passive bulk queues) to ping buffer;
   - @b passive: send ping buffer to active side;
   - @b active: receive ping buffer;
   - @b active: determine number of network buffer descriptors inside ping
     buffer using m0_net_test_network_bd_count();
   - @b active: reset number of network buffer desriptors in the ping buffer
     using m0_net_test_network_bd_reset();
   - @b active: sequentially call m0_net_test_network_bd_encode() as many times
     as there is network buffer descriptors, encoded in this ping buffer,
     to set appropriate network buffer descriptors for a bulk buffers to
     perform active send/receive.
   @param ctx Net-test network context.
   @param buf_ping_index Index of message buffer in ctx->ntc_buf_ping array.
   @param buf_bulk_index Index of bulk buffer in ctx->ntc_buf_bulk array.
 */
int m0_net_test_network_bd_encode(struct m0_net_test_network_ctx *ctx,
				  int32_t buf_ping_index,
				  int32_t buf_bulk_index);
/**
   Recover a network descriptor from the message buffer.
   @see @ref m0_net_test_network_bd_encode().
 */
int m0_net_test_network_bd_decode(struct m0_net_test_network_ctx *ctx,
				  int32_t buf_ping_index,
				  int32_t buf_bulk_index);

/**
   Accessor to buffers in net-test network context.
 */
struct m0_net_buffer *
m0_net_test_network_buf(struct m0_net_test_network_ctx *ctx,
			enum m0_net_test_network_buf_type buf_type,
			uint32_t buf_index);

/**
   Resize network buffer.
   Calls m0_net_buffer_deregister()/m0_net_buffer_register().
 */
int m0_net_test_network_buf_resize(struct m0_net_test_network_ctx *ctx,
				   enum m0_net_test_network_buf_type buf_type,
				   uint32_t buf_index,
				   m0_bcount_t new_size);

/**
   Fill entire buffer m0_bufvec with char ch.
   Useful for unit tests.
 */
void m0_net_test_network_buf_fill(struct m0_net_test_network_ctx *ctx,
				  enum m0_net_test_network_buf_type buf_type,
				  uint32_t buf_index,
				  uint8_t fill);

/** Accessor for endpoints by index. */
struct m0_net_end_point *
m0_net_test_network_ep(struct m0_net_test_network_ctx *ctx, size_t ep_index);

/**
   Search for ep_addr in m0_net_test_network_ctx.ntc_ep
   This function have time complexity
   of O(number of endpoints in the network context).
   @return >= 0 endpoint index
   @return -1 endpoint not found
 */
ssize_t m0_net_test_network_ep_search(struct m0_net_test_network_ctx *ctx,
				      const char *ep_addr);

/**
   Return m0_net_test_network_timeouts, filled with M0_TIME_NEVER.
   Useful because M0_TIME_NEVER is declared as "extern const".
 */
struct m0_net_test_network_timeouts m0_net_test_network_timeouts_never(void);

/**
   @} end of NetTestNetworkDFS group
 */

#endif /*  __MERO_NET_TEST_NETWORK_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
