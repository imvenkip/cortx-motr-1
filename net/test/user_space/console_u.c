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

#include <stdio.h>			/* printf */
#include <string.h>			/* strlen */

#include "lib/getopts.h"		/* m0_getopts */
#include "lib/errno.h"			/* EINVAL */
#include "lib/memory.h"			/* m0_alloc */
#include "lib/time.h"			/* m0_time_t */

#include "mero/init.h"		/* m0_init */

#include "net/test/user_space/common_u.h" /* m0_net_test_u_str_copy */
#include "net/test/slist.h"		/* m0_net_test_slist */
#include "net/test/stats.h"		/* m0_net_test_stats */
#include "net/test/console.h"		/* m0_net_test_console_ctx */

/**
   @page net-test-fspec-cli-console Test console command line parameters
   @todo Update obsoleted options.

   Installing/uninstalling test suite (kernel modules, scripts etc.)
   to/from remote host:
   - @b --install Install test suite. This means only copying binaries,
                  scripts etc., but not running something.
   - @b --uninstall Uninstall test suite.
   - @b --remote-path Remote path for installing.
   - @b --targets Comma-separated list of host names for installion.

   Running test:
   - @b --type Test type. Can be @b bulk or @b ping.
   - @b --clients Comma-separated list of test client hostnames.
   - @b --servers Comma-separated list of test server hostnames.
   - @b --count Number of test messages to exchange between every test
		client and every test server.
   - @b --size Size of bulk messages, bytes. Makes sense for bulk test only.
   - @b --remote-path Path to test suite on remote host.
   - @b --live Live report update time, seconds.

   @section net-test-fspec-usecases-console Test console parameters example

   @code
   --install --remote-path=$HOME/net-test --targets=c1,m0,c3,s1,s2
   @endcode
   Install test suite to $HOME/net-test directory on hosts c1, m0, c3,
   s1 and s2.

   @code
   --uninstall --remote-path=/tmp/net-test --targets=host1,host2
   @endcode
   Uninstall test suite on hosts host1 and host2.

   @code
   --type=ping --clients=c1,m0,c3 --servers=s1,s2 --count=1024
   --remote-path=$HOME/net-test
   @endcode
   Run ping test with hosts c1, m0 and c3 as clients and s2 and s2 as servers.
   Ping test should have 1024 test messages and test suite on remote hosts
   is installed in $HOME/net-test.

   @code
   --type=bulk --clients=host1 --servers=host2 --count=1000000 --size=1M
   --remote-path=$HOME/net-test --live=1
   @endcode
   Run bulk test with host1 as test client and host2 as test server. Number of
   bulk packets is one million, size is 1 MiB. Test statistics should be updated
   every second.

   @see @ref net-test
 */

/**
   @defgroup NetTestUConsoleInternals Test Console user-space program
   @ingroup NetTestInternals

   @see @ref net-test

   @{
 */

/** Console printf */
static bool addr_check(const char *addr)
{
	if (addr == NULL)
		return false;
	if (strlen(addr) == 0)
		return false;
	/** @todo additional checks */
	return true;
}

static bool addr_list_check(struct m0_net_test_slist *slist)
{
	size_t i;

	if (!m0_net_test_slist_invariant(slist))
		return false;
	if (slist->ntsl_nr == 0)
		return false;
	for (i = 0; i < slist->ntsl_nr; ++i)
		if (!addr_check(slist->ntsl_list[i]))
			return false;
	return true;
}

