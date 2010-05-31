/* -*- C -*- */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/smp_lock.h>
#include <linux/uaccess.h>
#include <linux/vfs.h>
#include <linux/param.h>
#include <linux/time.h>
#include <linux/utsname.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xdr.h>

#include "ksunrpc.h"

/* taken from other branch. When merged, should be removed, or be consistant */
enum c2_stob_io_fop_opcode {
        SIF_READ  = 0x4001,
        SIF_WRITE = 0x4002,
        SIF_CREAT = 0x4003,
        SIF_QUIT  = 0x4004
};

static int ksunrpc_xdr_enc_int(struct rpc_rqst *req, uint32_t *p, int *args)
{
	*p++ = htonl(*args);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	return 0;
}

static int ksunrpc_xdr_dec_int(struct rpc_rqst *req, uint32_t *p, int *res)
{
	*res = ntohl(*p++);
	return 0;
}

#if 0
static uint32_t *decode_64(uint32_t *place, uint64_t *val)
{
	*val   = ntohl(*place++);
	*val <<= 32;
	*val  |= ntohl(*place++);
	return place;
}
#endif

static uint32_t *decode_32(uint32_t *place, uint32_t *val)
{
	*val = ntohl(*place++);
	return place;
}

static uint32_t *encode_64(uint32_t *place, uint64_t val)
{
	*place++ = htonl((uint32_t)(val >> 32));
	*place++ = htonl((uint32_t)val);
	return place;
}

static uint32_t *encode_32(uint32_t *place, uint32_t val)
{
	*place++ = htonl((uint32_t)val);
	return place;
}

static uint32_t *reserve_nr(struct xdr_stream *xdr, size_t nr)
{
	uint32_t *result;

	result = xdr_reserve_space(xdr, nr);
	BUG_ON(result == 0);
	return result;
}

enum {
	/*
	  FID    (8 + 8)
	  1      (v_count, 4)
	  OFFSET (8)
	  NOB    (4)
	  PAD    (4)
	  NOB    (4)
	  pages
	*/
	C2T1FS_WRITE_BASE = 8 + 8 + 4 + 8 + 4 + 4 + 4,
	/*
	  FID    (8 + 8)
	  1      (v_count, 4)
	  OFFSET (8)
	  NOB    (4)
	  pages
	*/

	C2T1FS_READ_BASE = 8 + 8 + 4 + 8 + 4,
};

static int ksunrpc_xdr_enc_create(struct rpc_rqst *req, uint32_t *p, void *datum)
{
	struct c2t1fs_create_arg *arg = datum;
	struct xdr_stream        xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	p = reserve_nr(&xdr, sizeof(*arg));
	p = encode_64(p, arg->ca_fid.f_d1);
	p = encode_64(p, arg->ca_fid.f_d2);
	return 0;
}

static int ksunrpc_xdr_dec_create(struct rpc_rqst *req, uint32_t *p, void *datum)
{
	struct c2t1fs_create_res *ret = datum;

	decode_32(p, &ret->res);
	return 0;
}

const struct c2_rpc_op create_op = {
        .ro_op          = SIF_CREAT,
        .ro_arg_size    = sizeof(struct c2t1fs_create_arg),
        .ro_xdr_arg     = (c2_xdrproc_t)ksunrpc_xdr_enc_create,
        .ro_result_size = sizeof(struct c2t1fs_create_res),
        .ro_xdr_result  = (c2_xdrproc_t)ksunrpc_xdr_dec_create,
        .ro_handler     = NULL,
	.ro_name        = "create"
};


static int ksunrpc_xdr_enc_write(struct rpc_rqst *req, uint32_t *p, void *datum)
{
	struct c2t1fs_write_arg *arg = datum;
	struct xdr_stream        xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	p = reserve_nr(&xdr, C2T1FS_WRITE_BASE);
	p = encode_64(p, arg->wa_fid.f_d1);
	p = encode_64(p, arg->wa_fid.f_d2);
	p = encode_32(p, 1); /* count of segments */
	p = encode_64(p, arg->wa_offset);
	p = encode_32(p, arg->wa_nob);
	p = encode_32(p, 1); /* padding */
	encode_32(p, arg->wa_nob);
	xdr_write_pages(&xdr, arg->wa_pages, arg->wa_pageoff, arg->wa_nob);
	return 0;
}

