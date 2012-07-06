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
 * Original creation date: 06/06/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "lib/ut.h"		/* C2_UT_ASSERT */
#include "lib/misc.h"		/* C2_SET0 */
#include "lib/semaphore.h"	/* c2_semaphore */
#include "lib/memory.h"		/* c2_alloc */
#include "net/lnet/lnet.h"	/* c2_net_lnet_ifaces_get */

#include "net/test/commands.h"

/* NTC_ == NET_TEST_COMMANDS_ */
enum {
	NTC_PORT	      = 30,
	NTC_SVC_ID_CONSOLE    = 3000,
	NTC_SVC_ID_NODE	      = 3001,
	NTC_MULTIPLE_COMMANDS = 64,
	NTC_ADDR_LEN_MAX      = 0x100,
	NTC_TIMEOUT_MS	      = 1000,
};

static const char   NTC_ADDR[]	   = "0@lo:12345:%d:%d";
static const size_t NTC_ADDR_LEN   = ARRAY_SIZE(NTC_ADDR);
static const char   NTC_DELIM      = ',';

struct net_test_cmd_node {
	struct c2_thread	   ntcn_thread;
	struct c2_net_test_cmd_ctx ntcn_ctx;
	/** index (range: [0..nr)) in nodes list */
	int			   ntcn_index;
	/** used for barriers with the main thread */
	struct c2_semaphore	   ntcn_signal;
	struct c2_semaphore	   ntcn_wait;
	/** number of failures */
	unsigned long		   ntcn_failures;
	/** flag: barriers are disabled for this node */
	bool			   ntcn_barriers_disabled;
	/** used when checking send/recv */
	bool			   ntcn_flag;
};

static char addr_console[NTC_ADDR_LEN_MAX];
static char addr_node[NTC_ADDR_LEN_MAX * NTC_MULTIPLE_COMMANDS];

static struct c2_net_test_slist   slist_node;
static struct c2_net_test_slist   slist_console;
static struct net_test_cmd_node	 *node;
static struct c2_net_test_cmd_ctx console;

static c2_time_t timeout_get(void)
{
	c2_time_t timeout;

	return c2_time_set(&timeout, NTC_TIMEOUT_MS / 1000,
		           (NTC_TIMEOUT_MS % 1000) * 1000000);
}

static c2_time_t timeout_get_abs(void)
{
	return c2_time_add(c2_time_now(), timeout_get());
}

static int make_addr(char *s, size_t s_len, int port, int svc_id,
		     bool add_comma)
{
	int rc = snprintf(s, s_len, NTC_ADDR, port, svc_id);

	C2_ASSERT(NTC_ADDR_LEN <= NTC_ADDR_LEN_MAX);
	C2_ASSERT(rc > 0);

	if (add_comma) {
		s[rc++] = NTC_DELIM;
		s[rc] = '\0';
	}
	return rc;
}

/** Fill addr_console and addr_node strings. */
static void fill_addr(uint32_t nr)
{
	char    *pos = addr_node;
	uint32_t i;

	/* console */
	make_addr(addr_console, NTC_ADDR_LEN_MAX, NTC_PORT,
		  NTC_SVC_ID_CONSOLE, false);
	/* nodes */
	for (i = 0; i < nr; ++i)
		pos += make_addr(pos, NTC_ADDR_LEN_MAX, NTC_PORT,
				 NTC_SVC_ID_NODE + i, i != nr - 1);
}

/**
   C2_UT_ASSERT() isn't thread-safe, so node->ntcn_failures is used for
   assertion failure tracking.
   Assuming that C2_UT_ASSERT() will abort program, otherwise
   will be a deadlock in the barrier_with_main()/barrier_with_nodes().
   Called from the node threads.
   @see command_ut_check(), commands_node_thread(), net_test_command_ut().
 */
static bool commands_ut_assert(struct net_test_cmd_node *node, bool value)
{
	node->ntcn_failures += !value;
	return value;
}

/** Called from the main thread */
static void barrier_init(struct net_test_cmd_node *node)
{
	int rc = c2_semaphore_init(&node->ntcn_signal, 0);
	C2_UT_ASSERT(rc == 0);
	rc = c2_semaphore_init(&node->ntcn_wait, 0);
	C2_UT_ASSERT(rc == 0);
	node->ntcn_barriers_disabled = false;
}

/**
   Called from the main thread.
   @see net_test_command_ut().
 */
static void barrier_fini(struct net_test_cmd_node *node)
{
	c2_semaphore_fini(&node->ntcn_signal);
	c2_semaphore_fini(&node->ntcn_wait);
}

/**
   Called from the node threads.
   @see commands_node_thread().
 */
static void barrier_with_main(struct net_test_cmd_node *node)
{
	if (!node->ntcn_barriers_disabled) {
		c2_semaphore_up(&node->ntcn_signal);
		c2_semaphore_down(&node->ntcn_wait);
	}
}

/**
   Called from the main thread.
   Also checks for UT failures.
   @see net_test_command_ut().
 */
