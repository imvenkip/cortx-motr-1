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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifndef CONSOLE_UT
#define CONSOLE_UT
#endif

#include <sysexits.h>

#include "lib/types.h"            /* uint64_t */
#include "lib/ut.h"
#include "lib/assert.h"
#include "lib/ub.h"
#include "lib/memory.h"
#include "lib/errno.h"            /* ETIMEDOUT */
#include "lib/processor.h"        /* c2_processors_init/fini */
#include "lib/thread.h"		  /* c2_thread */
#include "lib/trace.h"
#include "lib/misc.h"		  /* C2_SET0 */
#include "rpc/rpclib.h"           /* c2_rpc_server_start */
#include "ut/rpc.h"               /* c2_rpc_client_init */

#include "console/console.h"
#include "console/console_xc.h"
#include "console/console_fop.h"
#include "console/console_it.h"
#include "console/console_yaml.h"
#include "console/console_mesg.h"
#include "console/main.c"

/**
   @addtogroup console
   @{
 */

enum {
	COB_DOM_CLIENT_ID  = 14,
	COB_DOM_SERVER_ID = 15,
};

static const char *yaml_file = "/tmp/console_ut.yaml";
static const char *err_file = "/tmp/stderr";
static const char *out_file = "/tmp/stdout";
static const char *in_file = "/tmp/stdin";

static struct c2_ut_redirect in_redir;
static struct c2_ut_redirect out_redir;
static struct c2_ut_redirect err_redir;

#define CLIENT_ENDPOINT_ADDR    "0@lo:12345:34:2"
#define CLIENT_DB_NAME		"cons_client_db"

#define SERVER_ENDPOINT_ADDR	"0@lo:12345:34:1"
#define SERVER_ENDPOINT		"lnet:" SERVER_ENDPOINT_ADDR
#define SERVER_DB_FILE_NAME	"cons_server_db"
#define SERVER_STOB_FILE_NAME	"cons_server_stob"
#define SERVER_LOG_FILE_NAME	"cons_server.log"

enum {
	CLIENT_COB_DOM_ID	= 14,
	SESSION_SLOTS		= 1,
	MAX_RPCS_IN_FLIGHT	= 1,
	CONNECT_TIMEOUT		= 5,
};

static struct c2_net_xprt   *xprt = &c2_net_lnet_xprt;
static struct c2_net_domain  client_net_dom = { };
static struct c2_dbenv       client_dbenv;
static struct c2_cob_domain  client_cob_dom;

static struct c2_rpc_client_ctx cctx = {
	.rcx_net_dom            = &client_net_dom,
	.rcx_local_addr         = CLIENT_ENDPOINT_ADDR,
	.rcx_remote_addr        = SERVER_ENDPOINT_ADDR,
	.rcx_db_name            = CLIENT_DB_NAME,
	.rcx_dbenv              = &client_dbenv,
	.rcx_cob_dom_id         = CLIENT_COB_DOM_ID,
	.rcx_cob_dom            = &client_cob_dom,
	.rcx_nr_slots           = SESSION_SLOTS,
	.rcx_timeout_s          = CONNECT_TIMEOUT,
	.rcx_max_rpcs_in_flight = MAX_RPCS_IN_FLIGHT,
};

static char *server_argv[] = {
	"console_ut", "-r", "-T", "AD", "-D", SERVER_DB_FILE_NAME,
	"-S", SERVER_STOB_FILE_NAME, "-e", SERVER_ENDPOINT,
	"-s", "ds1", "-s", "ds2"
};

static struct c2_rpc_server_ctx sctx = {
	.rsx_xprts            = &xprt,
	.rsx_xprts_nr         = 1,
	.rsx_argv             = server_argv,
	.rsx_argc             = ARRAY_SIZE(server_argv),
	.rsx_service_types    = cs_default_stypes,
	/*
	 * can't use cs_default_stypes_nr to initialize rsx_service_types_nr,
	 * since it leads to compile-time error 'initializer element is not
	 * constant', because sctx here is a global/static variable, which not
	 * allowed to be initialized with non-constant values
	 */
	.rsx_service_types_nr = 2,
	.rsx_log_file_name    = SERVER_LOG_FILE_NAME,
};


