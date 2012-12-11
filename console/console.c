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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 08/03/2011
 */

#include <sysexits.h>

#include "lib/errno.h"		  /* ETIMEDOUT */
#include "lib/memory.h"		  /* m0_free */
#include "lib/getopts.h"	  /* M0_GETOPTS */
#include "mero/init.h"	  /* m0_init */
#include "net/lnet/lnet.h"
#include "fop/fop.h"
#include "ut/rpc.h"

#include "console/console.h"
#include "console/console_mesg.h"
#include "console/console_it.h"
#include "console/console_yaml.h"
#include "console/console_fop.h"

/**
   @addtogroup console
   @{
 */

uint32_t timeout;
bool     verbose;

/**
 * @brief Iterate over FOP and print names of its members.
 *
 * @param opcode Item type opcode
 */
static int fop_info_show(uint32_t opcode)
{
	struct m0_fop_type *ftype;

	fprintf(stdout, "\n");
	ftype = m0_cons_fop_type_find(opcode);
	if (ftype == NULL) {
		fprintf(stderr, "Invalid FOP opcode %.2d.\n", opcode);
		return -EINVAL;
	}
	m0_cons_fop_name_print(ftype);
	fprintf(stdout, "\n");
	return m0_cons_fop_show(ftype);
}

/**
 * @brief Build the RPC item using FOP (Embedded into item) and send it.
 *
 * @param cctx RPC Client context
 * @param opcode RPC item opcode
 */
static int fop_send_and_print(struct m0_rpc_client_ctx *cctx, uint32_t opcode)
{
	struct m0_fop_type *ftype;
	struct m0_rpc_item *item;
	struct m0_fop	   *fop;
	struct m0_fop	   *rfop;
	int		    rc;

	ftype = m0_cons_fop_type_find(opcode);
	if (ftype == NULL)
		return -EINVAL;

	/* Allocate fop */
	fop = m0_fop_alloc(ftype, NULL);
	if (fop == NULL)
		return -EINVAL;

	fprintf(stdout, "\nSending message for ");
	m0_cons_fop_name_print(ftype);
	m0_cons_fop_obj_input(fop);
	rc = m0_rpc_client_call(fop, &cctx->rcx_session,
				&m0_fop_default_item_ops, 0 /* deadline */,
				timeout);
	if (rc != 0) {
		fprintf(stderr, "Sending message failed!\n");
		return -EINVAL;
	}

	/* Fetch the FOP reply */
	item = &fop->f_item;
        if (item->ri_error != 0) {
		fprintf(stderr, "rpc item receive failed.\n");
		return -EINVAL;
	}

	rfop = m0_rpc_item_to_fop(item->ri_reply);
	if(rfop == NULL) {
		fprintf(stderr, "RPC item reply not received.\n");
		return -EINVAL;
	}

	/* Print reply */
	fprintf(stdout, "Print reply FOP: \n");
	m0_cons_fop_obj_output(rfop);

	m0_xcode_free(&M0_FOP_XCODE_OBJ(fop));
	m0_xcode_free(&M0_FOP_XCODE_OBJ(rfop));

	return 0;
}

const char *usage_msg =	"Usage: m0console "
			" { -l FOP list | -f FOP opcode }"
			" [-s server (e.g. 172.18.50.40@o2ib1:12345:34:1) ]"
			" [-c client (e.g. 172.18.50.40@o2ib1:12345:34:*) ]"
			" [-t timeout]"
			" [[-i] [-y yaml file path]]"
			" [-h] [-v]";

static void usage(void)
{
	fprintf(stderr, "%s\n", usage_msg);
}

/**
 * @brief The service to connect to is specified at the command line.
 *
 *	  The fop type to be sent is specified at the command line.
 *
 *	  The values of fop fields are specified interactively. The program
 *	  locates the fop type format (m0_fop_type_format) corresponding to the
 *	  specified fop type and iterates over fop fields, prompting the user
 *	  for the field values.
 *
 *	  Fop iterator code should be used. The program should support RECORD,
 *	  SEQUENCE and UNION aggregation types, as well as all atomic types
 *	  (U32, U64, BYTE and VOID).
 *
 *	  Usage:
 *	  m0console :	{ -l FOP list | -f FOP opcode }
 *			[-s server (e.g. 172.18.50.40\@o2ib1:12345:34:1) ]
 *			[-c client (e.g. 172.18.50.40\@o2ib1:12345:34:*) ]
 *                      [-q TM recv queue min length] [-m max rpc msg size]
 *			[-t timeout] [[-i] [-y yaml file path]] [-v]
 *
 * @return 0 success, -errno failure.
 */