static bool config_check(struct m0_net_test_console_cfg *cfg)
{
	if (!addr_check(cfg->ntcc_addr_console4servers) ||
	    !addr_check(cfg->ntcc_addr_console4clients))
		return false;
	if (!addr_list_check(&cfg->ntcc_servers) ||
	    !addr_list_check(&cfg->ntcc_clients))
		return false;
	if (!addr_list_check(&cfg->ntcc_data_servers) ||
	    !addr_list_check(&cfg->ntcc_data_clients))
		return false;
	if (cfg->ntcc_servers.ntsl_nr != cfg->ntcc_data_servers.ntsl_nr)
		return false;
	if (cfg->ntcc_clients.ntsl_nr != cfg->ntcc_data_clients.ntsl_nr)
		return false;
	if (!(cfg->ntcc_test_type == M0_NET_TEST_TYPE_PING ||
	      cfg->ntcc_test_type == M0_NET_TEST_TYPE_BULK))
		return false;
	if (cfg->ntcc_msg_nr == 0 || cfg->ntcc_msg_size == 0)
	if (cfg->ntcc_concurrency_server == 0 ||
	    cfg->ntcc_concurrency_client == 0)
		return false;
	return true;
}

static void print_slist(char *name, struct m0_net_test_slist *slist)
{
	size_t i;

	M0_PRE(slist != NULL);

	m0_net_test_u_printf_v("%s: size\t= %lu\n", name, slist->ntsl_nr);
	for (i = 0; i < slist->ntsl_nr; ++i)
		m0_net_test_u_printf_v("%lu | %s\n", i, slist->ntsl_list[i]);
}

static void config_print(struct m0_net_test_console_cfg *cfg)
{
	/** @todo write text */
	m0_net_test_u_print_s("addr_console4servers\t= %s\n",
			      cfg->ntcc_addr_console4servers);
	m0_net_test_u_print_s("addr_console4clients\t= %s\n",
			      cfg->ntcc_addr_console4clients);
	print_slist("ntcc_servers", &cfg->ntcc_servers);
	print_slist("ntcc_clients", &cfg->ntcc_clients);
	print_slist("ntcc_data_servers", &cfg->ntcc_data_servers);
	print_slist("ntcc_data_clients", &cfg->ntcc_data_clients);
	m0_net_test_u_print_time("ntcc_cmd_send_timeout",
				 cfg->ntcc_cmd_send_timeout);
	m0_net_test_u_print_time("ntcc_cmd_recv_timeout",
				 cfg->ntcc_cmd_send_timeout);
	m0_net_test_u_print_time("ntcc_buf_send_timeout",
				 cfg->ntcc_cmd_send_timeout);
	m0_net_test_u_print_time("ntcc_buf_recv_timeout",
				 cfg->ntcc_cmd_send_timeout);
	m0_net_test_u_printf_v("ntcc_test_type\t\t= %s\n",
	      cfg->ntcc_test_type == M0_NET_TEST_TYPE_PING ? "ping" :
	      cfg->ntcc_test_type == M0_NET_TEST_TYPE_BULK ? "bulk" :
	      "UNKNOWN");
	m0_net_test_u_printf_v("ntcc_msg_nr\t\t= %lu\n",
			       cfg->ntcc_msg_nr);
	m0_net_test_u_printf_v("ntcc_msg_size\t\t= %lu\n",
			       cfg->ntcc_msg_size);
	m0_net_test_u_printf_v("ntcc_concurrency_server\t= %lu\n",
			       cfg->ntcc_concurrency_server);
	m0_net_test_u_printf_v("ntcc_concurrency_client\t= %lu\n",
			       cfg->ntcc_concurrency_client);
}

