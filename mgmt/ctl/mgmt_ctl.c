/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Dave Cohrs <dave_cohrs@xyratex.com>
 * Original creation date: 12-Mar-2013
 */

/**
   @page MGMT-CTL-DLD Management CLI Design
   - @ref MGMT-CTL-DLD-ovw
   - @ref MGMT-CTL-DLD-req
   - @ref MGMT-CTL-DLD-depends
   - @ref MGMT-CTL-DLD-highlights
   - @ref MGMT-CTL-DLD-lspec
      - @ref MGMT-CTL-DLD-lspec-comps
      - @ref MGMT-CTL-DLD-lspec-mgmt-ctl
      - @ref MGMT-CTL-DLD-lspec-service
      - @ref MGMT-CTL-DLD-lspec-state
      - @ref MGMT-CTL-DLD-lspec-thread
      - @ref MGMT-CTL-DLD-lspec-numa
   - @ref MGMT-CTL-DLD-conformance
   - @ref MGMT-CTL-DLD-ut
   - @ref MGMT-CTL-DLD-st
   - @ref MGMT-CTL-DLD-O
   - @ref MGMT-CTL-DLD-impl-plan

   <hr>
   @section MGMT-CTL-DLD-ovw Overview
   This design describes CLI interfaces for managing mero, specifically
   for managing the mero kernel module and m0d process.  Mechanisms for
   starting, stopping and getting status of mero are provided.  A process
   for extending the management CLI is also presented.

   The design operates within the standard deployment pattern parameters
   outlined in @ref MGMT-DLD-lspec-osif "Extensions for the service command".

   <hr>
   @section MGMT-CTL-DLD-req Requirements
   The management service will address the following requirements described
   in the @ref MGMT-DLD-req "Management DLD".
   - @b R.cli.mgmt-api.services
   - @b R.cli.mgmt-api.query
   - @b R.cli.mgmt-api.start
   - @b R.cli.mgmt-api.shutdown

   <hr>
   @section MGMT-CTL-DLD-depends Dependencies
   - A management service as described in
   @ref MGMT-SVC-DLD "Management Service DLD"
   provides the internal APIs to implement query and service-specific
   management interfaces.
   - The @ref libgenders3 "genders" package from LLNL must be installed on all
   nodes.
     - The @c /usr/bin/nodeattr CLI is used to query the /etc/mero/genders file.
   - The m0d command must be enhanced to support bootstrap configuration from
   the /etc/mero/genders file.  This involves support for a new "-g" option
   to denote this mode of bootstrap configuration, and a corresponding
   "-f genderspath" option.
   - The ADDB subsystem must be extended to allow a mero CLI to run even
   before the mero kernel module is loaded.  This requires removing an assertion
   that the node UUID is set and providing an API to set the node UUID once
   it is known (e.g. by reading a genders file).

   <hr>
   @section MGMT-CTL-DLD-highlights Design Highlights
   - A Linux standard @c service script is provided to start, stop
   and query status of mero services.
   - A management CLI, @c m0ctl, is provided.
     - It implements the query status fuctionality of the @c service script.
     - It provides a pattern for providing future management functions.

   <hr>
   @section MGMT-CTL-DLD-lspec Logical Specification
   - @ref MGMT-CTL-DLD-lspec-comps
   - @ref MGMT-CTL-DLD-lspec-service
   - @ref MGMT-CTL-DLD-lspec-mgmt-ctl
   - @ref MGMT-CTL-DLD-lspec-state
   - @ref MGMT-CTL-DLD-lspec-thread
   - @ref MGMT-CTL-DLD-lspec-numa

   @subsection MGMT-CTL-DLD-lspec-comps Component Overview
   This design involves the following sub-components:
   - A script to be used by the Linux service CLI.
   - A management CLI.

   @subsection MGMT-CTL-DLD-lspec-service The Service CLI Interface
   The primary external interface for managing Mero services is the Linux
   @c service CLI.  The high-level behavior provided is described in
   @ref MGMT-DLD-lspec-osif.

   An /etc/rc.d/init.d/mero script implements the three supported directives:
   start, stop and status.

   The "start" directive performs the following steps:
   - It uses @c nodeattr to determine the configuration of the current node.
   If the node is not configured (m0_uuid, m0_var specified, etc), nothing
   is started.
   - It loads the Lustre lnet module if not already loaded.
   - It loads the m0mero module.
   - It changes to the directory specified in the m0_var attribute.
   - It starts the a m0d process, specifying that m0d should use the
   /etc/mero/genders file to bootstrap its configuration.
     - Functions in the /etc/rc.d/init.d/functions are used for this, with
     the side effect that the pid of the m0d is stored in a file in /var/run.

   The "stop" directive performs the following steps:
   - It sends a signal to the m0d process and waits for it to terminate.
     - Functions in the /etc/rc.d/init.d/functions are used for this.
   - It unloads the m0mero module.
   - The Lustre lnet module is not unloaded.

   The "status" directive performs the following steps:
   - It uses the m0ctl CLI to determine and print the status of the running
   m0d process, using the "query-all" operation.

   @subsection MGMT-CTL-DLD-lspec-mgmt-ctl The Management Client
   The management client is modular.  Its main program performs common
   functionality and the calls an operation-specific "main" function to
   execute a given operation.

   The common functionality includes:
   - Parsing common options and populating a m0_mgmt_ctl_ctx.
   - Determining which operation to perform.
   - Parsing the genders file.
   - Setting up ADDB in the process, including setting the node UUID.

   The external inteface of m0ctl is described in
   @ref MGMT-DLD-ref-svc-plan "Mero Service Interface Planning"
   and is not repeated here.

   In the specific case of the "query-all" operation:
   - The remaining argv are parsed (to determine optional repeat rate).
   - Send an m0_fop_mgmt_service_state_req FOP to the m0d mgmt service,
   requesting status of all services.
   - Wait for response.
   - Output status of all services, one line for each service, in the form
   service_name(UUID): State (equivlent output when YAML format is requested).
   - If a repeat rate was specified, sleep for the requested interval and
   loop back to sending the m0_fop_mgmt_service_state_req FOP.

   If an error occurs in the attempt to send the FOP or a timeout occurs
   waiting for the response, an error message is output instead.  If a repeat
   rate was specified, an error will not cause the "query-all" operation
   to terminate.  The "query-all" operation with a repeat rate will only
   terminate if an error occurs when it is generating output (e.g. if the
   output is being sent to a pipe and the other end of the pipe closes).
   Otherwise, the parent process can terminate the "query-all" operation by
   sending a signal the sub-process.

   @subsection MGMT-CTL-DLD-lspec-state State Specification
   These components have no formal state machines.

   @subsection MGMT-CTL-DLD-lspec-thread Threading and Concurrency Model
   At present, the m0ctl CLI is single-threaded (it may use functions in the
   Mero library that create additional, internal threads, but it does not
   depend on such multi-threaded behavior).

   @subsection MGMT-CTL-DLD-lspec-numa NUMA optimizations
   The m0ctl CLI requires and provides no NUMA optimizations.

   <hr>
   @section MGMT-CTL-DLD-conformance Conformance
   - @b I.cli.mgmt-api.services An /etc/rc.d/init.d script is provided for
   local services management.
   - @b I.cli.mgmt-api.query The m0ctl "query-all" operation is provided.
   - @b I.cli.mgmt-api.start An /etc/rc.d/init.d script is provided.
   - @b I.cli.mgmt-api.shutdown An /etc/rc.d/init.d script is provided.

   <hr>
   @section MGMT-CTL-DLD-ut Unit Tests
   Unit testing will be limited to tests of
   @ref MGMT-CONF-DLD "Management Configuration", as this
   is the only component that resides in the mero library.

   <hr>
   @section MGMT-CTL-DLD-st System Tests
   A system test script will be used to verify:
   - Correct CLI parsing
   - Operational (currently, query-all) behavior.

   <hr>
   @section MGMT-CTL-DLD-O Analysis
   Parsing of the genders file only occurs on startup and should have no
   significant impact on normal operation.  When m0ctl "query-all" is used
   with a repeat-rate, this is true of m0ctl as well.

   <hr>
   @section MGMT-CTL-DLD-impl-plan Implementation Plan
   - Initial implementation of m0ctl to support query-all and looping behavior.
   - The standard start, stop and status commands will be implemented in the
   /etc/rc.d/init.d script.

   Other features as required by the shipping product.

 */


