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

#ifndef CONSOLE_UT
#define CONSOLE_UT
#endif

#include <sysexits.h>

#include "lib/types.h"            /* uint64_t */
#include "lib/ut.h"
#include "lib/assert.h"
#include "lib/ub.h"
#include "lib/memory.h"
#include "fop/fop_iterator.h"
#include "lib/errno.h"            /* ETIMEDOUT */
#include "net/bulk_sunrpc.h"      /* bulk transport */
#include "colibri/init.h"         /* c2_init */
#include "lib/processor.h"        /* c2_processors_init/fini */
#include "lib/thread.h"		  /* c2_thread */
#include "lib/misc.h"		  /* C2_SET0 */

#include "console/console.h"
#include "console/console_u.h"
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
	MAXLINE = 1025
};

const char *yaml_file = "/tmp/console_ut.yaml";
const char *err_file = "/tmp/stderr";
const char *out_file = "/tmp/stdout";
const char *in_file = "/tmp/stdin";

static int cons_init(void)
{
	int result;

	timeout = 5;
	result = c2_console_fop_init();
        C2_ASSERT(result == 0);
	result = c2_processors_init();
	C2_ASSERT(result == 0);

	return result;
}

static int cons_fini(void)
{
	c2_processors_fini();
        c2_console_fop_fini();
	return 0;
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
	fprintf(fp, "  - ff_seq : 1\n");
	fprintf(fp, "    ff_oid : 2\n");
	fprintf(fp, "    cons_test_type : d\n");
	fprintf(fp, "    cons_test_id : 64\n");

	fclose(fp);
	return 0;
}

static void init_test_fop(struct c2_cons_fop_test *fop)
{
	fop->cons_id.ff_seq = 1;
        fop->cons_id.ff_oid = 2;
	fop->cons_test_type = 'd';
	fop->cons_test_id = 64;
}

static void check_values(struct c2_fit *it)
{
        struct c2_fit_yield       yield;
	struct c2_fid		 *fid;
	char			 *data;
	uint64_t		 *value;
	int			  result;

	result = c2_fit_yield(it, &yield);
	C2_UT_ASSERT(result != 0);
	fid = (struct c2_fid *)yield.fy_val.ffi_val;
	C2_UT_ASSERT(fid->f_container == 1);
	C2_UT_ASSERT(fid->f_key == 2);

	result = c2_fit_yield(it, &yield);
	C2_UT_ASSERT(result != 0);
	data = (char *)yield.fy_val.ffi_val;
	C2_UT_ASSERT(*data == 'd');

	result = c2_fit_yield(it, &yield);
	C2_UT_ASSERT(result != 0);
	value = (uint64_t *)yield.fy_val.ffi_val;
	C2_UT_ASSERT(*value == 64);
}

