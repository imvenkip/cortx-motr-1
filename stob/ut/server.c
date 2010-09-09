#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>  /* mkdir */
#include <sys/types.h> /* mkdir */
#include <unistd.h>    /* sleep */

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/getopts.h"
#include "lib/arith.h"  /* min64u */
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "fop/fop.h"
#include "net/net.h"
#include "net/usunrpc/usunrpc.h"

#include "stob/stob.h"
#include "stob/linux.h"
#include "stob/ad.h"
#include "colibri/init.h"

#include "io_fop.h"
#include "io_u.h"

/**
   @addtogroup stob
   @{
 */

static struct c2_stob_domain *dom;

static struct c2_stob *object_find(const struct c2_fop_fid *fid, 
				   struct c2_dtx *tx)
{
	struct c2_stob_id  id;
	struct c2_stob    *obj;
	int result;

	id.si_bits.u_hi = fid->f_seq;
	id.si_bits.u_lo = fid->f_oid;
	result = dom->sd_ops->sdo_stob_find(dom, &id, &obj);
	C2_ASSERT(result == 0);
	result = c2_stob_locate(obj, tx);
	return obj;
}

int create_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	struct c2_io_create     *in = c2_fop_data(fop);
	struct c2_io_create_rep *ex;
	struct c2_fop           *reply;
	struct c2_stob          *obj;
	struct c2_dtx            tx;
	int                      result;

	reply = c2_fop_alloc(&c2_io_create_rep_fopt, NULL);
	C2_ASSERT(reply != NULL);
	ex = c2_fop_data(reply);

	result = dom->sd_ops->sdo_tx_make(dom, &tx);
	C2_ASSERT(result == 0);

	obj = object_find(&in->sic_object, &tx);

	result = c2_stob_create(obj, &tx);
	C2_ASSERT(result == 0);
	ex->sicr_rc = 0;
	c2_net_reply_post(ctx->ft_service, reply, ctx->fc_cookie);

	c2_stob_put(obj);

	result = c2_db_tx_commit(&tx.tx_dbtx);
	C2_ASSERT(result == 0);

	return 1;
}

int read_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	struct c2_io_read     *in = c2_fop_data(fop);
	struct c2_io_read_rep *ex;
	struct c2_fop         *reply;
	struct c2_stob        *obj;
	struct c2_stob_io      io;
	struct c2_clink        clink;
	struct c2_dtx          tx;
	void                  *addr;
	uint32_t               bshift;
	uint64_t               bmask;
	int                    result;
	int                    rc;

	reply = c2_fop_alloc(&c2_io_read_rep_fopt, NULL);
	C2_ASSERT(reply != NULL);
	ex = c2_fop_data(reply);

	while (1) {
		result = dom->sd_ops->sdo_tx_make(dom, &tx);
		C2_ASSERT(result == 0);

		obj = object_find(&in->sir_object, &tx);

		bshift = obj->so_op->sop_block_shift(obj);
		bmask  = (1 << bshift) - 1;

		C2_ASSERT((in->sir_seg.f_count & bmask) == 0);
		C2_ASSERT((in->sir_seg.f_offset & bmask) == 0);
		
		C2_ALLOC_ARR(ex->sirr_buf.cib_value, in->sir_seg.f_count);
		C2_ASSERT(ex->sirr_buf.cib_value != NULL);

		in->sir_seg.f_count >>= bshift;
		in->sir_seg.f_offset >>= bshift;

		addr = c2_stob_addr_pack(ex->sirr_buf.cib_value, bshift);

		c2_stob_io_init(&io);

		io.si_user.div_vec.ov_vec.v_nr    = 1;
		io.si_user.div_vec.ov_vec.v_count = &in->sir_seg.f_count;
		io.si_user.div_vec.ov_buf = &addr;

		io.si_stob.iv_vec.v_nr    = 1;
		io.si_stob.iv_vec.v_count = &in->sir_seg.f_count;
		io.si_stob.iv_index       = &in->sir_seg.f_offset;

		io.si_opcode = SIO_READ;
		io.si_flags  = 0;

		c2_clink_init(&clink, NULL);
		c2_clink_add(&io.si_wait, &clink);

		result = c2_stob_io_launch(&io, obj, &tx, NULL);
		C2_ASSERT(result == 0);

		c2_chan_wait(&clink);

		ex->sirr_rc            = io.si_rc;
		ex->sirr_buf.cib_count = io.si_count << bshift;

		c2_clink_del(&clink);
		c2_clink_fini(&clink);

		c2_stob_io_fini(&io);

		c2_stob_put(obj);

		if (result != -EDEADLK) {
			rc = c2_db_tx_commit(&tx.tx_dbtx);
			C2_ASSERT(rc == 0);
			break;
		} else {
			fprintf(stderr, "Deadlock, aborting read.\n");
			rc = c2_db_tx_abort(&tx.tx_dbtx);
			C2_ASSERT(rc == 0);
		}
	}
	c2_net_reply_post(ctx->ft_service, reply, ctx->fc_cookie);

	return 1;
}