#ifdef CONSOLE_UT
int console_main(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
	int                   result;
	uint32_t              opcode         = 0;
	bool                  show           = false;
	bool                  input          = false;
	const char           *client         = NULL;
	const char           *server         = NULL;
	const char           *yaml_path      = NULL;
	struct m0_net_xprt   *xprt           = &m0_net_lnet_xprt;
	struct m0_net_domain  client_net_dom = { };
	struct m0_dbenv       client_dbenv;
	struct m0_cob_domain  client_cob_dom;
	uint32_t              tm_recv_queue_len = M0_NET_TM_RECV_QUEUE_DEF_LEN;
	uint32_t              max_rpc_msg_size  = M0_RPC_DEF_MAX_RPC_MSG_SIZE;


	struct m0_rpc_client_ctx cctx = {
		.rcx_net_dom               = &client_net_dom,
		.rcx_local_addr            = "0@lo:12345:34:*",
		.rcx_remote_addr           = "0@lo:12345:34:1",
		.rcx_db_name               = "cons_client_db",
		.rcx_dbenv                 = &client_dbenv,
		.rcx_cob_dom_id            = 14,
		.rcx_cob_dom               = &client_cob_dom,
		.rcx_nr_slots              = 1,
		.rcx_timeout_s             = 5,
		.rcx_max_rpcs_in_flight    = 1,
	};

	verbose = false;
	yaml_support = false;
	timeout = 10;

	/*
	 * Gets the info to connect to the service and type of fop to be send.
	 */
	result = M0_GETOPTS("m0console", argc, argv,
			    M0_HELPARG('h'),
			    M0_FLAGARG('l', "show list of fops", &show),
			    M0_FORMATARG('f', "fop type", "%u", &opcode),
			    M0_STRINGARG('s', "server",
			    LAMBDA(void, (const char *name){server =  name; })),
			    M0_STRINGARG('c', "client",
			    LAMBDA(void, (const char *name){client = name; })),
			    M0_FORMATARG('t', "wait time(in seconds)",
					 "%u", &timeout),
			    M0_FLAGARG('i', "yaml input", &input),
			    M0_FORMATARG('q', "minimum TM receive queue length",
					 "%i", &tm_recv_queue_len),
			    M0_FORMATARG('m', "max rpc msg size", "%i",
					 &max_rpc_msg_size),
			    M0_STRINGARG('y', "yaml file path",
			    LAMBDA(void, (const char *name){ yaml_path = name; })),
			    M0_FLAGARG('v', "verbose", &verbose));
	if (result != 0)
		/*
		 * No need to print "usage" here, M0_GETOPTS will automatically
		 * do it for us
		 */
		return EX_USAGE;

	/* If no argument provided */
	if (argc == 1) {
		usage();
		return EX_USAGE;
	}

	/* Verbose is true but no other input is valid */
	if (verbose && argc == 2) {
		usage();
		return EX_USAGE;
	}

	/* Input is false but yaml is assigned path */
	if ((!input && yaml_path != NULL) ||
	    (input && yaml_path == NULL)) {
		usage();
		return EX_USAGE;
	}

	/* Init YAML info */
	if (input) {
		result = m0_cons_yaml_init(yaml_path);
		if (result != 0) {
			fprintf(stderr, "YAML Init failed\n");
			return EX_NOINPUT;
		}

		server = m0_cons_yaml_get_value("server");
		if (server == NULL) {
			fprintf(stderr, "Server assignment failed\n");
			result = EX_DATAERR;
			goto yaml;
		}
		client = m0_cons_yaml_get_value("client");
		if (client == NULL) {
			fprintf(stderr, "Client assignment failed\n");
			result = EX_DATAERR;
			goto yaml;
		}
	}

#ifndef CONSOLE_UT
	result = m0_init();
	if (result != 0) {
		fprintf(stderr, "m0_init failed\n");
		return EX_SOFTWARE;
	}

	result = m0_console_fop_init();
	if (result != 0) {
		fprintf(stderr, "m0_console_fop_init failed\n");
		goto end0;
	}
#endif
	if (show && opcode <= 0) {
		m0_cons_fop_list_show();
		usage();
		result = EX_USAGE;
		goto end1;
	}

	if (show && opcode > 0) {
		result = fop_info_show(opcode);
		if (result == 0)
			result = EX_OK;
		goto end1;
	}

	result = m0_net_xprt_init(xprt);
	M0_ASSERT(result == 0);

	result = m0_net_domain_init(&client_net_dom, xprt);
	M0_ASSERT(result == 0);

	/* Init the console members from CLI input */
	if (server != NULL)
		cctx.rcx_remote_addr = server;
	if (client != NULL)
		cctx.rcx_local_addr = client;
	cctx.rcx_recv_queue_min_length = tm_recv_queue_len;
	cctx.rcx_max_rpc_msg_size      = max_rpc_msg_size;

	result = m0_rpc_client_init(&cctx);
	if (result != 0) {
		fprintf(stderr, "m0_rpc_client_init failed\n");
		result = EX_SOFTWARE;
		goto end1;
	}

	printf("Console Address = %s\n", cctx.rcx_local_addr);
	printf("Server Address = %s\n", cctx.rcx_remote_addr);

	/* Build the fop/fom/item and send */
	result = fop_send_and_print(&cctx, opcode);
	if (result != 0) {
		fprintf(stderr, "fop_send_and_print failed\n");
		result = EX_SOFTWARE;
		goto cleanup;
	}

cleanup:
	result = m0_rpc_client_fini(&cctx);
	M0_ASSERT(result == 0);
end1:
#ifndef CONSOLE_UT
	m0_console_fop_fini();
end0:
	m0_fini();
#endif

yaml:
	if (input)
		m0_cons_yaml_fini();

	return result;
}

/** @} end of console group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