static void barrier_with_nodes(void)
{
	int i;

	for (i = 0; i < slist_node.ntsl_nr; ++i)
		if (!node[i].ntcn_barriers_disabled)
			c2_semaphore_down(&node[i].ntcn_signal);

	for (i = 0; i < slist_node.ntsl_nr; ++i)
		C2_UT_ASSERT(node[i].ntcn_failures == 0);

	for (i = 0; i < slist_node.ntsl_nr; ++i)
		if (!node[i].ntcn_barriers_disabled)
			c2_semaphore_up(&node[i].ntcn_wait);
}

/**
   Called from the node threads.
 */
static void barrier_disable(struct net_test_cmd_node *node)
{
	node->ntcn_barriers_disabled = true;
	c2_semaphore_up(&node->ntcn_signal);
}

static void flags_reset(size_t nr)
{
	size_t i;

	for (i = 0; i < nr; ++i)
		node[i].ntcn_flag = false;
}

static void flag_set(int index)
{
	node[index].ntcn_flag = true;
}

static bool is_flags_set(size_t nr, bool set)
{
	size_t i;

	for (i = 0; i < nr; ++i)
		if (node[i].ntcn_flag == !set)
			return false;
	return true;
}

static bool is_flags_set_odd(size_t nr)
{
	size_t i;

	for (i = 0; i < nr; ++i)
		if (node[i].ntcn_flag == (i + 1) % 2)
			return false;
	return true;
}

static void commands_ut_send(struct net_test_cmd_node *node,
			     struct c2_net_test_cmd_ctx *ctx)
{
	struct c2_net_test_cmd cmd;
	int		       rc;

	C2_SET0(&cmd);
	cmd.ntc_type = C2_NET_TEST_CMD_STOP_ACK;
	cmd.ntc_ack.ntca_errno = -node->ntcn_index;
	cmd.ntc_ep_index = 0;
	rc = c2_net_test_commands_send(ctx, &cmd);
	commands_ut_assert(node, rc == 0);
	c2_net_test_commands_send_wait_all(ctx);
}

static void commands_ut_recv(struct net_test_cmd_node *node,
			     struct c2_net_test_cmd_ctx *ctx,
			     c2_time_t deadline)
{
	struct c2_net_test_cmd cmd;
	int		       rc;

	C2_SET0(&cmd);
	rc = c2_net_test_commands_recv(ctx, &cmd, deadline);
	commands_ut_assert(node, rc == 0);
	rc = c2_net_test_commands_recv_enqueue(ctx, cmd.ntc_buf_index);
	commands_ut_assert(node, rc == 0);
	commands_ut_assert(node, cmd.ntc_type == C2_NET_TEST_CMD_STOP);
	commands_ut_assert(node, cmd.ntc_stop.ntcs_cancel);
	commands_ut_assert(node, cmd.ntc_ep_index == 0);
	flag_set(node->ntcn_index);
}

static void commands_node_thread(struct net_test_cmd_node *node)
{
	struct c2_net_test_cmd_ctx *ctx;
	int			    rc;

	if (node == NULL)
		return;
	ctx = &node->ntcn_ctx;

	node->ntcn_failures = 0;
	rc = c2_net_test_commands_init(ctx,
				       slist_node.ntsl_list[node->ntcn_index],
				       timeout_get(), NULL, &slist_console);
	if (!commands_ut_assert(node, rc == 0))
		return barrier_disable(node);

	barrier_with_main(node);	/* barrier #0 */
	commands_ut_recv(node, ctx, C2_TIME_NEVER);	/* test #1 */
	barrier_with_main(node);	/* barrier #1.0 */
	/* main thread will check flags here */
	barrier_with_main(node);	/* barrier #1.1 */
	commands_ut_send(node, ctx);			/* test #2 */
	barrier_with_main(node);	/* barrier #2 */
	if (node->ntcn_index % 2 != 0)			/* test #3 */
		commands_ut_send(node, ctx);
	barrier_with_main(node);	/* barrier #3 */
	if (node->ntcn_index % 2 != 0)			/* test #4 */
		commands_ut_recv(node, ctx, timeout_get_abs());
	barrier_with_main(node);	/* barrier #4.0 */
	/* main thread will check flags here */
	barrier_with_main(node);	/* barrier #4.1 */
	commands_ut_send(node, ctx);			/* test #5 */
	commands_ut_send(node, ctx);
	barrier_with_main(node);	/* barrier #5.0 */
	/* main thread will start receiving here */
	barrier_with_main(node);	/* barrier #5.1 */
	c2_net_test_commands_fini(&node->ntcn_ctx);
}

static void commands_ut_send_all(size_t nr)
{
	struct c2_net_test_cmd cmd;
	int		       i;
	int		       rc;

	C2_SET0(&cmd);
	cmd.ntc_type = C2_NET_TEST_CMD_STOP;
	cmd.ntc_stop.ntcs_cancel = true;
	for (i = 0; i < nr; ++i) {
		cmd.ntc_ep_index = i;
		rc = c2_net_test_commands_send(&console, &cmd);
		C2_UT_ASSERT(rc == 0);
	}
}