static void fop_iterator_test(void)
{
        struct c2_cons_fop_test *fop;
        struct c2_fop           *f;
        struct c2_fit            it;

        f = c2_fop_alloc(&c2_cons_fop_test_fopt, NULL);
        C2_UT_ASSERT(f != NULL);
        fop = c2_fop_data(f);

        c2_fop_all_object_it_init(&it, f);
	init_test_fop(fop);
	check_values(&it);

        c2_fop_all_object_it_fini(&it);
        c2_fop_free(f);
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
        struct c2_cons_fop_test *fop;
        struct c2_fop           *f;
        struct c2_fit            it;
	int			 result;

	result = generate_yaml_file(yaml_file);
	C2_UT_ASSERT(result == 0);
	result = c2_cons_yaml_init(yaml_file);
	C2_UT_ASSERT(result == 0);

        f = c2_fop_alloc(&c2_cons_fop_test_fopt, NULL);
        C2_UT_ASSERT(f != NULL);
        fop = c2_fop_data(f);

        c2_fop_all_object_it_init(&it, f);
        c2_cons_fop_obj_input(&it);
	c2_fop_it_reset(&it);
	check_values(&it);

        c2_fop_all_object_it_fini(&it);
        c2_fop_free(f);
	c2_cons_yaml_fini();
	result = remove(yaml_file);
	C2_UT_ASSERT(result == 0);
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
        struct c2_cons_fop_test *fop;
        struct c2_fop           *f;
        struct c2_fit            it;
	FILE			*fp;
	int			 fd;
	int			 result;

	verbose = true;
	result = generate_yaml_file(yaml_file);
	C2_UT_ASSERT(result == 0);
	result = c2_cons_yaml_init(yaml_file);
	C2_UT_ASSERT(result == 0);

        f = c2_fop_alloc(&c2_cons_fop_test_fopt, NULL);
        C2_UT_ASSERT(f != NULL);
        fop = c2_fop_data(f);

	/* save fd of stdout */
	fd = dup(fileno(stdout));
	C2_UT_ASSERT(fd != -1);

	/* redirect stdout */
	fp = freopen(in_file, "w+", stdout);
	C2_UT_ASSERT(fp != NULL);

        c2_fop_all_object_it_init(&it, f);
        c2_cons_fop_obj_input(&it);

	/* again redirect stdout */
	fp = freopen(out_file, "w+", fp);
	C2_UT_ASSERT(fp != NULL);

	c2_fop_it_reset(&it);
	c2_cons_fop_obj_output(&it);

	/* file cleanup */
	fclose(fp);
	/* restore stdout */
	fd = dup2(fd, 1);
	C2_UT_ASSERT(fd != -1);
	stdout = fdopen(fd, "a+");
	C2_UT_ASSERT(stdout != NULL);

	file_compare(in_file, out_file);

	result = remove(out_file);
	C2_UT_ASSERT(result == 0);
	result = remove(in_file);
	C2_UT_ASSERT(result == 0);

	verbose = false;
        c2_fop_all_object_it_fini(&it);
        c2_fop_free(f);
	c2_cons_yaml_fini();
	result = remove(yaml_file);
	C2_UT_ASSERT(result == 0);
}

static void yaml_file_test(void)
{
	int result;

	result = c2_cons_yaml_init(yaml_file);
	C2_UT_ASSERT(result != 0);
}

static void yaml_parser_test(void)
{
	FILE *fp;
	int   result;

        fp = fopen(yaml_file, "w");
        C2_UT_ASSERT(fp != NULL);
	fprintf(fp, "# Generated yaml file for console UT\n\n");
	fprintf(fp, "server  : localhost\n");
	fprintf(fp, "sport   : 23125\n");
	fprintf(fp, "client  : localhost\n");
	fprintf(fp, "cport   : 23126\n");
	fprintf(fp, "\n\n");
	fprintf(fp, "Test FOP:\n");
	fprintf(fp, "  - ff_seq : 1\n");
	/* Error introduced here */
	fprintf(fp, "ff_oid : 2\n");
	fprintf(fp, "    cons_test_type : d\n");
	fprintf(fp, "    cons_test_id : 64\n");
	fclose(fp);

	result = c2_cons_yaml_init(yaml_file);
	C2_UT_ASSERT(result != 0);
	result = remove(yaml_file);
	C2_UT_ASSERT(result == 0);
}

static void yaml_root_get_test(void)
{
	FILE *fp;
	int   result;

        fp = fopen(yaml_file, "w");
        C2_UT_ASSERT(fp != NULL);
	fclose(fp);

	result = c2_cons_yaml_init(yaml_file);
	C2_UT_ASSERT(result != 0);
	result = remove(yaml_file);
	C2_UT_ASSERT(result == 0);
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

	value = c2_cons_yaml_get_value("ff_seq");
	C2_UT_ASSERT(value != NULL);
	number = strtoul(value, NULL, 10);
	C2_UT_ASSERT(number == 1);

	value = c2_cons_yaml_get_value("ff_oid");
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


static int disk_yaml_file(const char *name)
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
	fprintf(fp, "  - ff_seq : 1\n");
	fprintf(fp, "    ff_oid : 2\n");
	fprintf(fp, "    cons_notify_type : 0\n");
	fprintf(fp, "    cons_disk_id : 64\n");

	fclose(fp);
	return 0;
}

static void cons_client_init(struct c2_console *cons)
{
	int result;

	/* Init Test */
	result = disk_yaml_file(yaml_file);
	C2_UT_ASSERT(result == 0);
	result = c2_cons_yaml_init(yaml_file);
	C2_UT_ASSERT(result == 0);
	result = c2_cons_mesg_init();
	C2_UT_ASSERT(result == 0);
	result = c2_cons_rpc_client_init(cons);
	C2_UT_ASSERT(result == 0);
}

