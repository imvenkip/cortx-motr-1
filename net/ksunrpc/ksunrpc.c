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

/**
   @addtogroup ksunrpc Sun RPC

   Kernel User level Sunrpc-based implementation of C2 networking interfaces.

  @{
 */

#define C2_DEF_RPC_VER  1
#define C2_SESSION_PROGRAM 0x20000001

/*
 * Client code.
 */
struct rpc_procinfo c2t1_procedures[2] = {
	[0] = {},
	[1] = {},
};


struct rpc_version  c2t1_sunrpc_version = {
	.number     = 1,
	.nrprocs    = ARRAY_SIZE(c2t1_procedures),
	.procs      = c2t1_procedures
};

static struct rpc_version *c2t1_versions[2] = {
	[0]	    = NULL,
	[1]         = &c2t1_sunrpc_version,
};

struct rpc_stat c2t1_rpcstat;

struct rpc_program c2t1_program = {
	.name                   = "c2t1",
	.number                 = C2_SESSION_PROGRAM,
	.nrvers                 = ARRAY_SIZE(c2t1_versions),
	.version                = c2t1_versions,
	.stats                  = &c2t1_rpcstat,
	.pipe_dir_name          = NULL,
};

struct rpc_stat c2t1_rpcstat = {
	.program                = &c2t1_program
};

void ksunrpc_xprt_fini(struct ksunrpc_xprt *xprt)
{
	if (xprt == NULL)
		return;

	if (xprt->ksx_client != NULL) {
		rpc_shutdown_client(xprt->ksx_client);
		xprt->ksx_client = NULL;
	}
	kfree(xprt);
}

#define C2_DEF_TCP_RETRANS (2)
#define C2_DEF_TCP_TIMEO   (600)

struct ksunrpc_xprt* ksunrpc_xprt_init(struct ksunrpc_service_id *xsid)
{
	struct rpc_timeout       timeparms = {
		.to_retries   = C2_DEF_TCP_RETRANS,
		.to_initval   = C2_DEF_TCP_TIMEO * HZ / 10,
		.to_increment = C2_DEF_TCP_TIMEO * HZ / 10,
		.to_maxval    = C2_DEF_TCP_TIMEO * HZ / 10 +
			       (C2_DEF_TCP_TIMEO * HZ / 10 * C2_DEF_TCP_RETRANS),
		.to_exponential = 0
	};

	struct rpc_clnt         *clnt;
	struct rpc_xprt         *rpc_xprt;
	struct ksunrpc_xprt     *ksunrpc_xprt;

	ksunrpc_xprt = kmalloc(sizeof *ksunrpc_xprt, GFP_KERNEL);
	if (ksunrpc_xprt == NULL)
		return ERR_PTR(-ENOMEM);

	/* create transport and client */
        rpc_xprt = xprt_create_proto(IPPROTO_TCP, xsid->ssi_sockaddr, &timeparms);
	if (IS_ERR(rpc_xprt)) {
		printk("%s: cannot create RPC transport. Error = %ld\n",
		__FUNCTION__, PTR_ERR(rpc_xprt));
                return (struct ksunrpc_xprt *)rpc_xprt;
        }
	clnt = rpc_create_client(rpc_xprt, xsid->ssi_host, &c2t1_program,
				 1, RPC_AUTH_NULL);
	if (IS_ERR(clnt)) {
		printk("%s: cannot create RPC client. Error = %ld\n",
			__FUNCTION__, PTR_ERR(clnt));
			return (struct ksunrpc_xprt *)clnt;
	}

	clnt->cl_intr     = 1;
	clnt->cl_softrtry = 1;

	ksunrpc_xprt->ksx_client = clnt;
        return ksunrpc_xprt;
}

static int ksunrpc_xprt_call(struct ksunrpc_xprt *xprt,
			     const struct c2_rpc_op *op,
			     void *arg, void *ret)
{
	int                 result;

	struct rpc_procinfo proc = {
		.p_proc   = op->ro_op,
		.p_encode = (kxdrproc_t) op->ro_xdr_arg,
		.p_decode = (kxdrproc_t) op->ro_xdr_result,
		.p_bufsiz = op->ro_arg_size,
		.p_statidx= 1,
		.p_name   = op->ro_name,
	};

        struct rpc_message msg = {
                .rpc_proc = &proc,
                .rpc_argp = arg,
                .rpc_resp = ret,
        };

        result = rpc_call_sync(xprt->ksx_client, &msg, 0);

	return result;
}

