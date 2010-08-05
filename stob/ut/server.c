#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>    /* memset */
#include <sys/stat.h>  /* mkdir */
#include <sys/types.h> /* mkdir */
#include <unistd.h>    /* sleep */

#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "fop/fop.h"
#include "net/net.h"
#include "net/usunrpc/usunrpc.h"

#include "stob/stob.h"
#include "stob/linux.h"
#include "colibri/init.h"

#include "io_fop.h"
#include "io_u.h"

/**
   @addtogroup stob
   @{
 */

static struct c2_stob_domain *dom;

static struct c2_stob *object_find(const struct c2_fop_fid *fid)
{
	struct c2_stob_id  id;
	struct c2_stob    *obj;
	int result;

	id.si_seq = fid->f_seq;
	id.si_id  = fid->f_oid;
	result = dom->sd_ops->sdo_stob_find(dom, &id, &obj);
	C2_ASSERT(result == 0);
	result = c2_stob_locate(obj);
	return obj;
}

int create_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	struct c2_io_create     *in = c2_fop_data(fop);
	struct c2_io_create_rep *ex;
	struct c2_fop                    *reply;
	struct c2_stob                   *obj;
	bool result;
	
	reply = c2_fop_alloc(&c2_io_create_rep_fopt, NULL);
	C2_ASSERT(reply != NULL);
	ex = c2_fop_data(reply);

	obj = object_find(&in->sic_object);
	result = c2_stob_create(obj);
	C2_ASSERT(result == 0);
	ex->sicr_rc = 0;
	c2_net_reply_post(ctx->ft_service, reply, ctx->fc_cookie);
	return 1;
}

int read_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	struct c2_io_read     *in = c2_fop_data(fop);
	struct c2_io_read_rep *ex;
	struct c2_fop                  *reply;
	struct c2_stob                 *obj;
	struct c2_stob_io               io;
	struct c2_clink                 clink;
	bool result;

	reply = c2_fop_alloc(&c2_io_read_rep_fopt, NULL);
	C2_ASSERT(reply != NULL);
	ex = c2_fop_data(reply);

	obj = object_find(&in->sir_object);

	C2_ALLOC_ARR(ex->sirr_buf.cib_value, in->sir_seg.f_count);

	C2_ASSERT(ex->sirr_buf.cib_value != NULL);
	c2_stob_io_init(&io);

	io.si_user.div_vec.ov_vec.v_nr    = 1;
	io.si_user.div_vec.ov_vec.v_count = &in->sir_seg.f_count;
	io.si_user.div_vec.ov_buf         = (void **)&ex->sirr_buf.cib_value;

	io.si_stob.ov_vec.v_nr    = 1;
	io.si_stob.ov_vec.v_count = &in->sir_seg.f_count;
	io.si_stob.ov_index       = &in->sir_seg.f_offset;

	io.si_opcode = SIO_READ;
	io.si_flags  = 0;

	c2_clink_init(&clink, NULL);
	c2_clink_add(&io.si_wait, &clink);

	result = c2_stob_io_launch(&io, obj, NULL, NULL);
	C2_ASSERT(result == 0);

	c2_chan_wait(&clink);

	ex->sirr_rc             = io.si_rc;
	ex->sirr_buf.cib_count = io.si_count;

	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	c2_stob_io_fini(&io);

	c2_net_reply_post(ctx->ft_service, reply, ctx->fc_cookie);
	return 1;
}

int write_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	struct c2_io_write     *in = c2_fop_data(fop);
	struct c2_io_write_rep *ex;
	struct c2_fop                   *reply;
	struct c2_stob                  *obj;
	struct c2_stob_io                io;
	c2_bcount_t                      count;
	struct c2_clink                  clink;
	bool result;

	reply = c2_fop_alloc(&c2_io_write_rep_fopt, NULL);
	C2_ASSERT(reply != NULL);
	ex = c2_fop_data(reply);

	obj = object_find(&in->siw_object);

	c2_stob_io_init(&io);

	count = in->siw_buf.cib_count;
	io.si_user.div_vec.ov_vec.v_nr    = 1;
	io.si_user.div_vec.ov_vec.v_count = &count;
	io.si_user.div_vec.ov_buf         = (void **)&in->siw_buf.cib_value;

	io.si_stob.ov_vec.v_nr    = 1;
	io.si_stob.ov_vec.v_count = &count;
	io.si_stob.ov_index       = &in->siw_offset;

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

	c2_net_reply_post(ctx->ft_service, reply, ctx->fc_cookie);
	return 1;
}

static bool stop = false;

int quit_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	struct c2_io_quit *in = c2_fop_data(fop);
	struct c2_fop              *reply;
        struct c2_io_quit *ex;

	reply = c2_fop_alloc(&c2_io_quit_fopt, NULL);
	C2_ASSERT(reply != NULL);
	ex = c2_fop_data(reply);

	ex->siq_rc = 42;
	printf("I got quit request: arg = %d\n", in->siq_rc);
	stop = true;

	c2_net_reply_post(ctx->ft_service, reply, ctx->fc_cookie);
	return 1;
}

static int io_handler(struct c2_service *service, struct c2_fop *fop,
		      void *cookie)
{
	struct c2_fop_ctx ctx;

	ctx.ft_service = service;
	ctx.fc_cookie  = cookie;
	return fop->f_type->ft_ops->fto_execute(fop, &ctx);
}

static struct c2_fop_type *fopt[] = {
	&c2_io_write_fopt,
	&c2_io_read_fopt,
	&c2_io_create_fopt,
	&c2_io_quit_fopt
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
	struct c2_service       service;
	struct c2_net_domain    ndom;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	if (argc != 3) {
		fprintf(stderr, "%s path port\n", argv[0]);
		return -1;
	}

	result = c2_init();
	C2_ASSERT(result == 0);
	
	result = io_fop_init();
	C2_ASSERT(result == 0);

	path = argv[1];
	port = atoi(argv[2]);

	C2_ASSERT(strlen(path) < ARRAY_SIZE(opath) - 8);

	result = mkdir(path, 0700);
	C2_ASSERT(result == 0 || (result == -1 && errno == EEXIST));
	sprintf(opath, "%s/o", path);
	result = mkdir(opath, 0700);
	C2_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	result = linux_stob_type.st_op->sto_domain_locate(&linux_stob_type, 
							  path, &dom);
	C2_ASSERT(result == 0);

	memset(&service, 0, sizeof service);

	service.s_table.not_start = fopt[0]->ft_code;
	service.s_table.not_nr    = ARRAY_SIZE(fopt);
	service.s_table.not_fopt  = fopt;
	service.s_handler         = &io_handler;

	result = c2_net_xprt_init(&c2_net_usunrpc_xprt);
	C2_ASSERT(result == 0);

	result = c2_net_domain_init(&ndom, &c2_net_usunrpc_xprt);
	C2_ASSERT(result == 0);

	result = c2_service_id_init(&sid, &ndom, "127.0.0.1", port);
	C2_ASSERT(result == 0);

	result = c2_service_start(&service, &sid);
	C2_ASSERT(result >= 0);

	while (!stop) {
		sleep(1);
		//printf("allocated: %li\n", c2_allocated());
	}

	c2_service_stop(&service);
	c2_service_id_fini(&sid);
	c2_net_domain_fini(&ndom);
	c2_net_xprt_fini(&c2_net_usunrpc_xprt);

	dom->sd_ops->sdo_fini(dom);
	io_fop_fini();
	c2_fini();
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
