/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 05/30/2010
 */

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
#include <linux/string.h>
#include <linux/in.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xdr.h>

#include "lib/errno.h"
#include "lib/cdefs.h"
#include "lib/memory.h"
#include "fop/fop.h"

#include "net/ksunrpc/ksunrpc.h"

/**
   @addtogroup ksunrpc Sun RPC

   Kernel User level Sunrpc-based implementation of C2 networking interfaces.

  @{
 */

#define UT_PROC_NAME "ksunrpc-ut"

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

/**
  Finalize a kernel sunrpc connection.
*/
static void ksunrpc_conn_fini(struct c2_net_conn *conn)
{
	struct ksunrpc_conn *kconn = conn->nc_xprt_private;

	if (kconn == NULL)
		return;

	if (kconn->ksc_xprt != NULL) {
		struct ksunrpc_xprt *xprt = kconn->ksc_xprt;

		c2_mutex_fini(&xprt->ksx_lock);
		if (xprt->ksx_client != NULL) {
			rpc_shutdown_client(xprt->ksx_client);
		}
		kfree(xprt);
	}
	c2_free(kconn);
}

#define C2_DEF_TCP_RETRANS (2)
#define C2_DEF_TCP_TIMEO   (600)

/**
  Init a kernel sunrpc connection to the given service id.

  @param id the target service id;
  @param conn the returned connection will be stored there;
*/
static int ksunrpc_conn_init(struct c2_service_id *id, struct c2_net_conn *conn)
{
	struct ksunrpc_service_id *ksid = id->si_xport_private;
	struct rpc_timeout       timeparms = {
		.to_retries   = C2_DEF_TCP_RETRANS,
		.to_initval   = C2_DEF_TCP_TIMEO * HZ / 10,
		.to_increment = C2_DEF_TCP_TIMEO * HZ / 10,
		.to_maxval    = C2_DEF_TCP_TIMEO * HZ / 10 +
			       (C2_DEF_TCP_TIMEO * HZ / 10 * C2_DEF_TCP_RETRANS),
		.to_exponential = 0
	};

	struct rpc_create_args args = {
#ifdef HAVE_STRUCT_NET
		.net		= &init_net,
#endif
		.protocol	= XPRT_TRANSPORT_TCP,
		.address	= (struct sockaddr *)&ksid->ssi_sockaddr,
		.addrsize	= ksid->ssi_addrlen,
		.timeout	= &timeparms,
		.servername	= ksid->ssi_host,
		.program	= &c2t1_program,
		.version	= C2_DEF_RPC_VER,
		.authflavor	= RPC_AUTH_NULL,
//		.flags          = RPC_CLNT_CREATE_DISCRTRY,
	};
	int                 result;
	struct rpc_clnt     *clnt;
	struct ksunrpc_xprt *ksunrpc_xprt;
	struct ksunrpc_conn *kconn;

	result = -ENOMEM;
	C2_ALLOC_PTR(kconn);

	if (kconn != NULL) {
		ksunrpc_xprt = c2_alloc(sizeof *ksunrpc_xprt);
		if (ksunrpc_xprt != NULL) {
			clnt = rpc_create(&args);
			if (IS_ERR(clnt)) {
				printk("%s: cannot create RPC client. "
					"Error = %ld\n",
					__FUNCTION__, PTR_ERR(clnt));
				result = PTR_ERR(clnt);
			} else {
				ksunrpc_xprt->ksx_client = clnt;
				c2_mutex_init(&ksunrpc_xprt->ksx_lock);
				kconn->ksc_xprt = ksunrpc_xprt;
				conn->nc_xprt_private = kconn;
				conn->nc_ops = &ksunrpc_conn_ops;
				result = 0;
			}
		}
	}

        return result;
}

