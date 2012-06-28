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

/** @todo remove */
#ifndef __KERNEL__
#include <stdio.h>		/* printf */
#endif

#include "lib/ut.h"		/* C2_UT_ASSERT */
#include "lib/semaphore.h"	/* c2_semaphore */
#include "lib/memory.h"		/* c2_alloc */
#include "net/lnet/lnet.h"	/* c2_net_lnet_ifaces_get */

#include "net/test/commands.h"

#ifndef __KERNEL__
#define LOGD(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOGD(format, ...) do {} while (0)
#endif

/* NTC_ == NET_TEST_COMMANDS_ */
enum {
	NTC_PORT	      = 30,
	NTC_SVC_ID_CONSOLE    = 4000,
	NTC_SVC_ID_NODE	      = 4001,
	NTC_MULTIPLE_COMMANDS = 8,
	NTC_SLIST_NR	      = 16,
	NTC_ADDR_LEN_MAX      = 0x100,
};

static const char   NTC_ADDR[]	  = "0@lo:12345:%d:%d";
static const size_t NTC_ADDR_LEN  = ARRAY_SIZE(NTC_ADDR);
static const char   NTC_DELIM     = ',';
static const char   NTC_TIMEOUT[] = ".5s";

struct net_test_cmd_node {
	struct c2_thread	   ntcn_thread;
	struct c2_net_test_cmd_ctx ntcn_ctx;
	/* index (range: [0..nr)) in nodes list */
	int			   ntcn_index;
	/* used for barriers with the main thread */
	struct c2_semaphore	   ntcn_signal;
	struct c2_semaphore	   ntcn_wait;
	/* failures number */
	struct c2_atomic64	   ntcn_failures;
	/* barriers are disabled for this node */
	bool			   ntcn_barriers_disabled;
};

static char addr_console[NTC_ADDR_LEN_MAX];
static char addr_node[NTC_ADDR_LEN_MAX * NTC_MULTIPLE_COMMANDS];

static struct c2_net_test_slist  slist_node;
static struct c2_net_test_slist  slist_console;
static struct net_test_cmd_node	*node;

static int make_addr(char *s, size_t s_len, int port, int svc_id, bool add_comma)
{
	int rc = snprintf(s, s_len, NTC_ADDR, port, svc_id);

	C2_ASSERT(NTC_ADDR_LEN <= NTC_ADDR_LEN_MAX);
	C2_POST(rc > 0);

	if (add_comma) {
		s[rc++] = NTC_DELIM;
		s[rc] = '\0';
	}
	return rc;
}

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
	if (!value)
		c2_atomic64_inc(&node->ntcn_failures);
	return value;
}

/**
   Check for non-zero values in node->ntcm_failures for every node.
   Called from the main thread.
   @see net_test_command_ut().
 */
