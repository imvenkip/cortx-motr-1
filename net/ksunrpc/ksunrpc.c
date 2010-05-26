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
#include <linux/sunrpc/clnt.h>

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

struct rpc_procinfo c2t1_procedures[] = {
};


struct rpc_version  c2t1_sunrpc_version = {
        .number     = 1,
        .nrprocs    = ARRAY_SIZE(c2t1_procedures),
        .procs      = c2t1_procedures
};

static struct rpc_version *c2t1_versions[2] = {
        [1]         = &c2t1_sunrpc_version,
};

struct rpc_stat c2t1_rpcstat;

struct rpc_program c2t1_program = {
        .name                   = "c2t1",
        .number                 = C2_SESSION_PROGRAM,
        .nrvers                 = ARRAY_SIZE(c2t1_versions),
        .version                = c2t1_versions,
        .stats                  = &c2t1_rpcstat,
        .pipe_dir_name          = "/c2t1",
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
		.p_statidx= op->ro_op,
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

static struct proc_dir_entry *proc_ksunrpc_ut;

static int read_ksunrpc_ut(char *page, char **start, off_t off,
			   int count, int *eof, void *data)
{
	int ret;

	ret = sprintf(page + off, "%d\n", 1);
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

	printk("got data from userspace. testing...\n");
	xprt = ksunrpc_xprt_ops.ksxo_init(&id_in_kernel);
	printk("xprt = %p\n", xprt);
	if (!IS_ERR(xprt))
		ksunrpc_xprt_ops.ksxo_fini(xprt);

	return count;
}


static void __init create_ut_proc_entry(void)
{
	proc_ksunrpc_ut = create_proc_entry(UT_PROC_NAME, 0644, NULL);
	if (proc_ksunrpc_ut) {
		/* Why is this so hard? */
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
