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
 * Original creation date: 05/05/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/* XXX remove */
#ifndef __KERNEL__
#include <stdio.h>		/* printf */
#endif

#include "lib/misc.h"		/* C2_SET0 */
#include "lib/memory.h"		/* c2_alloc */
#include "lib/errno.h"		/* ENOMEM */

#include "net/test/commands.h"

#ifndef __KERNEL__
#define LOGD(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOGD(format, ...) do {} while (0)
#endif

/**
   @defgroup NetTestCommandsInternals Colibri Network Bencmark \
				    Commands Internals

   @see
   @ref net-test

   @{
 */

static void commands_tm_event_cb(const struct c2_net_tm_event *ev) {
}

static const struct c2_net_tm_callbacks c2_net_test_commands_tm_cb = {
	.ntc_event_cb = commands_tm_event_cb
};

static void commands_cb_msg_recv(struct c2_net_test_network_ctx *ctx,
		const uint32_t buf_index,
		const struct c2_net_buffer_event *ev) {
	/* XXX */
}

static void commands_cb_msg_send(struct c2_net_test_network_ctx *ctx,
		const uint32_t buf_index,
		const struct c2_net_buffer_event *ev) {
	/* XXX */
}

static void commands_cb_impossible(struct c2_net_test_network_ctx *ctx,
		const uint32_t buf_index,
		const struct c2_net_buffer_event *ev) {

	C2_IMPOSSIBLE("commands bulk buffer callback is impossible");
}

static const struct c2_net_test_network_buffer_callbacks
		c2_net_test_commands_buffer_cb = {
	.ntnbc_cb = {
		[C2_NET_QT_MSG_RECV]		= commands_cb_msg_recv,
		[C2_NET_QT_MSG_SEND]		= commands_cb_msg_send,
		[C2_NET_QT_PASSIVE_BULK_RECV]	= commands_cb_impossible,
		[C2_NET_QT_PASSIVE_BULK_SEND]	= commands_cb_impossible,
		[C2_NET_QT_ACTIVE_BULK_RECV]	= commands_cb_impossible,
		[C2_NET_QT_ACTIVE_BULK_SEND]	= commands_cb_impossible,
	}
};

int c2_net_test_command_init(struct c2_net_test_command_ctx *ctx,
		char *cmd_ep,
		c2_time_t timeout_send,
		c2_time_t timeout_wait,
		struct c2_net_test_slist ep_list) {
	int rc;
	int i;
	struct c2_net_test_network_timeouts timeouts;

	C2_PRE(ctx != NULL);
	C2_PRE(ep_list.ntsl_nr > 0);
	C2_SET0(ctx);

	timeouts = c2_net_test_network_timeouts_never();
	timeouts.ntnt_timeout[C2_NET_QT_MSG_SEND] = timeout_send;
	timeouts.ntnt_timeout[C2_NET_QT_MSG_RECV] = timeout_wait;

	ctx->ntcc_cmd_nr = ep_list.ntsl_nr;
	C2_ALLOC_ARR(ctx->ntcc_cmd, ctx->ntcc_cmd_nr);
	if (ctx->ntcc_cmd == NULL)
		return -ENOMEM;

	rc = c2_net_test_network_ctx_init(&ctx->ntcc_net, cmd_ep,
			&c2_net_test_commands_tm_cb,
			&c2_net_test_commands_buffer_cb,
			1 /* XXX measure */, ctx->ntcc_cmd_nr,
			0, 0,
			ep_list.ntsl_nr,
			&timeouts);
	if (rc != 0)
		goto free_cmd;

	for (i = 0; i < ep_list.ntsl_nr; ++i) {
		rc = c2_net_test_network_ep_add(&ctx->ntcc_net,
				ep_list.ntsl_list[i]);
		if (rc < 0)
			goto free_net;
	}

	C2_POST(c2_net_test_command_invariant(ctx));
	rc = 0;
	goto success;

    free_net:
	c2_net_test_network_ctx_fini(&ctx->ntcc_net);
    free_cmd:
	c2_free(ctx->ntcc_cmd);
    success:
	return rc;
}

void c2_net_test_command_fini(struct c2_net_test_command_ctx *ctx) {
	C2_PRE(ctx != NULL);
	C2_PRE(c2_net_test_command_invariant(ctx));

	c2_net_test_network_ctx_fini(&ctx->ntcc_net);
	c2_free(ctx->ntcc_cmd);
}

int c2_net_test_command_send_single(struct c2_net_test_command_ctx *ctx,
		struct c2_net_test_command *cmd) {
	return -1;
}

int c2_net_test_command_send(struct c2_net_test_command_ctx *ctx) {
	return -1;
}

int c2_net_test_command_wait(struct c2_net_test_command_ctx *ctx) {
	return -1;
}

struct c2_net_test_command *c2_net_test_command_cmd(
		struct c2_net_test_command_ctx *ctx, uint32_t index) {
	C2_PRE(ctx != NULL);
	C2_PRE(index < ctx->ntcc_cmd_nr);

	return &ctx->ntcc_cmd[index];
}

bool c2_net_test_command_invariant(struct c2_net_test_command_ctx *ctx) {

	if (ctx == NULL)
		return false;
	if (ctx->ntcc_cmd_nr != ctx->ntcc_net.ntc_ep_nr)
		return false;
	if (ctx->ntcc_cmd_nr != ctx->ntcc_net.ntc_buf_ping_nr)
		return false;
	if (ctx->ntcc_net.ntc_buf_bulk_nr != 0)
		return false;
	return true;
}

int c2_net_test_slist_init(struct c2_net_test_slist *slist,
		char *str, char delim) {
	char  *str1;
	size_t len;
	size_t i = 0;

	C2_SET0(slist);

	slist->ntsl_nr = 0;
	if (str == NULL)
		return 0;
	len = strlen(str);
	if (len == 0)
		return 0;

	str1 = str;
	while (*str1)
		slist->ntsl_nr += *str1++ == delim;
	slist->ntsl_nr++;

	C2_ALLOC_ARR(slist->ntsl_str, len + 1);
	if (slist->ntsl_str == NULL)
		return -ENOMEM;
	C2_ALLOC_ARR(slist->ntsl_list, slist->ntsl_nr);
	if (slist->ntsl_list == NULL) {
		c2_free(slist->ntsl_str);
		return -ENOMEM;
	}

	strncpy(slist->ntsl_str, str, len + 1);
	str = slist->ntsl_str;
	slist->ntsl_list[i++] = str;
	for (; *str; str++)
		if (*str == delim) {
			*str = '\0';
			slist->ntsl_list[i++] = str + 1;
		}
	return 0;
}

void c2_net_test_slist_fini(struct c2_net_test_slist *slist) {
	C2_PRE(slist != NULL);

	if (slist->ntsl_nr > 0) {
		c2_free(slist->ntsl_list);
		c2_free(slist->ntsl_str);
	}
}

/**
   @} end NetTestCommandsInternals
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