static void cons_client_fini(struct c2_console *cons)
{
	int result;

	/* Fini Test */
	c2_cons_rpc_client_fini(cons);
	c2_cons_mesg_fini();
	c2_cons_yaml_fini();
	result = remove(yaml_file);
	C2_UT_ASSERT(result == 0);
}

static void cons_server_init(struct c2_console *cons)
{
	int result;

	result = c2_cons_rpc_server_init(cons);
	C2_UT_ASSERT(result == 0);
}

static void cons_server_fini(struct c2_console *cons)
{
	c2_cons_rpc_server_fini(cons);
}

static void conn_basic_test(void)
{
	struct c2_console client = {
		.cons_lhost	      = "localhost",
		.cons_lport	      = CLIENT_PORT,
		.cons_rhost	      = "localhost",
		.cons_rport	      = CLIENT_PORT,
		.cons_db_name	      = "cons_client_db",
		.cons_cob_dom_id      = { .id = 14 },
		.cons_nr_slots	      = NR_SLOTS,
		.cons_rid	      = 1,
		.cons_xprt	      = &c2_net_bulk_sunrpc_xprt,
		.cons_items_in_flight = MAX_RPCS_IN_FLIGHT
	};

	struct c2_console server = {
		.cons_lhost	      = "localhost",
		.cons_lport	      = CLIENT_PORT,
		.cons_rhost	      = "localhost",
		.cons_rport	      = CLIENT_PORT,
		.cons_db_name	      = "cons_server_db",
		.cons_cob_dom_id      = { .id = 15 },
		.cons_nr_slots	      = NR_SLOTS,
		.cons_rid	      = 2,
		.cons_xprt	      = &c2_net_bulk_sunrpc_xprt,
		.cons_items_in_flight = MAX_RPCS_IN_FLIGHT
	};
	int		result;

	cons_server_init(&server);
	cons_client_init(&client);
	result = c2_cons_rpc_client_connect(&client);
	C2_UT_ASSERT(result == 0);
	result = c2_cons_rpc_client_disconnect(&client);
	C2_UT_ASSERT(result == 0);
	cons_client_fini(&client);
	cons_server_fini(&server);
}

static void success_client(int dummy)
{
	struct c2_console client = {
		.cons_lhost	      = "localhost",
		.cons_lport	      = 23124,
		.cons_rhost	      = "localhost",
		.cons_rport	      = 23124,
		.cons_db_name	      = "cons_client_db",
		.cons_cob_dom_id      = { .id = 14 },
		.cons_nr_slots	      = NR_SLOTS,
		.cons_rid	      = 1,
		.cons_xprt	      = &c2_net_bulk_sunrpc_xprt,
		.cons_items_in_flight = MAX_RPCS_IN_FLIGHT
	};
	int		result;

	cons_client_init(&client);
	result = c2_cons_rpc_client_connect(&client);
	C2_UT_ASSERT(result == 0);
	result = c2_cons_rpc_client_disconnect(&client);
	C2_UT_ASSERT(result == 0);
	cons_client_fini(&client);
}

static void conn_success_test(void)
{
	struct c2_console server = {
		.cons_lhost	      = "localhost",
		.cons_lport	      = 23124,
		.cons_rhost	      = "localhost",
		.cons_rport	      = 23124,
		.cons_db_name	      = "cons_server_db",
		.cons_cob_dom_id      = { .id = 15 },
		.cons_nr_slots	      = NR_SLOTS,
		.cons_rid	      = 2,
		.cons_xprt	      = &c2_net_bulk_sunrpc_xprt,
		.cons_items_in_flight = MAX_RPCS_IN_FLIGHT
	};
	struct c2_thread client_handle;
	int		 result;

	cons_server_init(&server);
	C2_SET0(&client_handle);
	result = C2_THREAD_INIT(&client_handle, int, NULL, &success_client,
				0, "console-client");
	C2_UT_ASSERT(result == 0);
	c2_thread_join(&client_handle);
	c2_thread_fini(&client_handle);
	cons_server_fini(&server);
}

