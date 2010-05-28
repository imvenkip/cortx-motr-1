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
#include "net/sunrpc/sunrpc.h"

#include "stob/stob.h"
#include "stob/linux.h"

#include "io_fop.h"

/**
   @addtogroup stob
   @{
 */

static struct c2_rpc_op_table *ops;

static const struct c2_rpc_op create_op = {
	.ro_op          = SIF_CREAT,
	.ro_arg_size    = sizeof(struct c2_stob_io_create_fop),
	.ro_xdr_arg     = (c2_xdrproc_t)xdr_c2_stob_io_create_fop,
	.ro_result_size = sizeof(struct c2_stob_io_create_rep_fop),
	.ro_xdr_result  = (c2_xdrproc_t)xdr_c2_stob_io_create_rep_fop,
	.ro_handler     = NULL
};

static const struct c2_rpc_op read_op = {
	.ro_op          = SIF_READ,
	.ro_arg_size    = sizeof(struct c2_stob_io_read_fop),
	.ro_xdr_arg     = (c2_xdrproc_t)xdr_c2_stob_io_read_fop,
	.ro_result_size = sizeof(struct c2_stob_io_read_rep_fop),
	.ro_xdr_result  = (c2_xdrproc_t)xdr_c2_stob_io_read_rep_fop,
	.ro_handler     = NULL
};

static const struct c2_rpc_op write_op = {
	.ro_op          = SIF_WRITE,
	.ro_arg_size    = sizeof(struct c2_stob_io_write_fop),
	.ro_xdr_arg     = (c2_xdrproc_t)xdr_c2_stob_io_write_fop,
	.ro_result_size = sizeof(struct c2_stob_io_write_rep_fop),
	.ro_xdr_result  = (c2_xdrproc_t)xdr_c2_stob_io_write_rep_fop,
	.ro_handler     = NULL
};

static const struct c2_rpc_op quit_op = {
	.ro_op          = SIF_QUIT,
	.ro_arg_size    = sizeof(int),
	.ro_xdr_arg     = (c2_xdrproc_t)xdr_int,
	.ro_result_size = sizeof(int),
	.ro_xdr_result  = (c2_xdrproc_t)xdr_int,
	.ro_handler     = NULL
};

static void create_send(struct c2_net_conn *conn, const struct c2_fid *fid)
{
	int result;
	struct c2_stob_io_create_fop fop;
	struct c2_stob_io_create_rep_fop rep;

	memset(&rep, 0, sizeof rep);
	fop.sic_object = *fid;
	result = c2_net_cli_call(conn, ops, SIF_CREAT, &fop, &rep);
	printf("GOT: %i %i\n", result, rep.sicr_rc);
}

static void read_send(struct c2_net_conn *conn, const struct c2_fid *fid)
{
	int result;
	struct c2_stob_io_read_fop fop;
	struct c2_stob_io_read_rep_fop rep;
	int i;

	memset(&rep, 0, sizeof rep);
	fop.sir_object = *fid;
	if (scanf("%i", &fop.sir_vec.v_count) != 1)
		err(1, "wrong count conversion");
	C2_ALLOC_ARR(fop.sir_vec.v_seg, fop.sir_vec.v_count);
	C2_ASSERT(fop.sir_vec.v_seg != NULL);
	for (i = 0; i < fop.sir_vec.v_count; ++i) {
		if (scanf("%lu %u", 
			  (unsigned long *)&fop.sir_vec.v_seg[i].f_offset,
			  &fop.sir_vec.v_seg[i].f_count) != 2)
			err(1, "wrong offset conversion");
	}
	result = c2_net_cli_call(conn, ops, SIF_READ, &fop, &rep);
	C2_ASSERT(result == 0);
	printf("GOT: %i %i %i\n", rep.sirr_rc, rep.sirr_count,
	       rep.sirr_buf.b_count);
	for (i = 0; i < rep.sirr_buf.b_count; ++i) {
		int j;
		struct c2_stob_io_buf *buf;

		buf = &rep.sirr_buf.b_buf[i];

		printf("\t%i[", buf->ib_count);
		for (j = 0; j < buf->ib_count; ++j)
			printf("%02x", buf->ib_value[j]);
		printf("]\n");
	}
}