static int configure(int argc, char *argv[],
		     struct m0_net_test_console_cfg *cfg)
{
	bool list_if = false;
	bool success = true;
	/** @todo single-letter options is very bad */
	M0_GETOPTS("m0netperf", argc, argv,
		M0_STRINGARG('t', "Test type, {ping|bulk}",
		LAMBDA(void, (const char *type) {
			if (strncmp(type, "ping", 5) == 0)
				cfg->ntcc_test_type = M0_NET_TEST_TYPE_PING;
			else if (strncmp(type, "bulk", 5) == 0)
				cfg->ntcc_test_type = M0_NET_TEST_TYPE_BULK;
			else
				success = false;
		})),
		M0_NUMBERARG('n', "Number of test messages "
			      "for the test client",
		LAMBDA(void, (int64_t nr) {
			if (nr <= 0)
				success = false;
			else
				cfg->ntcc_msg_nr = nr;
		})),
		M0_SCALEDARG('s', "Test message size",
		LAMBDA(void, (m0_bcount_t size) {
			if (size <= 0)
				success = false;
			else
				cfg->ntcc_msg_size = size;
		})),
		M0_STRINGARG('a', "Console command endpoint address "
				  "for the test servers",
		LAMBDA(void, (const char *str) {
			cfg->ntcc_addr_console4servers =
				m0_net_test_u_str_copy(str);
		})),
		M0_STRINGARG('b', "Console command endpoint address "
				  "for the test clients",
		LAMBDA(void, (const char *str) {
			cfg->ntcc_addr_console4clients =
				m0_net_test_u_str_copy(str);
		})),
		M0_STRINGARG('c', "List of test server command endpoints",
		LAMBDA(void, (const char *str) {
			success &= m0_net_test_slist_init(&cfg->ntcc_servers,
							  str, ',') == 0;
		})),
		M0_STRINGARG('d', "List of test client command endpoints",
		LAMBDA(void, (const char *str) {
			success &= m0_net_test_slist_init(&cfg->ntcc_clients,
							  str, ',') == 0;
		})),
		M0_STRINGARG('e', "List of test server data endpoints",
		LAMBDA(void, (const char *str) {
			success &=
			m0_net_test_slist_init(&cfg->ntcc_data_servers,
					       str, ',') == 0;
		})),
		M0_STRINGARG('f', "List of test client data endpoints",
		LAMBDA(void, (const char *str) {
			success &=
			m0_net_test_slist_init(&cfg->ntcc_data_clients,
					       str, ',') == 0;
		})),
		M0_NUMBERARG('g', "Test server concurrency",
		LAMBDA(void, (int64_t nr) {
			if (nr <= 0)
				success = false;
			else
				cfg->ntcc_concurrency_server = nr;
		})),
		M0_NUMBERARG('h', "Test client concurrency",
		LAMBDA(void, (int64_t nr) {
			if (nr <= 0)
				success = false;
			else
				cfg->ntcc_concurrency_client = nr;
		})),
		M0_VERBOSEFLAGARG,
		M0_IFLISTARG(&list_if),
		M0_HELPARG('?'),
		);
	if (!list_if)
		config_print(cfg);
	success &= config_check(cfg);
	return list_if ? 1 : success ? 0 : -1;
}

static void config_free(struct m0_net_test_console_cfg *cfg)
{
	m0_net_test_u_str_free(cfg->ntcc_addr_console4servers);
	m0_net_test_u_str_free(cfg->ntcc_addr_console4clients);
	m0_net_test_slist_fini(&cfg->ntcc_servers);
	m0_net_test_slist_fini(&cfg->ntcc_clients);
	m0_net_test_slist_fini(&cfg->ntcc_data_servers);
	m0_net_test_slist_fini(&cfg->ntcc_data_clients);
}

static bool console_step(struct m0_net_test_console_ctx *ctx,
			 enum m0_net_test_role role,
			 enum m0_net_test_cmd_type cmd_type,
			 const char *text_pre,
			 const char *text_post)
{
	int rc;

	if (text_pre != NULL)
		m0_net_test_u_printf_v("%s\n", text_pre);
	rc = m0_net_test_console_cmd(ctx, role, cmd_type);
	if (text_post != NULL)
		m0_net_test_u_printf_v("%s (%d node%s)\n",
				       text_post, rc, rc != 1 ? "s" : "");
	return rc != 0;
}

static void print_msg_nr(const char *descr, struct m0_net_test_msg_nr *msg_nr)
{
	m0_net_test_u_printf_v("%s = %lu/%lu/%lu", descr, msg_nr->ntmn_total,
			       msg_nr->ntmn_failed, msg_nr->ntmn_bad);
}

