/* -*- C -*- */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

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

#include "lib/cdefs.h"
#include "fop/fop.h"

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
EXPORT_SYMBOL(ksunrpc_xprt_fini);

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

	struct rpc_create_args args = {
		.protocol	= XPRT_TRANSPORT_TCP,
		.address	= (struct sockaddr *)xsid->ssi_sockaddr,
		.addrsize	= xsid->ssi_addrlen,
		.timeout	= &timeparms,
		.servername	= xsid->ssi_host,
		.program	= &c2t1_program,
		.version	= C2_DEF_RPC_VER,
		.authflavor	= RPC_AUTH_NULL,
//		.flags          = RPC_CLNT_CREATE_DISCRTRY,
	};

	struct rpc_clnt         *clnt;
	struct ksunrpc_xprt     *ksunrpc_xprt;

	ksunrpc_xprt = kmalloc(sizeof *ksunrpc_xprt, GFP_KERNEL);
	if (ksunrpc_xprt == NULL)
		return ERR_PTR(-ENOMEM);

	clnt = rpc_create(&args);
	if (IS_ERR(clnt)) {
		printk("%s: cannot create RPC client. Error = %ld\n",
			__FUNCTION__, PTR_ERR(clnt));
			return (struct ksunrpc_xprt *)clnt;
	}

	ksunrpc_xprt->ksx_client = clnt;
        return ksunrpc_xprt;
}
EXPORT_SYMBOL(ksunrpc_xprt_init);

static int ksunrpc_xprt_call(struct ksunrpc_xprt *xprt, 
			     struct c2_knet_call *kcall)
{
	struct c2_fop_type *arg_fopt = kcall->ac_arg->f_type;
	struct c2_fop_type *ret_fopt = kcall->ac_ret->f_type;

	struct rpc_procinfo proc = {
		.p_proc   = arg_fopt->ft_code,
		.p_encode = (kxdrproc_t) c2_kcall_enc,
		.p_decode = (kxdrproc_t) c2_kcall_dec,
		.p_arglen = arg_fopt->ft_top->fft_layout->fm_sizeof,
		.p_replen = ret_fopt->ft_top->fft_layout->fm_sizeof,
		.p_statidx= 1,
		.p_name   = (char *)arg_fopt->ft_name
	};

        struct rpc_message msg = {
                .rpc_proc = &proc,
                .rpc_argp = kcall,
                .rpc_resp = kcall,
        };

        return rpc_call_sync(xprt->ksx_client, &msg, 0);
}

struct ksunrpc_xprt_ops ksunrpc_xprt_ops = {
	.ksxo_init = ksunrpc_xprt_init,
	.ksxo_fini = ksunrpc_xprt_fini,
	.ksxo_call = ksunrpc_xprt_call,
};

EXPORT_SYMBOL(ksunrpc_xprt_ops);

/*******************************************************************************
 *                     Start of UT                                             *
 ******************************************************************************/

#if 0

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
                        .wa_pageoff  = 0,
			.wa_pages  = pp
		};
		struct c2t1fs_write_ret wr;
                struct c2t1fs_create_arg ca = {
			.ca_fid    = { .f_d1 = 10, .f_d2 = 10 }
                };
                struct c2t1fs_create_res cres;

		printk("sending SIF_CREATE to server\n");
		retval = ksunrpc_xprt_ops.ksxo_call(xprt, &create_op, &ca, &cres);
		printk("got reply: retval=%d, result=%d\n", retval, cres.res);

		pp[0] = alloc_page(GFP_KERNEL);
		BUG_ON(pp[0] == NULL);

		memcpy(page_address(pp[0]), "sending SIF_WRITE to server\n", 256);
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