static int ksunrpc_xprt_send(struct ksunrpc_xprt *xprt,
			     const struct c2_rpc_op *op,
			     void *arg, void *ret,
			     struct rpc_call_ops *async_ops,
			     void *data)
{
	int result;

	struct rpc_procinfo proc = {
		.p_proc   = op->ro_op,
		.p_encode = (kxdrproc_t) op->ro_xdr_arg,
		.p_decode = (kxdrproc_t) op->ro_xdr_result,
		.p_bufsiz = op->ro_arg_size,
		.p_statidx= op->ro_op,
		.p_name   = op->ro_name,
	};

        struct rpc_message msg = {
                .rpc_proc = &proc,
                .rpc_argp = arg,
                .rpc_resp = ret,
        };

        result = rpc_call_async(xprt->ksx_client, &msg, RPC_TASK_SOFT,
                                async_ops, data);

	return result;
}

struct ksunrpc_xprt_ops ksunrpc_xprt_ops = {
	.ksxo_init = ksunrpc_xprt_init,
	.ksxo_fini = ksunrpc_xprt_fini,
	.ksxo_call = ksunrpc_xprt_call,
	.ksxo_send = ksunrpc_xprt_send,
};

EXPORT_SYMBOL(ksunrpc_xprt_ops);

/*******************************************************************************
 *                     Start of UT                                             *
 ******************************************************************************/

#if 1

static int ksunrpc_xdr_enc_int(struct rpc_rqst *req, uint32_t *p, int *args)
{
	*p++ = htonl(*args);
	req->rq_slen = xdr_adjust_iovec(req->rq_svec, p);
	printk("arg encoded\n");
	return 0;
}

static int ksunrpc_xdr_dec_int(struct rpc_rqst *req, uint32_t *p, int *res)
{
	*res = ntohl(*p++);
	printk("res decoded\n");
	return 0;
}

/* taken from other branch. When merged, should be removed, or be consistant */
enum c2_stob_io_fop_opcode {
        SIF_READ  = 0x4001,
        SIF_WRITE = 0x4002,
        SIF_CREAT = 0x4003,
        SIF_QUIT  = 0x4004
};

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

struct c2_fid {
	uint64_t f_d1;
	uint64_t f_d2;
};

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
};

/* --------- write op implementation --------- */

struct c2t1fs_write_arg {
	struct c2_fid wa_fid;
	uint32_t      wa_nob;
	uint64_t      wa_offset;
	struct page **wa_pages;
};

struct c2t1fs_write_ret {
	uint32_t cwr_rc;
	uint32_t cwr_count;
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
	p = encode_32(p, 0); /* padding */
	encode_32(p, arg->wa_nob); 
	xdr_write_pages(&xdr, arg->wa_pages, 0, arg->wa_nob);
	printk("write encoded\n");
	return 0;
}

static int ksunrpc_xdr_dec_write(struct rpc_rqst *req, uint32_t *p, void *datum)
{
	struct c2t1fs_write_ret *ret = datum;

	p = decode_32(p, &ret->cwr_rc);
	decode_32(p, &ret->cwr_count);
	printk("write decoded\n");
	return 0;
}

static const struct c2_rpc_op write_op = {
        .ro_op          = SIF_WRITE,
        .ro_arg_size    = C2T1FS_WRITE_BASE,
        .ro_xdr_arg     = (c2_xdrproc_t)ksunrpc_xdr_enc_write,
        .ro_result_size = sizeof(struct c2t1fs_write_ret),
        .ro_xdr_result  = (c2_xdrproc_t)ksunrpc_xdr_dec_write,
        .ro_handler     = NULL,
	.ro_name        = "write"
};
/* --------- write op end --------- */

/* --------- read op implementation --------- */
struct c2t1fs_read_arg {
	struct c2_fid wa_fid;
	uint32_t      wa_nob;
	uint64_t      wa_offset;
};

struct c2t1fs_read_ret {
	uint32_t crr_rc;
	uint32_t crr_count;
	struct page **wa_pages;
};

static const struct c2_rpc_op read_op = {
        .ro_op          = SIF_READ,
        .ro_arg_size    = C2T1FS_READ_BASE,
        .ro_xdr_arg     = (c2_xdrproc_t)ksunrpc_xdr_enc_read,
        .ro_result_size = sizeof(struct c2t1fs_read_ret),
        .ro_xdr_result  = (c2_xdrproc_t)ksunrpc_xdr_dec_read_ret,
        .ro_handler     = NULL,
	.ro_name        = "write"
};

/* --------- read op end --------- */