static int cons_init(void)
{
	int result;

	timeout = 10;
	result = c2_console_fop_init();
        C2_ASSERT(result == 0);
	/*result = c2_processors_init();*/
	C2_ASSERT(result == 0);

	/*
	 * There is no need to initialize xprt explicitly if client and server
	 * run withing a single process, because in this case transport is
	 * initialized by c2_rpc_server_start().
	 */

	result = c2_net_domain_init(&client_net_dom, xprt);
	C2_ASSERT(result == 0);

	return result;
}

static int cons_fini(void)
{
	c2_net_domain_fini(&client_net_dom);
	/*c2_processors_fini();*/
	c2_console_fop_fini();
	return 0;
}

static void file_redirect_init(void)
{
	c2_stream_redirect(stdin, in_file, &in_redir);
	c2_stream_redirect(stdout, out_file, &out_redir);
	c2_stream_redirect(stderr, err_file, &err_redir);
}

static void file_redirect_fini(void)
{
	int result;

	c2_stream_restore(&in_redir);
	c2_stream_restore(&out_redir);
	c2_stream_restore(&err_redir);

	result = remove(in_file);
	C2_UT_ASSERT(result == 0);
	result = remove(out_file);
	C2_UT_ASSERT(result == 0);
	result = remove(err_file);
	C2_UT_ASSERT(result == 0);
}

static int generate_yaml_file(const char *name)
{
	FILE *fp;

	C2_PRE(name != NULL);

        fp = fopen(name, "w");
        if (fp == NULL) {
                fprintf(stderr, "Failed to create yaml file\n");
                return -errno;
        }

	fprintf(fp, "# Generated yaml file for console UT\n\n");
	fprintf(fp, "server  : localhost\n");
	fprintf(fp, "sport   : 23125\n");
	fprintf(fp, "client  : localhost\n");
	fprintf(fp, "cport   : 23126\n");
	fprintf(fp, "\n\n");
	fprintf(fp, "Test FOP:\n");
	fprintf(fp, "  - cons_seq : 1\n");
	fprintf(fp, "    cons_oid : 2\n");
	fprintf(fp, "    cons_test_type : d\n");
	fprintf(fp, "    cons_test_id : 64\n");

	fclose(fp);
	return 0;
}

static void init_test_fop(struct c2_cons_fop_test *fop)
{
	fop->cons_id.cons_seq = 1;
        fop->cons_id.cons_oid = 2;
	fop->cons_test_type = 'd';
	fop->cons_test_id = 64;
}

static void check_values(struct c2_fop *fop)
{
	struct c2_fid          *fid;
	char                   *data;
	uint64_t               *value;
	int                     result;
	struct c2_xcode_ctx     ctx;
	struct c2_xcode_cursor *it;

	c2_xcode_ctx_init(&ctx, &(struct c2_xcode_obj) {
			  *fop->f_type->ft_xc_type,
			  c2_fop_data(fop) });
	it = &ctx.xcx_it;
	while ((result = c2_xcode_next(it)) > 0) {
		const struct c2_xcode_type   *xt;
		struct c2_xcode_obj          *cur;
		struct c2_xcode_cursor_frame *top;

		top = c2_xcode_cursor_top(it);
		if (top->s_flag != C2_XCODE_CURSOR_PRE)
			continue;
		cur = &top->s_obj;
		xt  = cur->xo_type;
		if (!strcmp(xt->xct_name, "c2_cons_fop_fid")) {
			fid = cur->xo_ptr;
			C2_UT_ASSERT(fid->f_container == 1);
			C2_UT_ASSERT(fid->f_key == 2);
			c2_xcode_skip(it);
		} else if (!strcmp(xt->xct_name, "u8")) {
			data = (char *)cur->xo_ptr;
			C2_UT_ASSERT(*data == 'd');
			c2_xcode_skip(it);
		} else if (!strcmp(xt->xct_name, "u64")) {
			value = (uint64_t *)cur->xo_ptr;
			C2_UT_ASSERT(*value == 64);
			c2_xcode_skip(it);
		}
	}
}

