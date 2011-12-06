/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <sysexits.h>

#include "lib/errno.h"		  /* ETIMEDOUT */
#include "net/bulk_sunrpc.h"	  /* bulk transport */
#include "colibri/init.h"	  /* c2_init */
#include "lib/processor.h"        /* c2_processors_init/fini */
#include "lib/getopts.h"	  /* C2_GETOPTS */

#include "console/console.h"
#include "console/console_mesg.h"
#include "console/console_it.h"
#include "console/console_yaml.h"
#include "console/console_fop.h"

/**
   @addtogroup console main
   @{
 */

static struct c2_console cons_client = {
	.cons_lepaddr	      = "127.0.0.1:123456:1",
	.cons_repaddr	      = "127.0.0.1:123457:1",
	.cons_db_name	      = "cons_client_db",
	.cons_cob_dom_id      = { .id = 14 },
	.cons_nr_slots	      = NR_SLOTS,
	.cons_xprt	      = &c2_net_bulk_sunrpc_xprt,
	.cons_items_in_flight = MAX_RPCS_IN_FLIGHT
};

/**
 * @brief Iterator over FOP and prints names of its members.
 *
 * @param type 0 shows list of FOPS.
 *	       ~0 displays info related to FOP.
 */
static void fop_info_show(enum c2_cons_mesg_type type)
{
	struct c2_cons_mesg *mesg;

	printf("\nInfo for \"");
	mesg = c2_cons_mesg_get(type);
	c2_cons_mesg_name_print(mesg);
	printf("\"\n");
	c2_cons_mesg_fop_show(mesg->cm_fopt);
}

/**
 * @brief Build the RPC item using FOP(Embedded into item) and send it.
 *
 * @param cons Console object ref.
 * @param type FOP type(disk failure, device failure).
 *
 * @return item success, NULL failure.
 */
static int message_send_and_print(struct c2_console *cons,
				  enum c2_cons_mesg_type type)
{
        struct c2_fit		  it;
	struct c2_cons_mesg	 *mesg;
	struct c2_rpc_item	 *item;
	struct c2_fop		 *fop;
	c2_time_t		  deadline;
	int			  rc;

	C2_PRE(cons != NULL);

	deadline = c2_cons_timeout_construct(timeout);
	mesg = c2_cons_mesg_get(type);
	mesg->cm_rpc_mach = &cons->cons_rpc_mach;
	mesg->cm_rpc_session = &cons->cons_rpc_session;
	printf("\nSending message for ");
	c2_cons_mesg_name_print(mesg);
	rc = c2_cons_mesg_send(mesg, deadline);
	if (rc != 0) {
		fprintf(stderr, "Sending message failed!\n");
		return -EINVAL;
	}

	/* Fetch the FOP reply */
	item = &mesg->cm_fop->f_item;
        if (item->ri_error != 0) {
		fprintf(stderr, "rpc item receive failed.\n");
		return -EINVAL;
	}

	fop = c2_rpc_item_to_fop(item->ri_reply);
	if(fop == NULL) {
		fprintf(stderr, "RPC item reply not received.\n");
		return -EINVAL;
	}

	/* Print reply */
	printf("Print reply FOP: \n");
        c2_fop_all_object_it_init(&it, fop);
	c2_cons_fop_obj_output(&it);
        c2_fop_all_object_it_fini(&it);

	return 0;
}