#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include "fop/fop.h"
#include "lib/errno.h"
#include "lib/getopts.h"
#include "lib/misc.h"
#include "lib/string.h"
#include "lib/thread.h"
#include "lib/time.h"
#include "lib/uuid.h"
#include "mero/init.h"
#include "mgmt/mgmt.h"
#include "mgmt/mgmt_addb.h"
#include "mgmt/mgmt_fops.h"
#include "net/lnet/lnet.h"
#include "reqh/reqh.h"
#include "reqh/reqh_service.h"
#include "rpc/rpclib.h"

/* include local sources here */
#include "mgmt/ctl/mgmt_ctl.h"
#include "mgmt/ctl/utils.c"
#include "mgmt/ctl/op_query_all.c"

/**
   @ingroup mgmt_ctl_pvt
   @{
 */

/** An array of management operations */
static struct m0_mgmt_ctl_op ops[] = {
	{ .cto_op = "query-all", .cto_main = op_qa_main },
};

static struct m0_mgmt_ctl_ctx ctx;

enum {
	RPC_TIMEOUT = 2
};

static int usage()
{
	int i;

	fprintf(stderr,
"Usage: m0ctl CommandFlags Operation [OperationArgs]\n"
"\n"
"CommandFlags are defined as:\n"
" [-h Hostname | -e EndPoint] [-f GendersFile] [-t TimeoutSecs] [-y]\n"
"\n"
"-e EndPoint    Specify the raw transport end point address.\n"
"-f GendersFile Specify an alternate path to the genders file.\n"
"-h Hostname    Specify the remote host name.\n"
"-t TimeoutSecs Specify the amount of time that the command will wait for\n"
"               a response.  The default is %d seconds.\n"
"-y             Specify that the output must be emitted in YAML.\n"
"\n"
"By default a connection to the local m0d will be established unless.\n"
"\n"
"Operation is one of:\n",
		RPC_TIMEOUT);
	for (i = 0; i < ARRAY_SIZE(ops); ++i)
		fprintf(stderr, "\t%s\n", ops[i].cto_op);
	fprintf(stderr,
"\nProvide '-?' as an argument to an operation for details.\n");
	return 1;
}