static void fop_iterator_test(void)
{
	struct c2_fop		*fop;
        struct c2_cons_fop_test *f;

        fop = c2_fop_alloc(&c2_cons_fop_test_fopt, NULL);
        C2_UT_ASSERT(fop != NULL);
	f = c2_fop_data(fop);
        C2_UT_ASSERT(f != NULL);
	init_test_fop(f);
	check_values(fop);
        c2_fop_free(fop);
}

static void yaml_basic_test(void)
{
	int result;

	result = generate_yaml_file(yaml_file);
	C2_UT_ASSERT(result == 0);
	result = c2_cons_yaml_init(yaml_file);
	C2_UT_ASSERT(result == 0);

	/* Init and Fini */
	c2_cons_yaml_fini();
	result = c2_cons_yaml_init(yaml_file);
	C2_UT_ASSERT(result == 0);
	c2_cons_yaml_fini();

	result = remove(yaml_file);
	C2_UT_ASSERT(result == 0);
}

static void input_test(void)
{
        struct c2_fop *fop;
	int	       result;

	file_redirect_init();
	result = generate_yaml_file(yaml_file);
	C2_UT_ASSERT(result == 0);
	result = c2_cons_yaml_init(yaml_file);
	C2_UT_ASSERT(result == 0);

        fop = c2_fop_alloc(&c2_cons_fop_test_fopt, NULL);
        C2_UT_ASSERT(fop != NULL);

        c2_cons_fop_obj_input(fop);
	check_values(fop);
        c2_fop_free(fop);
	c2_cons_yaml_fini();
	result = remove(yaml_file);
	C2_UT_ASSERT(result == 0);
	file_redirect_fini();
}

static void file_compare(const char *in, const char *out)
{
	FILE *infp;
	FILE *outfp;
	int   inc;
	int   outc;

	infp = fopen(in, "r");
	C2_UT_ASSERT(infp != NULL);
	outfp = fopen(out, "r");
	C2_UT_ASSERT(outfp != NULL);

	while ((inc = fgetc(infp)) != EOF &&
	       (outc = fgetc(outfp)) != EOF) {
	       C2_UT_ASSERT(inc == outc);
	}

	fclose(infp);
	fclose(outfp);
}

static void output_test(void)
{
        struct c2_fop *f;
	int	       result;

	verbose = true;
	result = generate_yaml_file(yaml_file);
	C2_UT_ASSERT(result == 0);
	result = c2_cons_yaml_init(yaml_file);
	C2_UT_ASSERT(result == 0);

        f = c2_fop_alloc(&c2_cons_fop_test_fopt, NULL);
        C2_UT_ASSERT(f != NULL);

	file_redirect_init();

        c2_cons_fop_obj_input(f);
	c2_cons_fop_obj_output(f);

	file_compare(in_file, out_file);
	file_redirect_fini();

	verbose = false;
        c2_fop_free(f);
	c2_cons_yaml_fini();
	result = remove(yaml_file);
	C2_UT_ASSERT(result == 0);
}

static void yaml_file_test(void)
{
	int result;

	file_redirect_init();
	result = c2_cons_yaml_init(yaml_file);
	C2_UT_ASSERT(result != 0);
	file_redirect_fini();
}

static void yaml_parser_test(void)
{
	FILE *fp;
	int   result;

	file_redirect_init();
        fp = fopen(yaml_file, "w");
        C2_UT_ASSERT(fp != NULL);
	fprintf(fp, "# Generated yaml file for console UT\n\n");
	fprintf(fp, "server  : localhost\n");
	fprintf(fp, "sport   : 23125\n");
	fprintf(fp, "client  : localhost\n");
	fprintf(fp, "cport   : 23126\n");
	fprintf(fp, "\n\n");
	fprintf(fp, "Test FOP:\n");
	fprintf(fp, "  - cons_seq : 1\n");
	/* Error introduced here */
	fprintf(fp, "cons_oid : 2\n");
	fprintf(fp, "    cons_test_type : d\n");
	fprintf(fp, "    cons_test_id : 64\n");
	fclose(fp);

	result = c2_cons_yaml_init(yaml_file);
	C2_UT_ASSERT(result != 0);
	result = remove(yaml_file);
	C2_UT_ASSERT(result == 0);
	file_redirect_fini();
}

