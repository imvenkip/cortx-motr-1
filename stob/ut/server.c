#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>    /* memset */
#include <sys/stat.h>  /* mkdir */
#include <sys/types.h> /* mkdir */
#include <unistd.h>    /* sleep */
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

static struct c2_stob_domain *dom;

static struct c2_stob *object_find(const struct c2_fid *fid)
{
	struct c2_stob_id  id;
	struct c2_stob    *obj;
	int result;

	id.si_seq = fid->f_d1;
	id.si_id  = fid->f_d2;
	result = dom->sd_ops->sdo_stob_find(dom, &id, &obj);
	C2_ASSERT(result == 0);
	result = c2_stob_locate(obj);
	return obj;
}

static bool create_handler(const struct c2_rpc_op *op, void *arg, void **ret)
{
	struct c2_stob_io_create_fop     *in = arg;
	struct c2_stob_io_create_rep_fop *ex;
	struct c2_stob                   *obj;
	bool result;

	C2_ALLOC_PTR(ex);
	C2_ASSERT(ex != NULL);

	obj = object_find(&in->sic_object);
	result = c2_stob_create(obj);
	C2_ASSERT(result == 0);
	ex->sicr_rc = 0;
	*ret = ex;
	return true;
}

static bool read_handler(const struct c2_rpc_op *op, void *arg, void **ret)
{
	struct c2_stob_io_read_fop     *in = arg;
	struct c2_stob_io_read_rep_fop *ex;
	struct c2_stob                 *obj;
	c2_bcount_t                    *count;
	c2_bindex_t                    *offset;
	void                          **buf;
	uint32_t                        nr;
	uint32_t                        i;
	struct c2_stob_io               io;
	struct c2_clink                 clink;
	bool result;

	C2_ALLOC_PTR(ex);
	C2_ASSERT(ex != NULL);

	obj = object_find(&in->sir_object);
	nr = in->sir_vec.v_count;
	C2_ASSERT(nr > 0);
	ex->sirr_buf.b_count = nr;

	C2_ALLOC_ARR(count, nr);
	C2_ALLOC_ARR(offset, nr);
	C2_ALLOC_ARR(buf, nr);
	C2_ALLOC_ARR(ex->sirr_buf.b_buf, nr);

	C2_ASSERT(count != NULL && offset != NULL && buf != NULL &&
		  ex->sirr_buf.b_buf != NULL);
	for (i = 0; i < nr; ++i) {
		count[i]  = in->sir_vec.v_seg[i].f_count;
		offset[i] = in->sir_vec.v_seg[i].f_offset;
		buf[i] = c2_alloc(count[i]);
		C2_ASSERT(buf[i] != NULL);
		ex->sirr_buf.b_buf[i].ib_count = count[i];
		ex->sirr_buf.b_buf[i].ib_value = buf[i];
	}
	c2_stob_io_init(&io);

	io.si_user.div_vec.ov_vec.v_nr    = nr;
	io.si_user.div_vec.ov_vec.v_count = count;
	io.si_user.div_vec.ov_buf         = buf;

	io.si_stob.ov_vec.v_nr    = nr;
	io.si_stob.ov_vec.v_count = count;
	io.si_stob.ov_index       = offset;

	io.si_opcode = SIO_READ;
	io.si_flags  = 0;

	c2_clink_init(&clink, NULL);
	c2_clink_add(&io.si_wait, &clink);

	result = c2_stob_io_launch(&io, obj, NULL, NULL);
	C2_ASSERT(result == 0);

	c2_chan_wait(&clink);

	ex->sirr_rc    = io.si_rc;
	ex->sirr_count = io.si_count;

	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	c2_stob_io_fini(&io);

	c2_free(count);
	c2_free(offset);
	c2_free(buf);

	*ret = ex;
	return true;
}

static bool write_handler(const struct c2_rpc_op *op, void *arg, void **ret)
{
	struct c2_stob_io_write_fop     *in = arg;
	struct c2_stob_io_write_rep_fop *ex;
	struct c2_stob                  *obj;
	c2_bcount_t                     *count;
	c2_bindex_t                     *offset;
	void                           **buf;
	uint32_t                         nr;
	uint32_t                         i;
	struct c2_stob_io                io;
	struct c2_clink                  clink;
	bool result;

	C2_ALLOC_PTR(ex);
	C2_ASSERT(ex != NULL);

	obj = object_find(&in->siw_object);
	nr = in->siw_vec.v_count;
	C2_ASSERT(nr > 0);
	C2_ASSERT(nr == in->siw_buf.b_count);

	C2_ALLOC_ARR(count, nr);
	C2_ALLOC_ARR(offset, nr);
	C2_ALLOC_ARR(buf, nr);

	C2_ASSERT(count != NULL && offset != NULL && buf != NULL);
	for (i = 0; i < nr; ++i) {
		count[i]  = in->siw_vec.v_seg[i].f_count;
		C2_ASSERT(count[i] == in->siw_buf.b_buf[i].ib_count);
		offset[i] = in->siw_vec.v_seg[i].f_offset;
		buf[i] = in->siw_buf.b_buf[i].ib_value;
		C2_ASSERT(buf[i] != NULL);
	}
	c2_stob_io_init(&io);

	io.si_user.div_vec.ov_vec.v_nr    = nr;
	io.si_user.div_vec.ov_vec.v_count = count;
	io.si_user.div_vec.ov_buf         = buf;

	io.si_stob.ov_vec.v_nr    = nr;
	io.si_stob.ov_vec.v_count = count;
	io.si_stob.ov_index       = offset;

	io.si_opcode = SIO_WRITE;
	io.si_flags  = 0;

	c2_clink_init(&clink, NULL);
	c2_clink_add(&io.si_wait, &clink);

	result = c2_stob_io_launch(&io, obj, NULL, NULL);
	C2_ASSERT(result == 0);

	c2_chan_wait(&clink);

	ex->siwr_rc    = io.si_rc;
	ex->siwr_count = io.si_count;

	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	c2_stob_io_fini(&io);

	c2_free(count);
	c2_free(offset);
	c2_free(buf);

	*ret = ex;
	return true;
}