int write_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	struct c2_io_write     *in = c2_fop_data(fop);
	struct c2_io_write_rep *ex;
	struct c2_fop          *reply;
	struct c2_stob         *obj;
	struct c2_stob_io       io;
	struct c2_dtx           tx;
	void                   *addr;
	c2_bcount_t             count;
	c2_bindex_t             offset;
	struct c2_clink         clink;
	uint32_t                bshift;
	uint64_t                bmask;
	int                     result;
	int                     rc;

	reply = c2_fop_alloc(&c2_io_write_rep_fopt, NULL);
	C2_ASSERT(reply != NULL);
	ex = c2_fop_data(reply);

	while (1) {
		result = dom->sd_ops->sdo_tx_make(dom, &tx);
		C2_ASSERT(result == 0);

		obj = object_find(&in->siw_object, &tx);

		bshift = obj->so_op->sop_block_shift(obj);
		bmask  = (1 << bshift) - 1;

		C2_ASSERT((in->siw_buf.cib_count & bmask) == 0);
		C2_ASSERT((in->siw_offset & bmask) == 0);

		addr = c2_stob_addr_pack(in->siw_buf.cib_value, bshift);
		count = in->siw_buf.cib_count >> bshift;
		offset = in->siw_offset >> bshift;

		c2_stob_io_init(&io);

		io.si_user.div_vec.ov_vec.v_nr    = 1;
		io.si_user.div_vec.ov_vec.v_count = &count;
		io.si_user.div_vec.ov_buf = &addr;

		io.si_stob.iv_vec.v_nr    = 1;
		io.si_stob.iv_vec.v_count = &count;
		io.si_stob.iv_index       = &offset;

		io.si_opcode = SIO_WRITE;
		io.si_flags  = 0;

		c2_clink_init(&clink, NULL);
		c2_clink_add(&io.si_wait, &clink);

		result = c2_stob_io_launch(&io, obj, &tx, NULL);

		if (result == 0)
			c2_chan_wait(&clink);

		ex->siwr_rc    = io.si_rc;
		ex->siwr_count = io.si_count << bshift;

		c2_clink_del(&clink);
		c2_clink_fini(&clink);

		c2_stob_io_fini(&io);

		c2_stob_put(obj);

		if (result != -EDEADLK) {
			rc = c2_db_tx_commit(&tx.tx_dbtx);
			C2_ASSERT(rc == 0);
			break;
		} else {
			fprintf(stderr, "Deadlock, aborting write.\n");
			rc = c2_db_tx_abort(&tx.tx_dbtx);
			C2_ASSERT(rc == 0);
		}
	}
	c2_net_reply_post(ctx->ft_service, reply, ctx->fc_cookie);

	return 1;
}

static bool stop = false;

int quit_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	struct c2_fop     *reply;
        struct c2_io_quit *ex;

	reply = c2_fop_alloc(&c2_io_quit_fopt, NULL);
	C2_ASSERT(reply != NULL);
	ex = c2_fop_data(reply);

	ex->siq_rc = 42;
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

struct mock_balloc {
	struct c2_mutex  mb_lock;
	c2_bindex_t      mb_next;
	struct ad_balloc mb_ballroom;
};

static struct mock_balloc *b2mock(struct ad_balloc *ballroom)
{
	return container_of(ballroom, struct mock_balloc, mb_ballroom);
}

static int mock_balloc_init(struct ad_balloc *ballroom, struct c2_dbenv *db,
			    uint32_t bshift)
{
	struct mock_balloc *mb = b2mock(ballroom);

	c2_mutex_init(&mb->mb_lock);
	return 0;
}

static void mock_balloc_fini(struct ad_balloc *ballroom)
{
	struct mock_balloc *mb = b2mock(ballroom);

	c2_mutex_fini(&mb->mb_lock);
}