static void mesg_send_client(int dummy)
{
	struct c2_console client = {
		.cons_lhost	      = "localhost",
		.cons_lport	      = 23126,
		.cons_rhost	      = "localhost",
		.cons_rport	      = 23126,
		.cons_db_name	      = "cons_client_db",
		.cons_cob_dom_id      = { .id = 14 },
		.cons_nr_slots	      = NR_SLOTS,
		.cons_rid	      = 1,
		.cons_xprt	      = &c2_net_bulk_sunrpc_xprt,
		.cons_items_in_flight = MAX_RPCS_IN_FLIGHT
	};
	struct c2_cons_mesg *mesg;
	c2_time_t	     deadline;
	int		     result;

	cons_client_init(&client);
	result = c2_cons_rpc_client_connect(&client);
	C2_UT_ASSERT(result == 0);

	deadline = c2_cons_timeout_construct(10);
	mesg = c2_cons_mesg_get(CMT_DISK_FAILURE);
	mesg->cm_rpc_mach = &client.cons_rpc_mach;
	mesg->cm_rpc_session = &client.cons_rpc_session;
	c2_cons_mesg_name_print(mesg);
	printf("\n");
	result = c2_cons_mesg_send(mesg, deadline);
	C2_UT_ASSERT(result == 0);

	result = c2_cons_rpc_client_disconnect(&client);
	C2_UT_ASSERT(result == 0);
	cons_client_fini(&client);
}

static void mesg_send_test(void)
{
	struct c2_console server = {
		.cons_lhost	      = "localhost",
		.cons_lport	      = 23126,
		.cons_rhost	      = "localhost",
		.cons_rport	      = 23126,
		.cons_db_name	      = "cons_server_db",
		.cons_cob_dom_id      = { .id = 15 },
		.cons_nr_slots	      = NR_SLOTS,
		.cons_rid	      = 2,
		.cons_xprt	      = &c2_net_bulk_sunrpc_xprt,
		.cons_items_in_flight = MAX_RPCS_IN_FLIGHT
	};
	struct c2_thread client_handle;
	int		 result;

	cons_server_init(&server);
	C2_SET0(&client_handle);
	result = C2_THREAD_INIT(&client_handle, int, NULL, &mesg_send_client,
				0, "console-client");
	C2_UT_ASSERT(result == 0);
	c2_thread_join(&client_handle);
	c2_thread_fini(&client_handle);
	cons_server_fini(&server);
}

static int console_cmd(const char *name, ...)
{
        va_list      list;
        va_list      clist;
        int          argc = 0;
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

        return console_main(argc, (char **)argv);
}

static int error_mesg_match(FILE *fp, const char *mesg)
{
	char line[MAXLINE];

	C2_PRE(fp != NULL);
	C2_PRE(mesg != NULL);

	fseek(fp, 0L, SEEK_SET);
	memset(line, '\0', MAXLINE);
	while (fgets(line, MAXLINE, fp) != NULL) {
		if (strncmp(mesg, line, strlen(mesg)) == 0)
			return 0;
	}
	return -EINVAL;
}

const char *umesg = "c2console :"
		    " {-l FOP list | -f FOP type}"
		    " [-s server] [-p server port]"
		    " [-c client] [-P client port]"
		    " [-t timeout]"
		    " [[-i] [-y yaml file path]]"
		    " [-v]";

