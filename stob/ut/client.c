#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <string.h>    /* memset */
#include <stdio.h>     /* feof, fscanf, ... */
#include <err.h>
#include <errno.h>

#include "lib/assert.h"
#include "lib/memory.h"
#include "net/net.h"
#include "net/usunrpc/usunrpc.h"

#include "stob/stob.h"
#include "stob/linux.h"
#include "colibri/init.h"

#include "io_u.h"
#include "io_fop.h"

/**
   @addtogroup stob
   @{
 */

static int netcall(struct c2_net_conn *conn, struct c2_fop *arg, 
		   struct c2_fop *ret)
{
	struct c2_net_call call = {
		.ac_arg = arg,
		.ac_ret = ret
	};
	return c2_net_cli_call(conn, &call);
}

static void create_send(struct c2_net_conn *conn, const struct c2_fop_fid *fid)
{
	int result;
	struct c2_fop                    *f;
	struct c2_fop                    *r;
	struct c2_io_create     *fop;
	struct c2_io_create_rep *rep;

	f = c2_fop_alloc(&c2_io_create_fopt, NULL);
	fop = c2_fop_data(f);
	r = c2_fop_alloc(&c2_io_create_rep_fopt, NULL);
	rep = c2_fop_data(r);
	fop->sic_object = *fid;

	result = netcall(conn, f, r);
	printf("GOT: %i %i\n", result, rep->sicr_rc);
}

static void read_send(struct c2_net_conn *conn, const struct c2_fop_fid *fid)
{
	int result;
	struct c2_fop                    *f;
	struct c2_fop                    *r;
	struct c2_io_read       *fop;
	struct c2_io_read_rep   *rep;
	int i;

	f = c2_fop_alloc(&c2_io_read_fopt, NULL);
	fop = c2_fop_data(f);
	r = c2_fop_alloc(&c2_io_read_rep_fopt, NULL);
	rep = c2_fop_data(r);

	fop->sir_object = *fid;
	if (scanf("%i", &i) != 1)
		err(1, "wrong count conversion");
	C2_ASSERT(i == 1);
	if (scanf("%llu %llu", 
		  (unsigned long long *)&fop->sir_seg.f_offset, 
		  (unsigned long long *)&fop->sir_seg.f_count) != 2) {
		err(1, "wrong offset conversion");
	}
	result = netcall(conn, f, r);
	C2_ASSERT(result == 0);
	printf("GOT: %i %i\n", rep->sirr_rc, rep->sirr_buf.cib_count);
	printf("\t[");
	for (i = 0; i < rep->sirr_buf.cib_count; ++i)
		printf("%02x", rep->sirr_buf.cib_value[i]);
	printf("]\n");
}

static void write_send(struct c2_net_conn *conn, const struct c2_fop_fid *fid)
{
	int result;
	struct c2_fop                    *f;
	struct c2_fop                    *r;
	struct c2_io_write      *fop;
	struct c2_io_write_rep  *rep;
	char filler;

	f = c2_fop_alloc(&c2_io_write_fopt, NULL);
	fop = c2_fop_data(f);
	r = c2_fop_alloc(&c2_io_write_rep_fopt, NULL);
	rep = c2_fop_data(r);

	memset(&rep, 0, sizeof rep);
	fop->siw_object = *fid;
	if (scanf("%i", &result) != 1)
		err(1, "wrong count conversion");
	C2_ASSERT(result == 1);
	if (scanf("%llu %u %c", 
		  (unsigned long long *)&fop->siw_offset, 
		  &fop->siw_buf.cib_count, &filler) != 3)
		err(1, "wrong offset conversion");
	fop->siw_buf.cib_value = c2_alloc(fop->siw_buf.cib_count);
	C2_ASSERT(fop->siw_buf.cib_value != NULL);
	memset(fop->siw_buf.cib_value, filler, fop->siw_buf.cib_count);

	result = netcall(conn, f, r);
	C2_ASSERT(result == 0);
	rep = c2_fop_data(r);

	printf("GOT: %i %i %i\n", result, rep->siwr_rc, rep->siwr_count);
}

static void quit_send(struct c2_net_conn *conn)
{
	struct c2_fop              *f;
	struct c2_fop              *r;
	struct c2_io_quit *fop;
	struct c2_io_quit *rep;
	int result;

	f = c2_fop_alloc(&c2_io_quit_fopt, NULL);
	fop = c2_fop_data(f);
	r = c2_fop_alloc(&c2_io_quit_fopt, NULL);
	rep = c2_fop_data(r);

	result = netcall(conn, f, r);
	C2_ASSERT(result == 0);
	rep = c2_fop_data(r);
	printf("GOT: %i %i\n", result, rep->siq_rc);
}

/**
   Simple client.

   Synopsis:

     client host port

   After connecting to the server, the client reads commands from the standard
   input. The following commands are supported:

       c <D1> <D2>

           Create an object with the fid (D1:D2)

       w <D1> <D2> <N> <OFF0> <COUNT0> <FILLER0> <OFF1> <COUNT1> <FILLER1> ...

           Write into an object with the fid (D1:D2) N buffers described by
           (offset, count, filler) triples that follow. Offset is the staring
           offset in the file, count is buffer size in bytes and filler is a
           character with which the buffer is filled.

       r <D1> <D2> <N> <OFF0> <COUNT0> <OFF1> <COUNT1> ...

           Read from an object with the fid (D1:D2) N buffers with given offsets
           and sizes.

       q <D1> <D2>

           Shutdown the server.
 */
int main(int argc, char **argv)
{
	int result;

	struct c2_service_id    sid = { .si_uuid = "UUURHG" };
	struct c2_net_domain    ndom;
	struct c2_net_conn     *conn;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	if (argc != 3) {
		fprintf(stderr, "%s host port\n", argv[0]);
		return -1;
	}

	result = c2_init();
	C2_ASSERT(result == 0);

	result = io_fop_init();
	C2_ASSERT(result == 0);

	result = c2_net_xprt_init(&c2_net_usunrpc_xprt);
	C2_ASSERT(result == 0);

	result = c2_net_domain_init(&ndom, &c2_net_usunrpc_xprt);
	C2_ASSERT(result == 0);

	result = c2_service_id_init(&sid, &ndom, argv[1], atoi(argv[2]));
	C2_ASSERT(result == 0);

	result = c2_net_conn_create(&sid);
	C2_ASSERT(result == 0);

	conn = c2_net_conn_find(&sid);
	C2_ASSERT(conn != NULL);

	while (!feof(stdin)) {
		struct c2_fop_fid fid;
		char cmd;
		int n;

		n = scanf("%c %lu %lu", &cmd, (unsigned long *)&fid.f_seq, 
			  (unsigned long *)&fid.f_oid);
		if (n != 3)
			err(1, "wrong conversion: %i", n);
		switch (cmd) {
		case 'c':
			create_send(conn, &fid);
			break;
		case 'r':
			read_send(conn, &fid);
			break;
		case 'w':
			write_send(conn, &fid);
			break;
		case 'q':
			quit_send(conn);
			break;
		default:
			err(1, "Unknown command '%c'", cmd);
		}
		n = scanf(" \n");
	}

	c2_net_conn_unlink(conn);
	c2_net_conn_release(conn);

	c2_service_id_fini(&sid);
	c2_net_domain_fini(&ndom);
	c2_net_xprt_fini(&c2_net_usunrpc_xprt);
	io_fop_fini();
	c2_fini();

	return 0;
}

int create_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	return 0;
}

int read_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	return 0;
}

int write_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	return 0;
}

int quit_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	return 0;
}


/** @} end group stob */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