static void commands_ut_check(void)
{
	int i;

	for (i = 0; i < slist_node.ntsl_nr; ++i)
		C2_UT_ASSERT(c2_atomic64_get(&node[i].ntcn_failures) == 0);
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
   @see net_test_command_ut(), commands_ut_check().
 */
static void barrier_with_nodes(void)
{
	int i;

	for (i = 0; i < slist_node.ntsl_nr; ++i)
		if (!node[i].ntcn_barriers_disabled)
			c2_semaphore_down(&node[i].ntcn_signal);
	commands_ut_check();
	for (i = 0; i < slist_node.ntsl_nr; ++i)
		if (!node[i].ntcn_barriers_disabled)
			c2_semaphore_up(&node[i].ntcn_wait);
}

/**
   Called from the node threads.
   @see commands_ut_assert().
 */
static void barrier_disable(struct net_test_cmd_node *node)
{
	node->ntcn_barriers_disabled = true;
	c2_semaphore_up(&node->ntcn_signal);
}

static void commands_node_thread(struct net_test_cmd_node *node)
{
	int rc;

	if (node == NULL)
		return;

	c2_atomic64_set(&node->ntcn_failures, 0);
	rc = c2_net_test_commands_init(&node->ntcn_ctx,
				       slist_node.ntsl_list[node->ntcn_index],
				       c2_time_from_str(NTC_TIMEOUT),
				       c2_time_from_str(NTC_TIMEOUT),
				       &slist_console);
	if (!commands_ut_assert(node, rc == 0))
		return barrier_disable(node);
	/* barrier #0 with the main thread */
	barrier_with_main(node);
	/* test #1 */
	/* barrier #1 with the main thread */
	barrier_with_main(node);
	/* barrier #2 with the main thread */
	barrier_with_main(node);
	/* barrier #3 with the main thread */
	barrier_with_main(node);
	/* barrier #4 with the main thread */
	barrier_with_main(node);
	c2_net_test_commands_fini(&node->ntcn_ctx);
}

static void net_test_command_ut(uint32_t nr)
{
	static struct c2_net_test_cmd_ctx console;
	uint32_t			  i;
	int				  rc;
	bool				  rc_bool;

	C2_UT_ASSERT(nr > 0);

	/* prepare addresses */
	fill_addr(nr);
	rc = c2_net_test_slist_init(&slist_node, addr_node, NTC_DELIM);
	C2_UT_ASSERT(rc == 0);
	rc_bool = c2_net_test_slist_unique(&slist_node);
	C2_UT_ASSERT(rc_bool);
	rc = c2_net_test_slist_init(&slist_console, addr_console, NTC_DELIM);
	C2_UT_ASSERT(rc == 0);
	/* init console */
	rc = c2_net_test_commands_init(&console,
				       addr_console,
				       c2_time_from_str(NTC_TIMEOUT),
				       c2_time_from_str(NTC_TIMEOUT),
				       &slist_node);
	C2_UT_ASSERT(rc == 0);
	/* alloc nodes */
	C2_ALLOC_ARR(node, nr);
	C2_UT_ASSERT(node != NULL);

	/*
	   start thread for every node because c2_net_test_commands_*()
	   functions have blocking interface.
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
	/* barrier #0 with all threads */
	barrier_with_nodes();
	/*
	   Test #1: console sends command to every node.
	 */
	/* barrier #1 with all threads */
	barrier_with_nodes();
	/*
	   Test #2: every node sends command to console.
	 */
	/* barrier #2 with all threads */
	barrier_with_nodes();
	/*
	   Test #3: half of nodes (node #0, #2, #4, #6, ...) do not send
	   commands, but other half of nodes send.
	   Console receives commands from every node.
	 */
	/* barrier #3 with all threads */
	barrier_with_nodes();
	/*
	   Test #4: half of nodes (node #0, #2, #4, #6, ...) do not start
	   waiting for commands, but other half of nodes start.
	   Console sends commands to every node.
	 */
	/* barrier #4 with all threads */
	barrier_with_nodes();
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

void c2_net_test_cmd_ut_slist(void)
{
	struct c2_net_test_slist slist;
	static char		 buf[NTC_SLIST_NR * 3];
	int			 i;
	int			 j;
	int			 rc;

	/* NULL-string test */
	rc = c2_net_test_slist_init(&slist, NULL, ':');
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(slist.ntsl_nr == 0);
	c2_net_test_slist_fini(&slist);
	/* empty string test */
	rc = c2_net_test_slist_init(&slist, "", ':');
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(slist.ntsl_nr == 0);
	c2_net_test_slist_fini(&slist);
	/* one string test */
	rc = c2_net_test_slist_init(&slist, "asdf", ',');
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(slist.ntsl_nr == 1);
	C2_UT_ASSERT(slist.ntsl_list != NULL);
	C2_UT_ASSERT(slist.ntsl_list[0] != NULL);
	C2_UT_ASSERT(strncmp(slist.ntsl_list[0], "asdf", 5) == 0);
	c2_net_test_slist_fini(&slist);
	/* one-of-strings-is-empty test */
	rc = c2_net_test_slist_init(&slist, "asdf,", ',');
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(slist.ntsl_nr == 2);
	C2_UT_ASSERT(slist.ntsl_list != NULL);
	for (i = 0; i < 2; ++i)
		C2_UT_ASSERT(slist.ntsl_list[i] != NULL);
	C2_UT_ASSERT(strncmp(slist.ntsl_list[0], "asdf", 5) == 0);
	C2_UT_ASSERT(strncmp(slist.ntsl_list[1], "",     1) == 0);
	c2_net_test_slist_fini(&slist);
	/* many strings test */
	/* fill string with some pattern (for example, "01,12,23,34\0") */
	for (i = 0; i < NTC_SLIST_NR; ++i) {
		buf[i * 3]     = '0' + i % 10;
		buf[i * 3 + 1] = '0' + (i + 1) % 10;
		buf[i * 3 + 2] = ',';
	}
	/* run test for every string number in [0, NET_TEST_SLIST_NR) */
	for (i = 0; i < NTC_SLIST_NR; ++i) {
		/* cut the line */
		buf[i * 3 + 2] = '\0';
		rc = c2_net_test_slist_init(&slist, buf, ',');
		C2_UT_ASSERT(rc == 0);
		/* check number of string in string list */
		C2_UT_ASSERT(slist.ntsl_nr == i + 1);
		C2_UT_ASSERT(slist.ntsl_list != NULL);
		/* for every string in the list */
		for (j = 0; j < i; ++j) {
			/* check the string content */
			rc = memcmp(slist.ntsl_list[j], &buf[j * 3], 2);
			C2_UT_ASSERT(rc == 0);
			/* check the string size */
			rc = strlen(slist.ntsl_list[j]);
			C2_UT_ASSERT(rc == 2);
		}
		c2_net_test_slist_fini(&slist);
		/* restore line delimiter */
		buf[i * 3 + 2] = ',';
	}
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