static int ksunrpc_xdr_dec_write(struct rpc_rqst *req, uint32_t *p, void *datum)
{
	struct c2t1fs_write_ret *ret = datum;

	p = decode_32(p, &ret->cwr_rc);
	decode_32(p, &ret->cwr_count);
	return 0;
}

const struct c2_rpc_op write_op = {
        .ro_op          = SIF_WRITE,
        .ro_arg_size    = C2T1FS_WRITE_BASE,
        .ro_xdr_arg     = (c2_xdrproc_t)ksunrpc_xdr_enc_write,
        .ro_result_size = sizeof(struct c2t1fs_write_ret),
        .ro_xdr_result  = (c2_xdrproc_t)ksunrpc_xdr_dec_write,
        .ro_handler     = NULL,
	.ro_name        = "write"
};

static int ksunrpc_xdr_enc_read(struct rpc_rqst *req, uint32_t *p, void *datum)
{
	struct c2t1fs_read_arg *arg = datum;
	struct xdr_stream        xdr;
        struct rpc_auth *auth = req->rq_task->tk_auth;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	p = reserve_nr(&xdr, C2T1FS_READ_BASE);
	p = encode_64(p, arg->ra_fid.f_d1);
	p = encode_64(p, arg->ra_fid.f_d2);
	p = encode_32(p, 1); /* count of segments */
	p = encode_64(p, arg->ra_offset);
	p = encode_32(p, arg->ra_nob);

        /* 
                struct c2_stob_io_read_rep_fop {
                        u_int sirr_rc;
                        u_int sirr_count;
                        struct {
                                u_int                  b_count;
                                struct c2_stob_io_buf {
                                        u_int ib_count;
                                        char *ib_value;
                                } *b_buf;
                        } sirr_buf;
                };
                replen = 4 + 4 + 4 + 4;
        */
	xdr_inline_pages(&req->rq_rcv_buf,
                        (RPC_REPHDRSIZE + auth->au_rslack + 4) << 2,
                        arg->ra_pages, arg->ra_pageoff, arg->ra_nob);
	return 0;
}

static int ksunrpc_xdr_dec_read(struct rpc_rqst *req, uint32_t *p, void *datum)
{
        struct xdr_stream xdr;
	struct c2t1fs_read_ret *ret = datum;
        int bcount, nbuf;

	p = decode_32(p, &ret->crr_rc);
	p = decode_32(p, &ret->crr_count);
        p = decode_32(p, &bcount);
        p = decode_32(p, &nbuf);

        xdr_init_decode(&xdr, &req->rq_rcv_buf, p);
        xdr_read_pages(&xdr, ret->crr_count);

	return 0;
}

const struct c2_rpc_op read_op = {
        .ro_op          = SIF_READ,
        .ro_arg_size    = C2T1FS_READ_BASE,
        .ro_xdr_arg     = (c2_xdrproc_t)ksunrpc_xdr_enc_read,
        .ro_result_size = sizeof(struct c2t1fs_read_ret),
        .ro_xdr_result  = (c2_xdrproc_t)ksunrpc_xdr_dec_read,
        .ro_handler     = NULL,
	.ro_name        = "read"
};

const struct c2_rpc_op quit_op = {
        .ro_op          = SIF_QUIT,
        .ro_arg_size    = sizeof(int),
        .ro_xdr_arg     = (c2_xdrproc_t)ksunrpc_xdr_enc_int,
        .ro_result_size = sizeof(int),
        .ro_xdr_result  = (c2_xdrproc_t)ksunrpc_xdr_dec_int,
        .ro_handler     = NULL,
	.ro_name        = "quit"
};

EXPORT_SYMBOL(create_op);
EXPORT_SYMBOL(write_op);
EXPORT_SYMBOL(read_op);
EXPORT_SYMBOL(quit_op);

MODULE_LICENSE("GPL");
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