static void print_stats(const char *descr,
			struct m0_net_test_stats *stats)
{
	m0_net_test_u_printf_v("%s = %lu/%lu/%lu/%.0f/%.0f", descr,
			       stats->nts_count, stats->nts_min, stats->nts_max,
			       m0_net_test_stats_avg(stats),
			       m0_net_test_stats_stddev(stats));
}

static void print_status_data_v(struct m0_net_test_cmd_status_data *sd)
{
	m0_net_test_u_printf_v("messages total/failed/bad: ");
	print_msg_nr("sent", &sd->ntcsd_msg_nr_send);
	print_msg_nr(", received", &sd->ntcsd_msg_nr_recv);
	m0_net_test_u_printf_v("; count/min/max/avg/stddev: ");
	print_stats("MPS, sent", &sd->ntcsd_mps_send.ntmps_stats);
	print_stats(", MPS, received", &sd->ntcsd_mps_recv.ntmps_stats);
	print_stats(", RTT", &sd->ntcsd_rtt);
	m0_net_test_u_printf_v(" ns\n");
}

static void bsize_print(const char *descr,
			struct m0_net_test_console_ctx *ctx,
			double msg_nr)
{
	m0_net_test_u_printf(descr);
	m0_net_test_u_print_bsize(msg_nr * ctx->ntcc_cfg->ntcc_msg_size);
}

static double avg_total(m0_time_t diff_t, double msg_nr)
{
	unsigned long diff = m0_time_seconds(diff_t) * M0_TIME_ONE_BILLION +
			     m0_time_nanoseconds(diff_t);

	return diff == 0 ? 0. : msg_nr * M0_TIME_ONE_BILLION / diff;
}

static void print_status_data(struct m0_net_test_console_ctx *ctx)
{
	struct m0_net_test_cmd_status_data *sd = ctx->ntcc_clients.ntcrc_sd;
	m0_time_t			    diff_t;
	m0_time_t			    rtt_t;
	unsigned long			    rtt;
	double				    avg_o;
	double				    avg_i;
	double				    total_o;
	double				    total_i;

	total_o = sd->ntcsd_msg_nr_send.ntmn_total;
	total_i = sd->ntcsd_msg_nr_recv.ntmn_total;
	if (sd->ntcsd_finished) {
		diff_t = m0_time_sub(sd->ntcsd_time_finish,
				     sd->ntcsd_time_start);
		avg_o = avg_total(diff_t, total_o);
		avg_i = avg_total(diff_t, total_i);
	} else {
		avg_o = m0_net_test_stats_avg(&sd->ntcsd_mps_recv.ntmps_stats);
		avg_i = m0_net_test_stats_avg(&sd->ntcsd_mps_send.ntmps_stats);
	}
	bsize_print("avg out: ", ctx, avg_o);
	bsize_print("/s avg in: ", ctx, avg_i);
	bsize_print("/s total out: ", ctx, total_o);
	bsize_print(" total in: ", ctx, total_i);

	rtt_t = m0_net_test_stats_avg(&sd->ntcsd_rtt);
	rtt = m0_time_seconds(rtt_t) * M0_TIME_ONE_BILLION +
	      m0_time_nanoseconds(rtt_t);
	m0_net_test_u_printf(" avg RTT: % 10.3f us", rtt / 1000.);

	m0_net_test_u_printf("\n");
}