static void commands_ut_recv_all(size_t nr, c2_time_t deadline)
{
	struct c2_net_test_cmd cmd;
	int		       i;
	int		       rc;

	for (i = 0; i < nr; ++i) {
		rc = c2_net_test_commands_recv(&console, &cmd, deadline);
		if (rc == -ETIMEDOUT)
			break;
		C2_UT_ASSERT(rc == 0);
		rc = c2_net_test_commands_recv_enqueue(&console,
						       cmd.ntc_buf_index);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(cmd.ntc_type == C2_NET_TEST_CMD_STOP_ACK);
		C2_UT_ASSERT(cmd.ntc_ack.ntca_errno == -cmd.ntc_ep_index);
		flag_set(cmd.ntc_ep_index);
	}
}

static void net_test_command_ut(size_t nr)
{
	size_t i;
	int    rc;
	bool   rc_bool;

	C2_UT_ASSERT(nr > 0);

	/* prepare addresses */
	fill_addr(nr);
	rc = c2_net_test_slist_init(&slist_node, addr_node, NTC_DELIM);
	C2_UT_ASSERT(rc == 0);
	rc_bool = c2_net_test_slist_unique(&slist_node);
	C2_UT_ASSERT(rc_bool);
	rc = c2_net_test_slist_init(&slist_console, addr_console, NTC_DELIM);
	C2_UT_ASSERT(rc == 0);
	rc_bool = c2_net_test_slist_unique(&slist_console);
	C2_UT_ASSERT(rc_bool);
	/* init console */
	rc = c2_net_test_commands_init(&console, addr_console, timeout_get(),
				       /** @todo set callback */
				       NULL, &slist_node);
	C2_UT_ASSERT(rc == 0);
	/* alloc nodes */
	C2_ALLOC_ARR(node, nr);
	C2_UT_ASSERT(node != NULL);

	/*
	   start thread for every node because:
	   - some of c2_net_test_commands_*() functions have blocking interface;
	   - c2_net transfer machines parallel initialization is much faster
	     then serial.
	 */
	for (i = 0; i < nr; ++i) {
		barrier_init(&node[i]);
		node[i].ntcn_index = i;
		rc = C2_THREAD_INIT(&node[i].ntcn_thread,
				    struct net_test_cmd_node *,
				    NULL,
				    &commands_node_thread,
				    &node[i],
				    "node_thread_#%d",
				    (int) i);
		C2_UT_ASSERT(rc == 0);
	}

	barrier_with_nodes();				/* barrier #0 */
	/*
	   Test #1: console sends command to every node.
	 */
	flags_reset(nr);
	commands_ut_send_all(nr);
	c2_net_test_commands_send_wait_all(&console);
	barrier_with_nodes();				/* barrier #1.0 */
	C2_ASSERT(is_flags_set(nr, true));
	barrier_with_nodes();				/* barrier #1.1 */
	/*
	   Test #2: every node sends command to console.
	 */
	flags_reset(nr);
	commands_ut_recv_all(nr, C2_TIME_NEVER);
	C2_UT_ASSERT(is_flags_set(nr, true));
	barrier_with_nodes();				/* barrier #2 */
	/*
	   Test #3: half of nodes (node #0, #2, #4, #6, ...) do not send
	   commands, but other half of nodes send.
	   Console receives commands from every node.
	 */
	flags_reset(nr);
	commands_ut_recv_all(nr, timeout_get_abs());
	C2_UT_ASSERT(is_flags_set_odd(nr));
	barrier_with_nodes();				/* barrier #3 */
	/*
	   Test #4: half of nodes (node #0, #2, #4, #6, ...) do not start
	   waiting for commands, but other half of nodes start.
	   Console sends commands to every node.
	 */
	flags_reset(nr);
	commands_ut_send_all(nr);
	c2_net_test_commands_send_wait_all(&console);
	barrier_with_nodes();				/* barrier #4.0 */
	C2_UT_ASSERT(is_flags_set_odd(nr));
	barrier_with_nodes();				/* barrier #4.1 */
	/*
	   Test #5: every node sends two commands, and only after that console
	   starts to receive.
	 */
	/* nodes will send two commands here */
	barrier_with_nodes();				/* barrier #5.0 */
	commands_ut_recv_all(nr, C2_TIME_NEVER);
	flags_reset(nr);
	commands_ut_recv_all(nr, timeout_get_abs());
	C2_ASSERT(is_flags_set(nr, false));
	barrier_with_nodes();				/* barrier #5.1 */
	/* stop all threads */
	for (i = 0; i < nr; ++i) {
		rc = c2_thread_join(&node[i].ntcn_thread);
		C2_UT_ASSERT(rc == 0);
		c2_thread_fini(&node[i].ntcn_thread);
		barrier_fini(&node[i]);
	}

	/* cleanup */
	c2_free(node);
	c2_net_test_commands_fini(&console);
	c2_net_test_slist_fini(&slist_console);
	c2_net_test_slist_fini(&slist_node);
}

void c2_net_test_cmd_ut_single(void)
{
	net_test_command_ut(1);
}

void c2_net_test_cmd_ut_multiple(void)
{
	net_test_command_ut(NTC_MULTIPLE_COMMANDS);
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