const char *usage_msg =	"c2console :"
			" { -l FOP list | -f FOP type }"
			" [-s server (e.g. 127.0.0.1:1024:1) ]"
			" [-c client (e.g. 127.0.0.1:1025:1) ]"
			" [-t timeout]"
			" [[-i] [-y yaml file path]]"
			" [-v]";

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
 *	  locates the fop type format (c2_fop_type_format) corresponding to the
 *	  specified fop type and iterates over fop fields, prompting the user
 *	  for the field values.
 *
 *	  Fop iterator code should be used. The program should support RECORD,
 *	  SEQUENCE and UNION aggregation types, as well as all atomic types
 *	  (U32, U64, BYTE and VOID).
 *
 *	  Usage:
 *	  c2console :	{ -l FOP list | -f FOP type }
 *			[-s server (e.g. 127.0.0.1:1024:1) ]
 *			[-c client (e.g. 127.0.0.1:1025:1) ] 
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
	enum c2_cons_mesg_type	type;
	int			result;
	bool			show = false;
	bool			input = false;
	const char		*server = NULL;
	const char		*client = NULL;
	const char		*yaml_path = NULL;

	verbose = false;
	yaml_support = false;
	timeout = TIME_TO_WAIT;
	type = CMT_MESG_NR;

	/*
	 * Gets the info to connect to the service and type of fop to be send.
	 */
	result = C2_GETOPTS("console", argc, argv,
			    C2_FLAGARG('l', "show list of fops", &show),
			    C2_FORMATARG('f', "fop type", "%u", &type),
			    C2_STRINGARG('s', "server",
			    LAMBDA(void, (const char *name){ server = name; })),
			    C2_STRINGARG('c', "client",
			    LAMBDA(void, (const char *name){ client = name; })),
			    C2_FORMATARG('t', "wait time(in seconds)",
					 "%u", &timeout),
			    C2_FLAGARG('i', "yaml input", &input),
			    C2_STRINGARG('y', "yaml file path",
			    LAMBDA(void, (const char *name){ yaml_path = name; })),
			    C2_FLAGARG('v', "verbose", &verbose));

	/* If no argument provided */
	if (result != 0 || argc == 1) {
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
		result = c2_cons_yaml_init(yaml_path);
		if (result != 0) {
			fprintf(stderr, "YAML Init failed\n");
			return EX_NOINPUT;
		}

		server = c2_cons_yaml_get_value("server");
		if (server == NULL) {
			fprintf(stderr, "Server assignment failed\n");
			result = EX_DATAERR;
			goto yaml;
		}

		client = c2_cons_yaml_get_value("client");
		if (server == NULL) {
			fprintf(stderr, "Client assignment failed\n");
			result = EX_DATAERR;
			goto yaml;
		}
	}

	/* Init the console members from CLI input */
	if (server != NULL)
		cons_client.cons_repaddr = server;
	if (client != NULL)
		cons_client.cons_lepaddr = client;

	result = c2_cons_mesg_init();
	if (result != 0) {
		fprintf(stderr, "c2_init failed\n");
		return EX_SOFTWARE;
	}

#ifndef CONSOLE_UT
	result = c2_init();
	if (result != 0) {
		fprintf(stderr, "c2_init failed\n");
		return EX_SOFTWARE;
	}

	result = c2_console_fop_init();
	if (result != 0) {
		fprintf(stderr, "c2_console_fop_init failed\n");
		goto end0;
	}

	result = c2_processors_init();
	if (result != 0) {
		fprintf(stderr, "c2_processors_init failed\n");
		result = EX_SOFTWARE;
		goto end1;
	}
#endif
	if (type >= CMT_MESG_NR) {
		c2_cons_mesg_list_show();
		usage();
		result = EX_USAGE;
		goto end1;
	}

	if (show && type < CMT_MESG_NR){
		fop_info_show(type);
		result = EX_OK;
		goto end1;
	}

	result = c2_cons_rpc_client_init(&cons_client);
	if (result != 0) {
		fprintf(stderr, "c2_cons_rpc_client_init failed\n");
		result = EX_SOFTWARE;
		goto end2;
	}

	printf("Console Address = %s\n", cons_client.cons_lepaddr);
	printf("Server Address = %s\n", cons_client.cons_repaddr);

	/* Connect to the specified server */
	result = c2_cons_rpc_client_connect(&cons_client);
	if (result != 0) {
		fprintf(stderr, "c2_cons_rpc_client_connect failed\n");
		result = EX_SOFTWARE;
		goto fini;
	}

	/* Build the fop/fom/item and send */
	result = message_send_and_print(&cons_client, type);
	if (result != 0) {
		fprintf(stderr, "message_send_and_print failed\n");
		result = EX_SOFTWARE;
		goto cleanup;
	}

cleanup:
	/* Close connection */
	c2_cons_rpc_client_disconnect(&cons_client);
fini:
	c2_cons_rpc_client_fini(&cons_client);
end2:
#ifndef CONSOLE_UT
	c2_processors_fini();
#endif
end1:
#ifndef CONSOLE_UT
	c2_console_fop_fini();
end0:
	c2_fini();
#endif

yaml:
	if (input)
		c2_cons_yaml_fini();

	c2_cons_mesg_fini();

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