static bool stop = false;

static bool quit_handler(const struct c2_rpc_op *op, void *arg, void **ret)
{
	int *ex;

	C2_ALLOC_PTR(ex);
	C2_ASSERT(ex != NULL);

	*ex = 42;
	printf("I got quit request: arg = %d, res = %d\n", *((int*)arg), *ex);
	*ret = ex;
	stop = true;
	return true;
}

static const struct c2_rpc_op create_op = {
	.ro_op          = SIF_CREAT,
	.ro_arg_size    = sizeof(struct c2_stob_io_create_fop),
	.ro_xdr_arg     = (c2_xdrproc_t)xdr_c2_stob_io_create_fop,
	.ro_result_size = sizeof(struct c2_stob_io_create_rep_fop),
	.ro_xdr_result  = (c2_xdrproc_t)xdr_c2_stob_io_create_rep_fop,
	.ro_handler     = create_handler
};

static const struct c2_rpc_op read_op = {
	.ro_op          = SIF_READ,
	.ro_arg_size    = sizeof(struct c2_stob_io_read_fop),
	.ro_xdr_arg     = (c2_xdrproc_t)xdr_c2_stob_io_read_fop,
	.ro_result_size = sizeof(struct c2_stob_io_read_rep_fop),
	.ro_xdr_result  = (c2_xdrproc_t)xdr_c2_stob_io_read_rep_fop,
	.ro_handler     = read_handler
};

static const struct c2_rpc_op write_op = {
	.ro_op          = SIF_WRITE,
	.ro_arg_size    = sizeof(struct c2_stob_io_write_fop),
	.ro_xdr_arg     = (c2_xdrproc_t)xdr_c2_stob_io_write_fop,
	.ro_result_size = sizeof(struct c2_stob_io_write_rep_fop),
	.ro_xdr_result  = (c2_xdrproc_t)xdr_c2_stob_io_write_rep_fop,
	.ro_handler     = write_handler
};

static const struct c2_rpc_op quit_op = {
	.ro_op          = SIF_QUIT,
	.ro_arg_size    = sizeof(int),
	.ro_xdr_arg     = (c2_xdrproc_t)xdr_int,
	.ro_result_size = sizeof(int),
	.ro_xdr_result  = (c2_xdrproc_t)xdr_int,
	.ro_handler     = quit_handler
};

/**
   Simple server for unit-test purposes.

   Synopsis:

       server path port

   "path" is a path to a directory that the server will create if necessary that
   would contain objects (a path to a storage object domain, c2_stob_domain,
   technically).

   "port" is a port the server listens to.

   Server supports create, read and write commands.
 */
int main(int argc, char **argv)
{
	int         result;
	const char *path;
	char        opath[64];
	int         port;

	struct c2_service_id    sid = { .si_uuid = "UUURHG" };
	struct c2_rpc_op_table *ops;
	struct c2_service       service;
	struct c2_net_domain    ndom;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	path = argv[1];
	port = atoi(argv[2]);

	C2_ASSERT(strlen(path) < ARRAY_SIZE(opath) - 8);

#ifdef LINUX
	result = linux_stob_module_init();
#else
        result = -ENOSYS;
#endif
	C2_ASSERT(result == 0);
	
	result = mkdir(path, 0700);
	C2_ASSERT(result == 0 || (result == -1 && errno == EEXIST));
	sprintf(opath, "%s/o", path);
	result = mkdir(opath, 0700);
	C2_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

#ifdef LINUX
	result = linux_stob_type.st_op->sto_domain_locate(&linux_stob_type, 
							  path, &dom);
#else
        /* Others than Linux are not supported so far. */
        result = -ENOSYS;
#endif
	C2_ASSERT(result == 0);

	memset(&service, 0, sizeof service);

	result = c2_net_init();
	C2_ASSERT(result == 0);

	result = c2_net_xprt_init(&c2_net_user_sunrpc_xprt);
	C2_ASSERT(result == 0);

	result = c2_net_domain_init(&ndom, &c2_net_user_sunrpc_xprt);
	C2_ASSERT(result == 0);

	result = c2_service_id_init(&sid, &ndom, "127.0.0.1", port);
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

	result = c2_service_start(&service, &sid, ops);
	C2_ASSERT(result >= 0);

	while (!stop) {
		sleep(1);
		//printf("allocated: %li\n", c2_allocated());
	}

	c2_service_stop(&service);
	c2_rpc_op_table_fini(ops);

	c2_service_id_fini(&sid);
	c2_net_domain_fini(&ndom);
	c2_net_xprt_fini(&c2_net_user_sunrpc_xprt);
	c2_net_fini();

	dom->sd_ops->sdo_fini(dom);
#ifdef LINUX
	linux_stob_module_fini();
#endif
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