static void write_send(struct c2_net_conn *conn, const struct c2_fid *fid)
{
	int result;
	int i;
	struct c2_stob_io_write_fop fop;
	struct c2_stob_io_write_rep_fop rep;

	memset(&rep, 0, sizeof rep);
	fop.siw_object = *fid;
	if (scanf("%i", &fop.siw_vec.v_count) != 1)
		err(1, "wrong count conversion");
	fop.siw_buf.b_count = fop.siw_vec.v_count;
	C2_ALLOC_ARR(fop.siw_vec.v_seg, fop.siw_vec.v_count);
	C2_ALLOC_ARR(fop.siw_buf.b_buf, fop.siw_buf.b_count);
	C2_ASSERT(fop.siw_vec.v_seg != NULL);
	C2_ASSERT(fop.siw_buf.b_buf != NULL);
	for (i = 0; i < fop.siw_vec.v_count; ++i) {
		char filler;
		struct c2_stob_io_buf *buf;

		buf = &fop.siw_buf.b_buf[i];
		if (scanf("%lu %u %c", 
			  (unsigned long *)&fop.siw_vec.v_seg[i].f_offset,
			  &fop.siw_vec.v_seg[i].f_count, &filler) != 3)
			err(1, "wrong offset conversion");
		buf->ib_count = fop.siw_vec.v_seg[i].f_count;
		buf->ib_value = c2_alloc(buf->ib_count);
		C2_ASSERT(buf->ib_value != NULL);
		memset(buf->ib_value, filler, buf->ib_count);
	}

	result = c2_net_cli_call(conn, ops, SIF_WRITE, &fop, &rep);

	printf("GOT: %i %i %i\n", result, rep.siwr_rc, rep.siwr_count);
}

static void quit_send(struct c2_net_conn *conn)
{
	int fop;
	int rep;
	int result;

	fop = rep = 0;
	result = c2_net_cli_call(conn, ops, SIF_QUIT, &fop, &rep);
	printf("GOT: %i %i\n", result, rep);
}

static void ping_send(struct c2_net_conn *conn)
{
	int result;

	result = c2_net_cli_call(conn, ops, NULLPROC, NULL, NULL);
	printf("Ping back: %i \n", result);
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

	result = c2_net_init();
	C2_ASSERT(result == 0);

	result = c2_net_xprt_init(&c2_net_user_sunrpc_xprt);
	C2_ASSERT(result == 0);

	result = c2_net_domain_init(&ndom, &c2_net_user_sunrpc_xprt);
	C2_ASSERT(result == 0);

	result = c2_service_id_init(&sid, &ndom, argv[1], atoi(argv[2]));
	C2_ASSERT(result == 0);

	c2_rpc_op_table_init(&ops);
	C2_ASSERT(ops != NULL);

	result = c2_rpc_op_register(ops, &create_op);
	C2_ASSERT(result == 0);
	result = c2_rpc_op_register(ops, &read_op);
	C2_ASSERT(result == 0);
	result = c2_rpc_op_register(ops, &write_op);
	C2_ASSERT(result == 0);
	result = c2_rpc_op_register(ops, &quit_op);
	C2_ASSERT(result == 0);

	result = c2_net_conn_create(&sid);
	C2_ASSERT(result == 0);

	conn = c2_net_conn_find(&sid);
	C2_ASSERT(conn != NULL);

	ping_send(conn);
	while (!feof(stdin)) {
		struct c2_fid fid;
		char cmd;
		int n;

		n = scanf("%c %lu %lu", &cmd, (unsigned long *)&fid.f_d1, (unsigned long *)&fid.f_d2);
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
		scanf(" \n");
	}

	c2_net_conn_unlink(conn);
	c2_net_conn_release(conn);

	c2_rpc_op_table_fini(ops);

	c2_service_id_fini(&sid);
	c2_net_domain_fini(&ndom);
	c2_net_xprt_fini(&c2_net_user_sunrpc_xprt);
	c2_net_fini();

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