static int mock_balloc_alloc(struct ad_balloc *ballroom, struct c2_dtx *tx,
			     c2_bcount_t count, struct c2_ext *out)
{
	struct mock_balloc *mb = b2mock(ballroom);
	c2_bcount_t giveout;

	c2_mutex_lock(&mb->mb_lock);
	giveout = min64u(count, 500000);
	out->e_start = mb->mb_next;
	out->e_end   = mb->mb_next + giveout;
	mb->mb_next += giveout + 1;
	/*
	printf("allocated %8lx/%8lx bytes: [%8lx .. %8lx)\n", giveout, count, 
	       out->e_start, out->e_end); */
	c2_mutex_unlock(&mb->mb_lock);
	return 0;
}

static int mock_balloc_free(struct ad_balloc *ballroom, struct c2_dtx *tx,
			    struct c2_ext *ext)
{
	printf("freed     %8lx bytes: [%8lx .. %8lx)\n", c2_ext_length(ext),
	       ext->e_start, ext->e_end);
	return 0;
}

static const struct ad_balloc_ops mock_balloc_ops = {
	.bo_init  = mock_balloc_init,
	.bo_fini  = mock_balloc_fini,
	.bo_alloc = mock_balloc_alloc,
	.bo_free  = mock_balloc_free,
};

static struct mock_balloc mb = {
	.mb_next = 0,
	.mb_ballroom = {
		.ab_ops = &mock_balloc_ops
	}
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
	char        dpath[64];
	int         port;

	struct c2_stob_domain  *bdom;
	struct c2_stob_id       backid;
	struct c2_stob         *bstore;
	struct c2_service_id    sid = { .si_uuid = "UUURHG" };
	struct c2_service       service;
	struct c2_net_domain    ndom;
	struct c2_dbenv         db;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	backid.si_bits.u_hi = 0x8;
	backid.si_bits.u_lo = 0xf00baf11e;
	port = 1001;
	path = "__s";

	result = C2_GETOPTS("server", argc, argv,
			    C2_VOIDARG('T', "parse trace log produced earlier",
				       LAMBDA(void, (void) {
					       c2_trace_parse();
					       exit(0);
					       })),
			    C2_STRINGARG('d', "path to object store",
				       LAMBDA(void, (const char *string) { 
					       path = string; })),
			    C2_NUMBERARG('o', "back store object id",
					 LAMBDA(void, (int64_t num) { 
					       backid.si_bits.u_lo = num; })),
			    C2_NUMBERARG('p', "port to listen at",
				       LAMBDA(void, (int64_t num) { 
					       port = num; })));
	if (result != 0)
		return result;

	result = c2_init();
	C2_ASSERT(result == 0);
	
	result = io_fop_init();
	C2_ASSERT(result == 0);

	C2_ASSERT(strlen(path) < ARRAY_SIZE(opath) - 8);

	result = mkdir(path, 0700);
	C2_ASSERT(result == 0 || (result == -1 && errno == EEXIST));
	sprintf(opath, "%s/o", path);
	result = mkdir(opath, 0700);
	C2_ASSERT(result == 0 || (result == -1 && errno == EEXIST));

	sprintf(dpath, "%s/d", path);

	/*
	 * Initialize the data-base.
	 */
	result = c2_dbenv_init(&db, dpath, 0);
	C2_ASSERT(result == 0);

	/*
	 * Locate and create (if necessary) the backing store object.
	 */

	result = linux_stob_type.st_op->sto_domain_locate(&linux_stob_type, 
							  path, &bdom);
	C2_ASSERT(result == 0);

	result = bdom->sd_ops->sdo_stob_find(bdom, &backid, &bstore);
	C2_ASSERT(result == 0);
	C2_ASSERT(bstore->so_state == CSS_UNKNOWN);

	result = c2_stob_create(bstore, NULL);
	C2_ASSERT(result == 0);
	C2_ASSERT(bstore->so_state == CSS_EXISTS);

	/*
	 * Create AD domain over backing store object.
	 */
	result = ad_stob_type.st_op->sto_domain_locate(&ad_stob_type, "", &dom);
	C2_ASSERT(result == 0);

	result = ad_setup(dom, &db, bstore, &mb.mb_ballroom);
	C2_ASSERT(result == 0);

	c2_stob_put(bstore);

	/*
	 * Set up the service.
	 */
	C2_SET0(&service);

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
	bdom->sd_ops->sdo_fini(bdom);
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