static const struct c2_rpc_op quit_op = {
        .ro_op          = SIF_QUIT,
        .ro_arg_size    = sizeof(int),
        .ro_xdr_arg     = (c2_xdrproc_t)ksunrpc_xdr_enc_int,
        .ro_result_size = sizeof(int),
        .ro_xdr_result  = (c2_xdrproc_t)ksunrpc_xdr_dec_int,
        .ro_handler     = NULL,
	.ro_name        = "quit"
};

static struct proc_dir_entry *proc_ksunrpc_ut;

static int read_ksunrpc_ut(char *page, char **start, off_t off,
			   int count, int *eof, void *data)
{
	int ret;

	ret = sprintf(page + off, "nothing to read\n");
	*eof = 1;
	printk("reading from ut\n");
	return ret;
}

static int write_ksunrpc_ut(struct file *file, const char __user *buffer,
			    unsigned long count, void *data)
{
	char host[128];
	struct sockaddr_in sa;
	struct ksunrpc_service_id id_in_kernel = {
		.ssi_host = host,
		.ssi_sockaddr = &sa,
	};
	struct ksunrpc_service_id id_in_user;
	struct ksunrpc_xprt* xprt;

	if (count != sizeof id_in_kernel) {
		printk("writing: wrong size %d. %d is expected\n", (int)count,
			(int)sizeof id_in_kernel);
		return count;
	}
	if (copy_from_user(&id_in_user, buffer, count))
		return -EFAULT;

	if (copy_from_user(id_in_kernel.ssi_host, id_in_user.ssi_host, id_in_user.ssi_addrlen))
		return -EFAULT;

	if (copy_from_user(id_in_kernel.ssi_sockaddr, id_in_user.ssi_sockaddr, sizeof sa))
		return -EFAULT;
	id_in_kernel.ssi_addrlen = id_in_user.ssi_addrlen;
	id_in_kernel.ssi_port = id_in_user.ssi_port;

	printk("got data from userspace. connect...\n");
	xprt = ksunrpc_xprt_ops.ksxo_init(&id_in_kernel);
	printk("got xprt = %p\n", xprt);
	if (!IS_ERR(xprt)) {
		int arg = 101;
		int res;
		int retval;
		struct page *pp[1];
		struct c2t1fs_write_arg wa = {
			.wa_fid    = { .f_d1 = 10, .f_d2 = 10 },
			.wa_nob    = 4096,
			.wa_offset = 4096,
			.wa_pages  = pp
		};
		struct c2t1fs_write_ret wr;

		pp[0] = alloc_page(GFP_KERNEL);
		BUG_ON(pp[0] == NULL);

		printk("sending SIF_WRITE to server\n");
		retval = ksunrpc_xprt_ops.ksxo_call(xprt, &write_op, &wa, &wr);
		printk("got reply: retval=%d, result=%d, %d\n", retval, 
		       wr.cwr_rc, wr.cwr_count);

		printk("sending SIF_QUIT to server\n");
		retval = ksunrpc_xprt_ops.ksxo_call(xprt, &quit_op, &arg, &res);
		printk("got reply: retval=%d, result=%d\n", retval, res);

		ksunrpc_xprt_ops.ksxo_fini(xprt);
		__free_page(pp[0]);
	}

	return count;
}

static void __init create_ut_proc_entry(void)
{
	proc_ksunrpc_ut = create_proc_entry(UT_PROC_NAME, 0644, NULL);
	if (proc_ksunrpc_ut) {
		proc_ksunrpc_ut->read_proc  = read_ksunrpc_ut;
		proc_ksunrpc_ut->write_proc = write_ksunrpc_ut;
	}
}

static void __exit remove_ut_proc_entry(void)
{
	if (proc_ksunrpc_ut)
		remove_proc_entry(UT_PROC_NAME, NULL);
}

#else

static void __init create_ut_proc_entry(void) {}
static void __exit remove_ut_proc_entry(void) {}

#endif

/*******************************************************************************
 *                       End of UT                                             *
 ******************************************************************************/


static int __init kernel_sunrpc_init(void)
{
	printk(KERN_INFO "Colibri Kernel Client SUNRPC(http://www.clusterstor.com)\n");
	create_ut_proc_entry();
	return 0;
}

static void __exit kernel_sunrpc_fini(void)
{
	remove_ut_proc_entry();
	printk(KERN_INFO "Colibri Kernel Client SUNRPC cleanup\n");
}

module_init(kernel_sunrpc_init)
module_exit(kernel_sunrpc_fini)

MODULE_AUTHOR("Huang Hua <hua.huang@clusterstor.com>");
MODULE_DESCRIPTION("C2 Kernel Client RPC");
MODULE_LICENSE("GPL");

/** @} end of group ksunrpc */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