static void yaml_root_get_test(void)
{
	FILE *fp;
	int   result;

	file_redirect_init();
        fp = fopen(yaml_file, "w");
        C2_UT_ASSERT(fp != NULL);
	fclose(fp);

	result = c2_cons_yaml_init(yaml_file);
	C2_UT_ASSERT(result != 0);
	result = remove(yaml_file);
	C2_UT_ASSERT(result == 0);
	file_redirect_fini();
}

static void yaml_get_value_test(void)
{
	uint32_t  number;
	int	  result;
	char	 *value;

	result = generate_yaml_file(yaml_file);
	C2_UT_ASSERT(result == 0);
	result = c2_cons_yaml_init(yaml_file);
	C2_UT_ASSERT(result == 0);

	value = c2_cons_yaml_get_value("server");
	C2_UT_ASSERT(value != NULL);
	C2_UT_ASSERT(strcmp("localhost", value) == 0);

	value = c2_cons_yaml_get_value("sport");
	C2_UT_ASSERT(value != NULL);
	number = strtoul(value, NULL, 10);
	C2_UT_ASSERT(number == 23125);

	value = c2_cons_yaml_get_value("client");
	C2_UT_ASSERT(value != NULL);
	C2_UT_ASSERT(strcmp("localhost", value) == 0);

	value = c2_cons_yaml_get_value("cport");
	C2_UT_ASSERT(value != NULL);
	number = strtoul(value, NULL, 10);
	C2_UT_ASSERT(number == 23126);

	value = c2_cons_yaml_get_value("cons_seq");
	C2_UT_ASSERT(value != NULL);
	number = strtoul(value, NULL, 10);
	C2_UT_ASSERT(number == 1);

	value = c2_cons_yaml_get_value("cons_oid");
	C2_UT_ASSERT(value != NULL);
	number = strtoul(value, NULL, 10);
	C2_UT_ASSERT(number == 2);

	value = c2_cons_yaml_get_value("cons_test_type");
	C2_UT_ASSERT(value != NULL);
	C2_UT_ASSERT(value[0] == 'd');

	value = c2_cons_yaml_get_value("cons_test_id");
	C2_UT_ASSERT(value != NULL);
	number = strtoul(value, NULL, 10);
	C2_UT_ASSERT(number == 64);

	value = c2_cons_yaml_get_value("xxxx");
	C2_UT_ASSERT(value == NULL);

	c2_cons_yaml_fini();
	result = remove(yaml_file);
	C2_UT_ASSERT(result == 0);
}


static int device_yaml_file(const char *name)
{
	FILE *fp;

	C2_PRE(name != NULL);

        fp = fopen(name, "w");
        if (fp == NULL) {
                fprintf(stderr, "Failed to create yaml file\n");
                return -errno;
        }

	fprintf(fp, "# Generated yaml file for console UT\n\n");
	fprintf(fp, "server  : localhost\n");
	fprintf(fp, "sport   : 23125\n");
	fprintf(fp, "client  : localhost\n");
	fprintf(fp, "cport   : 23126\n");
	fprintf(fp, "\n\n");
	fprintf(fp, "Test FOP:\n");
	fprintf(fp, "  - cons_seq : 1\n");
	fprintf(fp, "    cons_oid : 2\n");
	fprintf(fp, "    cons_notify_type : 0\n");
	fprintf(fp, "    cons_dev_id : 64\n");
	fprintf(fp, "    cons_size : 8\n");
	fprintf(fp, "    cons_buf  : console\n");

	fclose(fp);
	return 0;
}

static void cons_client_init(struct c2_rpc_client_ctx *cctx)
{
	int result;

	/* Init Test */
	result = device_yaml_file(yaml_file);
	C2_UT_ASSERT(result == 0);
	result = c2_cons_yaml_init(yaml_file);
	C2_UT_ASSERT(result == 0);
	result = c2_rpc_client_init(cctx);
	C2_UT_ASSERT(result == 0);
}