static int ksunrpc_conn_call(struct c2_net_conn *conn, struct c2_net_call *call)
{
	struct c2_fop_type  *arg_fopt = call->ac_arg->f_type;
	struct c2_fop_type  *ret_fopt = call->ac_ret->f_type;
	struct ksunrpc_conn *kconn;
	struct ksunrpc_xprt *kxprt;
	int                  result;


	struct rpc_procinfo proc = {
		.p_proc   = arg_fopt->ft_rpc_item_type.rit_opcode,
		.p_encode = (kxdrproc_t) c2_kcall_enc,
		.p_decode = (kxdrproc_t) c2_kcall_dec,
		.p_arglen = arg_fopt->ft_top->fft_layout->fm_sizeof,
		.p_replen = ret_fopt->ft_top->fft_layout->fm_sizeof,
		.p_statidx= 1,
		.p_name   = (char *)arg_fopt->ft_name
	};

        struct rpc_message msg = {
                .rpc_proc = &proc,
                .rpc_argp = call,
                .rpc_resp = call,
        };


	kconn  = conn->nc_xprt_private;
	kxprt  = kconn->ksc_xprt;

	c2_mutex_lock(&kxprt->ksx_lock);
        result = rpc_call_sync(kxprt->ksx_client, &msg, 0);
	c2_mutex_unlock(&kxprt->ksx_lock);
	return result;
}

static int ksunrpc_conn_send(struct c2_net_conn *conn, struct c2_net_call *call)
{
	/* TODO */
	return -ENOENT;
}


/**
  Fini a kernel sunrpc service id.

  @param id the target id to be finalized.
*/
static void ksunrpc_service_id_fini(struct c2_service_id *id)
{
	/* c2_free(NULL) is a no-op */
	c2_free(id->si_xport_private);
}

/**
  Init a kernel sunrpc service id from args.

  @param sid the result service id will be stored there.
  @param varagrs variable size of args can be passed.
*/
int ksunrpc_service_id_init(struct c2_service_id *sid, va_list varargs)
{
	struct ksunrpc_service_id *ksid;
	int                        result;

	C2_ALLOC_PTR(ksid);
	if (ksid != NULL) {
		char * hostname;
		sid->si_xport_private = ksid;
		ksid->ssi_id = sid;

		hostname = va_arg(varargs, char *);
		strncpy(ksid->ssi_host, hostname, ARRAY_SIZE(ksid->ssi_host)-1);
		ksid->ssi_port = va_arg(varargs, int);
		ksid->ssi_addrlen    = sizeof ksid->ssi_sockaddr;
		ksid->ssi_sockaddr.sin_family      = AF_INET;
		ksid->ssi_sockaddr.sin_port        = htons(ksid->ssi_port);
		ksid->ssi_sockaddr.sin_addr.s_addr = in_aton(hostname);
		ksid->ssi_prog = C2_SESSION_PROGRAM;
		ksid->ssi_ver  = C2_DEF_RPC_VER;
		sid->si_ops = &ksunrpc_service_id_ops;
		result = 0;
	} else
		result = -ENOMEM;
	return result;
}

/**
  kernel sunrpc service id operations
*/
const struct c2_service_id_ops ksunrpc_service_id_ops = {
	.sis_conn_init = ksunrpc_conn_init,
	.sis_fini      = ksunrpc_service_id_fini
};

/**
  kernel sunrpc connection operations.
*/
const struct c2_net_conn_ops ksunrpc_conn_ops = {
	.sio_fini = ksunrpc_conn_fini,
	.sio_call = ksunrpc_conn_call,
	.sio_send = ksunrpc_conn_send
};


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
	struct ksunrpc_service_id id_in_kernel = {
		.ssi_host = host,
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

	if (copy_from_user(&id_in_kernel.ssi_sockaddr, id_in_user.ssi_sockaddr, sizeof sa))
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

MODULE_AUTHOR("Huang Hua <Hua_Huang@xyratex.com>");
MODULE_DESCRIPTION("C2 Kernel Client RPC");
MODULE_LICENSE("GPL");
#endif

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