static int console_run(struct m0_net_test_console_ctx *ctx)
{
	m0_time_t status_interval = M0_MKTIME(1, 0);
	bool good;

	good = console_step(ctx, M0_NET_TEST_ROLE_SERVER, M0_NET_TEST_CMD_INIT,
			    "INIT => test servers",
			    "test servers => INIT DONE");
	if (!good)
		return -ENETUNREACH;
	good = console_step(ctx, M0_NET_TEST_ROLE_CLIENT, M0_NET_TEST_CMD_INIT,
			    "INIT => test clients",
			    "test clients => INIT DONE");
	if (!good)
		return -ENETUNREACH;
	good = console_step(ctx, M0_NET_TEST_ROLE_SERVER, M0_NET_TEST_CMD_START,
			    "START => test servers",
			    "test servers => START DONE");
	if (!good)
		return -ENETUNREACH;
	good = console_step(ctx, M0_NET_TEST_ROLE_CLIENT, M0_NET_TEST_CMD_START,
			    "START => test clients",
			    "test clients => START DONE");
	if (!good)
		return -ENETUNREACH;
	do {
		/** @todo can be interrupted */
		m0_nanosleep(status_interval, NULL);
		if (!console_step(ctx, M0_NET_TEST_ROLE_CLIENT,
				  M0_NET_TEST_CMD_STATUS, NULL, NULL)) {
			m0_net_test_u_printf("STATUS DATA command failed.\n");
		} else {
			print_status_data_v(ctx->ntcc_clients.ntcrc_sd);
			print_status_data(ctx);
		}
	} while (!ctx->ntcc_clients.ntcrc_sd->ntcsd_finished);
	good = console_step(ctx, M0_NET_TEST_ROLE_SERVER,
			  M0_NET_TEST_CMD_STATUS, NULL, NULL);
	if (!good)
		return -ENETUNREACH;
	good = console_step(ctx, M0_NET_TEST_ROLE_SERVER, M0_NET_TEST_CMD_STOP,
			    "STOP => test servers",
			    "test servers => STOP DONE");
	if (!good)
		return -ENETUNREACH;
	good = console_step(ctx, M0_NET_TEST_ROLE_CLIENT, M0_NET_TEST_CMD_STOP,
			    "STOP => test clients",
			    "test clients => STOP DONE");
	if (!good)
		return -ENETUNREACH;
	m0_net_test_u_printf_v("clients total: ");
	print_status_data_v(ctx->ntcc_clients.ntcrc_sd);
	m0_net_test_u_printf_v("servers total: ");
	print_status_data_v(ctx->ntcc_servers.ntcrc_sd);
	return 0;
}

int main(int argc, char *argv[])
{
	int rc;
	struct m0_net_test_console_ctx console;
	struct m0_net_test_console_cfg cfg = {
		.ntcc_addr_console4servers = NULL,
		.ntcc_addr_console4clients = NULL,
		/** @todo add to command line parameters */
		.ntcc_cmd_send_timeout     = M0_MKTIME(3, 0),
		.ntcc_cmd_recv_timeout     = M0_MKTIME(3, 0),
		.ntcc_buf_send_timeout     = M0_MKTIME(3, 0),
		.ntcc_buf_recv_timeout     = M0_MKTIME(3, 0),
		.ntcc_test_type		   = M0_NET_TEST_TYPE_PING,
		.ntcc_msg_nr		   = 0,
		.ntcc_msg_size		   = 0,
		.ntcc_concurrency_server   = 0,
		.ntcc_concurrency_client   = 0,
	};

	rc = m0_init();
	m0_net_test_u_print_error("Mero initialization failed", rc);
	if (rc != 0)
		return rc;

	rc = configure(argc, argv, &cfg);
	if (rc != 0) {
		if (rc == 1) {
			m0_net_test_u_lnet_info();
			rc = 0;
		} else {
			/** @todo where is the error */
			m0_net_test_u_printf("Error in configuration.\n");
			config_free(&cfg);
		}
		goto mero_fini;
	}

	rc = m0_net_test_console_init(&console, &cfg);
	m0_net_test_u_print_error("Test console initialization failed", rc);
	if (rc != 0)
		goto cfg_free;

	rc = console_run(&console);
	m0_net_test_u_print_error("Test console running failed", rc);

	m0_net_test_console_fini(&console);
cfg_free:
	config_free(&cfg);
mero_fini:
	m0_fini();

	return rc;
}

/**
   @} end of NetTestUConsoleInternals group
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