static void console_input_test(void)
{
	FILE *err_fp;
	FILE *out_fp;
	int   err_fd;
	int   out_fd;
	int   result;
	char  buf[35];

	/* save file fds of stderr and stdout */
	err_fd = dup(fileno(stderr));
	C2_UT_ASSERT(err_fd != -1);
	out_fd = dup(fileno(stdout));
	C2_UT_ASSERT(out_fd != -1);

	/* redirect stderr and stdout */
	err_fp = freopen(err_file, "w+", stderr);
	C2_UT_ASSERT(err_fp != NULL);
	out_fp = freopen(out_file, "w+", stdout);
	C2_UT_ASSERT(out_fp != NULL);

	/* starts UT test for console main */
	result = console_cmd("no_input", NULL);
	C2_UT_ASSERT(result == EX_USAGE);
	result = error_mesg_match(err_fp, umesg);
	C2_UT_ASSERT(result == 0);
	truncate(err_file, 0L);
	fseek(err_fp, 0L, SEEK_SET);

	result = console_cmd("no_input", "-v", NULL);
	C2_UT_ASSERT(result == EX_USAGE);
	result = error_mesg_match(err_fp, umesg);
	C2_UT_ASSERT(result == 0);
	truncate(err_file, 0L);
	fseek(err_fp, 0L, SEEK_SET);

	fseek(out_fp, 0L, SEEK_SET);
	result = console_cmd("list_fops", "-l", NULL);
	C2_UT_ASSERT(result == EX_USAGE);
	result = error_mesg_match(out_fp, "List of FOP's:");
	C2_UT_ASSERT(result == 0);
	truncate(out_file, 0L);
	fseek(out_fp, 0L, SEEK_SET);

	result = console_cmd("show_fops", "-l", "-f", "0", NULL);
	C2_UT_ASSERT(result == EX_OK);
	result = error_mesg_match(out_fp, "Info for \"00 Disk FOP Message\"");
	C2_UT_ASSERT(result == 0);
	truncate(out_file, 0L);
	fseek(out_fp, 0L, SEEK_SET);

	result = console_cmd("show_fops", "-l", "-f", "1", NULL);
	C2_UT_ASSERT(result == EX_OK);
	result = error_mesg_match(out_fp, "Info for \"01 Device FOP Message\"");
	C2_UT_ASSERT(result == 0);
	truncate(out_file, 0L);
	fseek(out_fp, 0L, SEEK_SET);

	result = console_cmd("show_fops", "-l", "-f", "2", NULL);
	C2_UT_ASSERT(result == EX_OK);
	result = error_mesg_match(out_fp, "Info for \"02 Reply FOP Message\"");
	C2_UT_ASSERT(result == 0);
	truncate(out_file, 0L);
	fseek(out_fp, 0L, SEEK_SET);

	sprintf(buf, "%d", CMT_MESG_NR);
	result = console_cmd("show_fops", "-l", "-f", buf, NULL);
	C2_UT_ASSERT(result == EX_USAGE);
	result = error_mesg_match(err_fp, umesg);
	C2_UT_ASSERT(result == 0);
	truncate(err_file, 0L);
	fseek(err_fp, 0L, SEEK_SET);

	sprintf(buf, "%d", CMT_MESG_NR + 1);
	result = console_cmd("show_fops", "-l", "-f", buf, NULL);
	C2_UT_ASSERT(result == EX_USAGE);
	result = error_mesg_match(err_fp, umesg);
	C2_UT_ASSERT(result == 0);
	truncate(err_file, 0L);
	fseek(err_fp, 0L, SEEK_SET);

	result = console_cmd("yaml_input", "-i", NULL);
	C2_UT_ASSERT(result == EX_USAGE);
	result = error_mesg_match(err_fp, umesg);
	C2_UT_ASSERT(result == 0);
	truncate(err_file, 0L);
	fseek(err_fp, 0L, SEEK_SET);

	result = console_cmd("yaml_input", "-y", yaml_file, NULL);
	C2_UT_ASSERT(result == EX_USAGE);
	result = error_mesg_match(err_fp, umesg);
	C2_UT_ASSERT(result == 0);
	truncate(err_file, 0L);
	fseek(err_fp, 0L, SEEK_SET);

	/* last UT test for console main */
	result = console_cmd("yaml_input", "-i", "-y", yaml_file, NULL);
	C2_UT_ASSERT(result == EX_NOINPUT);
	result = error_mesg_match(err_fp, "YAML Init failed");
	C2_UT_ASSERT(result == 0);

	/* file cleanup */
	fclose(err_fp);
	fclose(out_fp);
	result = remove(err_file);
	C2_UT_ASSERT(result == 0);
	result = remove(out_file);
	C2_UT_ASSERT(result == 0);

	/* restore stderr and stdout */
	err_fd = dup2(err_fd, 2);
	C2_UT_ASSERT(err_fd != -1);
	stderr = fdopen(err_fd, "a+");
	C2_UT_ASSERT(stderr != NULL);
	out_fd = dup2(out_fd, 1);
	C2_UT_ASSERT(out_fd != -1);
	stdout = fdopen(out_fd, "a+");
	C2_UT_ASSERT(stdout != NULL);
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
