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

   @{
*/

/**
   Net test network context structure.
   Contains transfer machine, tm and buffer callbacks, endpoints,
   ping and bulk message buffers.
 */
struct c2_net_test_ctx {
	struct c2_net_transfer_mc	ntc_tm;
	struct c2_net_tm_callbacks	ntc_tm_cb;
	struct c2_net_buffer_callbacks	ntc_buf_cb;
	struct c2_net_buffer	       *ntc_buf_ping;
	int				ntc_buf_ping_nr;
	struct c2_net_buffer	       *ntc_buf_bulk;
	int				ntc_buf_bulk_nr;
	struct c2_end_point	       *ntc_ep;
	int				ntc_ep_nr;
	int				ntc_ep_max;
};

/**
   Initialize c2_net structures.
   Call c2_net_xprt_init(), c2_net_domain_init().
 */
int c2_net_test_net_init(void);
void c2_net_test_net_fini(void);

/**
   Initialize c2_net_test_ctx structure.
   Allocate ping and bulk buffers.
 */
int c2_net_test_net_ctx_init(struct c2_net_test_ctx *ctx,
		const char *tm_addr,
		const struct c2_net_tm_callbacks *tm_cb,
		const struct c2_net_buffer_callbacks *buf_cb,
		const int buf_ping_size,
		const int buf_ping_nr,
		const int buf_bulk_size,
		const int buf_bulk_nr,
		const int ep_max);
void c2_net_test_net_ctx_fini(struct c2_net_test_ctx *ctx);

/**
   Add entry point to c2_net_test_ctx structure.
   @return entry point number.
 */
int c2_net_test_net_ep_add(struct c2_net_test_ctx *ctx,
		const char *ep_addr);

/**
   Send/receive ping messages.
 */
int c2_net_test_net_msg_send(struct c2_net_test_ctx *ctx,
		int buf_ping_index, int ep_index);
int c2_net_test_net_msg_recv(struct c2_net_test_ctx *ctx,
		int buf_ping_index, int ep_index);

/**
   Send/receive bulk messages.
 */
int c2_net_test_net_bulk_send_passive(struct c2_net_test_ctx *ctx,
		int buf_bulk_index, int ep_index);
int c2_net_test_net_bulk_recv_passive(struct c2_net_test_ctx *ctx,
		int buf_bulk_index, int ep_index);
int c2_net_test_net_bulk_send_active(struct c2_net_test_ctx *ctx,
		int buf_bulk_index, int buf_ping_index);
int c2_net_test_net_bulk_recv_active(struct c2_net_test_ctx *ctx,
		int buf_bulk_index, int buf_ping_index);

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