static void cons_client_fini(struct c2_rpc_client_ctx *cctx)
{
	int result;

	/* Fini Test */
	result = c2_rpc_client_fini(cctx);
	C2_UT_ASSERT(result == 0);
	c2_cons_yaml_fini();
	result = remove(yaml_file);
	C2_UT_ASSERT(result == 0);
}

static void cons_server_init(struct c2_rpc_server_ctx *sctx)
{
	int result;

	result = c2_rpc_server_start(sctx);
	C2_UT_ASSERT(result == 0);
}

static void cons_server_fini(struct c2_rpc_server_ctx *sctx)
{
	c2_rpc_server_stop(sctx);
}

static void conn_basic_test(void)
{
	cons_server_init(&sctx);
	cons_client_init(&cctx);
	cons_client_fini(&cctx);
	cons_server_fini(&sctx);
}

static void success_client(int dummy)
{
	cons_client_init(&cctx);
	cons_client_fini(&cctx);
}

static void conn_success_test(void)
{
	struct c2_thread client_handle;
	int		 result;

	cons_server_init(&sctx);
	C2_SET0(&client_handle);
	result = C2_THREAD_INIT(&client_handle, int, NULL, &success_client,
				0, "console-client");
	C2_UT_ASSERT(result == 0);
	c2_thread_join(&client_handle);
	c2_thread_fini(&client_handle);
	cons_server_fini(&sctx);
}

static void mesg_send_client(int dummy)
{
	struct c2_fop_type *ftype;
	struct c2_fop	   *fop;
	int		    result;

	cons_client_init(&cctx);

	ftype = c2_cons_fop_type_find(C2_CONS_FOP_DEVICE_OPCODE);
	C2_UT_ASSERT(ftype != NULL);
	c2_cons_fop_name_print(ftype);
	printf("\n");
	fop = c2_fop_alloc(ftype, NULL);
	C2_UT_ASSERT(fop != NULL);
	c2_cons_fop_obj_input(fop);
	result = c2_rpc_client_call(fop, &cctx.rcx_session,
				    &c2_fop_default_item_ops, CONNECT_TIMEOUT);
	C2_UT_ASSERT(result == 0);

	cons_client_fini(&cctx);
}

static void mesg_send_test(void)
{
	struct c2_thread client_handle;
	int		 result;

	file_redirect_init();
	cons_server_init(&sctx);
	C2_SET0(&client_handle);
	result = C2_THREAD_INIT(&client_handle, int, NULL, &mesg_send_client,
				0, "console-client");
	C2_UT_ASSERT(result == 0);
	c2_thread_join(&client_handle);
	c2_thread_fini(&client_handle);
	cons_server_fini(&sctx);
	file_redirect_fini();
}

static int console_cmd(const char *name, ...)
{
        va_list      list;
        va_list      clist;
        int          argc = 0;
	int	     result;
        const char **argv;
        const char **argp;
        const char  *arg;

        va_start(list, name);
        va_copy(clist, list);

        /* Count number of arguments */
        do {
                arg = va_arg(clist, const char *);
                argc++;
        } while(arg);
        va_end(clist);

        /* Allocate memory for pointer array */
        argp = argv = c2_alloc((argc + 1) * sizeof(const char *));
        C2_UT_ASSERT(argv != NULL);
	argv[argc] = NULL;

        /* Init list to array */
        *argp++ = name;
        do {
                arg = va_arg(list, const char *);
                *argp++ = arg;
        } while (arg);
        va_end(list);

        result = console_main(argc, (char **)argv);

	/* free memory allocated for argv */
	c2_free(argv);
	return result;
}