/**
   Signal handler to cleanup on interrup.
 */
static void sig_handler(int signum)
{
	/*
	  DO NOT CALL
	     m0_fini();
	  HERE!
	  FORGET ABOUT A CLEAN SHUTDOWN!
	  IT CAN PANIC IF AN RPC CONNECTION IS OPEN.
	*/
	unlink_tmpdir(&ctx);
	exit(2);
}

int main(int argc, char *argv[])
{
	int i;
	int rc;
	char *cmd;
	int cmd_argc;
	struct m0_mgmt_ctl_op *op;
	char *alt_ep = NULL;
	char *alt_h = NULL;
	struct sigaction sa;

	/*
	  Parse command flags.
	  Cannot use M0_GETOPTS because it looks at the operation flags too!
	  Can use M0_GETOPTS in the individual operations.
	*/
	rc = 0;
	ctx.mcc_timeout = RPC_TIMEOUT; /* secs - convert to ns later */

#define HAS_ARG(f)							\
	if (i + 1 >= argc) {						\
		fprintf(stderr, "Error: '%s' needs an argument\n", f);  \
		return 1;						\
	}
#define SARG(f, func)				\
	if (strcmp(f, argv[i]) == 0) {		\
		HAS_ARG(f);			\
		(*func)(argv[++i]);		\
		continue;			\
	}
#define U64ARG(f, func)					\
	if (strcmp(f, argv[i]) == 0) {			\
		uint64_t ui;				\
		HAS_ARG(f);				\
		ui = strtoull(argv[++i], NULL, 0);	\
		(*func)(ui);				\
		continue;				\
	}
#define BOOLARG(f, func)			\
	if (strcmp(f, argv[i]) == 0) {		\
		(*func)();			\
		continue;			\
	}

	for (i = 1; i < argc; ++i) {
		SARG("-e", LAMBDA(void, (char *s) { alt_ep = s; }));
		SARG("-f", LAMBDA(void, (char *s) {
					ctx.mcc_genders = s;
				}));
		SARG("-h", LAMBDA(void, (char *s) { alt_h = s; }));
		U64ARG("-t", LAMBDA(void, (uint64_t ui) {
					ctx.mcc_timeout = ui;
				}));
		BOOLARG("-y", LAMBDA(void, (void) {
					ctx.mcc_yaml = true;
				}));
		if (strcmp(argv[i], "-?") == 0)
			return usage();
		if (*argv[i] == '-') {
			fprintf(stderr, "Invalid command flag '%s'\n"
				"Use '-?' for help\n", argv[i]);
			return 1;
		} else
			break; /* non-flag */
	}
	if (i >= argc)
		return usage();
#undef HAS_ARG
#undef SARG
#undef U64ARG
#undef BOOLARG

	/* find the command */
	cmd_argc = i;
	cmd = argv[cmd_argc];
	for (i = 0, op = NULL; i < ARRAY_SIZE(ops); ++i)
		if (strcmp(cmd, ops[i].cto_op) == 0) {
			op = &ops[i];
			break;
		}
	if (op == NULL)
		return usage();

	/* initialize the library */
	rc = m0_init();
	if (rc != 0) {
		emit_error(&ctx, "Failed to initialize library", rc);
		return 1;
	}

	/* set up a signal handler to remove work arena */
	M0_SET0(&sa);
	sigaddset(&sa.sa_mask, SIGTERM);
	sigaddset(&sa.sa_mask, SIGINT);
	sigaddset(&sa.sa_mask, SIGQUIT);
	sigaddset(&sa.sa_mask, SIGPIPE);
	sa.sa_handler = sig_handler;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
	sigaction(SIGPIPE, &sa, NULL);

	/* create work arena */
	rc = make_tmpdir(&ctx);
	if (rc != 0)
		return 1;

	/* addb context for conveyance */
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &ctx.mcc_addb_ctx, &m0_addb_ct_mgmt_ctl,
			 &m0_addb_proc_ctx);

	/* load the configuration */
	rc = m0_mgmt_conf_init(&ctx.mcc_conf, ctx.mcc_genders, alt_h);
	if (rc != 0) {
		emit_error(&ctx, "Failed to load configuration", rc);
		goto fail;
	}
	ctx.mcc_timeout = M0_MKTIME(ctx.mcc_timeout, 0);
	if (alt_ep != NULL)
		ctx.mcc_conf.mnc_m0d_ep = alt_ep;

	/* run the command */
	rc = op->cto_main(argc - cmd_argc, &argv[cmd_argc], &ctx);

	m0_mgmt_conf_fini(&ctx.mcc_conf);
 fail:
	m0_addb_ctx_fini(&ctx.mcc_addb_ctx);
	m0_fini();
	unlink_tmpdir(&ctx);
	return !(rc == 0);
}

/** @} end mgmt_ctl_pvt group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