static void console_input_test(void)
{
	int  result;
	char buf[35];

	file_redirect_init();
	/* starts UT test for console main */
	result = console_cmd("no_input", NULL);
	C2_UT_ASSERT(result == EX_USAGE);
	C2_UT_ASSERT(c2_error_mesg_match(stderr, usage_msg));
	result = truncate(err_file, 0L);
	C2_UT_ASSERT(result == 0);
	fseek(stderr, 0L, SEEK_SET);

	result = console_cmd("no_input", "-v", NULL);
	C2_UT_ASSERT(result == EX_USAGE);
	C2_UT_ASSERT(c2_error_mesg_match(stderr, usage_msg));
	result = truncate(err_file, 0L);
	C2_UT_ASSERT(result == 0);
	fseek(stderr, 0L, SEEK_SET);

	fseek(stdout, 0L, SEEK_SET);
	result = console_cmd("list_fops", "-l", NULL);
	C2_UT_ASSERT(result == EX_USAGE);
	C2_UT_ASSERT(c2_error_mesg_match(stdout, "List of FOP's:"));
	result = truncate(out_file, 0L);
	C2_UT_ASSERT(result == 0);
	fseek(stdout, 0L, SEEK_SET);

	sprintf(buf, "%d", C2_CONS_FOP_DEVICE_OPCODE);
	result = console_cmd("show_fops", "-l", "-f", buf, NULL);
	C2_UT_ASSERT(result == EX_OK);
	sprintf(buf, "%.2d, Device Failed",
		     C2_CONS_FOP_DEVICE_OPCODE);
	C2_UT_ASSERT(c2_error_mesg_match(stdout, buf));
	result = truncate(out_file, 0L);
	C2_UT_ASSERT(result == 0);
	fseek(stdout, 0L, SEEK_SET);

	sprintf(buf, "%d", C2_CONS_FOP_REPLY_OPCODE);
	result = console_cmd("show_fops", "-l", "-f", buf, NULL);
	C2_UT_ASSERT(result == EX_OK);
	sprintf(buf, "%.2d, Console Reply",
		     C2_CONS_FOP_REPLY_OPCODE);
	C2_UT_ASSERT(c2_error_mesg_match(stdout, buf));
	result = truncate(out_file, 0L);
	C2_UT_ASSERT(result == 0);
	fseek(stdout, 0L, SEEK_SET);

	result = console_cmd("show_fops", "-l", "-f", 0, NULL);
	C2_UT_ASSERT(result == EX_USAGE);
	C2_UT_ASSERT(c2_error_mesg_match(stderr, usage_msg));
	result = truncate(err_file, 0L);
	C2_UT_ASSERT(result == 0);
	fseek(stderr, 0L, SEEK_SET);

	result = console_cmd("yaml_input", "-i", NULL);
	C2_UT_ASSERT(result == EX_USAGE);
	C2_UT_ASSERT(c2_error_mesg_match(stderr, usage_msg));
	result = truncate(err_file, 0L);
	C2_UT_ASSERT(result == 0);
	fseek(stderr, 0L, SEEK_SET);

	result = console_cmd("yaml_input", "-y", yaml_file, NULL);
	C2_UT_ASSERT(result == EX_USAGE);
	C2_UT_ASSERT(c2_error_mesg_match(stderr, usage_msg));
	result = truncate(err_file, 0L);
	C2_UT_ASSERT(result == 0);
	fseek(stderr, 0L, SEEK_SET);

	/* last UT test for console main */
	result = console_cmd("yaml_input", "-i", "-y", yaml_file, NULL);
	C2_UT_ASSERT(result == EX_NOINPUT);
	C2_UT_ASSERT(c2_error_mesg_match(stderr, "YAML Init failed"));

	file_redirect_fini();
}

const struct c2_test_suite console_ut = {
        .ts_name = "libconsole-ut",
        .ts_init = cons_init,
        .ts_fini = cons_fini,
        .ts_tests = {
		{ "yaml_basic_test", yaml_basic_test },
		{ "fop_iterator_test", fop_iterator_test },
                { "input_test", input_test },
                { "console_input_test", console_input_test },
                { "output_test", output_test },
                { "yaml_file_test", yaml_file_test },
                { "yaml_parser_test", yaml_parser_test },
                { "yaml_root_get_test", yaml_root_get_test },
                { "yaml_get_value_test", yaml_get_value_test },
                { "conn_basic_test", conn_basic_test },
                { "conn_success_test", conn_success_test },
                { "mesg_send_test", mesg_send_test },
		{ NULL, NULL }
	}
};

#undef CONSOLE_UT

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
