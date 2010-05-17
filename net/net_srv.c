#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <rpc/pmap_clnt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <rpc/auth.h>
#include <rpc/svc.h>

#include <unistd.h> /* fork */
#include <signal.h> /* kill */

#include <errno.h>
#include <stdlib.h> /* exit */

#include "lib/cdefs.h"
#include "lib/memory.h"
#include "net/net.h"
#include "net/net_types.h"

static struct c2_rpc_op_table *g_c2_rpc_ops;

void c2_net_srv_fn_generic(struct svc_req *req, SVCXPRT *transp)
{
	bool retval;
	const struct c2_rpc_op *op;
	void *arg;
	void *ret;

	op = c2_rpc_op_find(g_c2_rpc_ops, req->rq_proc);
	if (op == NULL) {
		svcerr_noproc(transp);
		return;
	}

	arg = c2_alloc(op->ro_arg_size);
	if (!arg) {
		svcerr_systemerr(transp);
		return;
	}

	if (!svc_getargs(transp, (xdrproc_t) op->ro_xdr_arg,
			 (caddr_t) arg)) {
		svcerr_decode(transp);
		goto out;
	}

	ret  = c2_alloc(op->ro_result_size);
	if (!ret) {
		svcerr_systemerr(transp);
		goto out;
	}

	/** XXX need auth code */
	retval = (*op->ro_handler)(arg, ret);
	if (retval && !svc_sendreply(transp,
				     (xdrproc_t) op->ro_xdr_result,
				     ret)) {
		svcerr_systemerr(transp);
	}

	if (!svc_freeargs(transp, (xdrproc_t) op->ro_xdr_arg,
			  (caddr_t) arg)) {
		/* bug */
	}

	xdr_free ((xdrproc_t) op->ro_xdr_result, (caddr_t) ret);
	c2_free(ret);
out:
	c2_free(arg);
}

void c2_net_service_thread(void *data)
{
        struct c2_service_thread_data *pdata = (struct c2_service_thread_data *)data;
	SVCXPRT *transp;

	pmap_unset(pdata->std_progid, C2_DEF_RPC_VER);

	transp = svctcp_create(RPC_ANYSOCK, 0, 0);
	if (transp == NULL) {
                fprintf(stderr, "svc_register failure\n");
		return;
	}

	if (!svc_register(transp, pdata->std_progid, C2_DEF_RPC_VER,
			  c2_net_srv_fn_generic, IPPROTO_TCP)) {
                fprintf(stderr, "svc_register failure\n");
		return;
	}

	pdata->std_transp = transp;
	svc_run();
	return;
}

int c2_net_service_start(enum c2_rpc_service_id prog_id,
			 int num_of_threads,
			 struct c2_rpc_op_table *ops,
			 struct c2_service *service)
{
        pthread_attr_t attr;
	struct c2_service_thread_data *thread_data_array;
        struct timespec ts = {0, 1000*1000*50}; /* .05 sec */
	int i;
	int rc;

        service->s_number_of_threads = num_of_threads;
	thread_data_array = (struct c2_service_thread_data *)
               c2_alloc(num_of_threads * sizeof(struct c2_service_thread_data));

        if (thread_data_array == NULL) {
                fprintf(stderr, "alloc thread handle failure\n");
                return -ENOMEM;
        }
	service->s_thread_data_array = thread_data_array;

        rc = pthread_attr_init(&attr);
        if (rc) {
                fprintf(stderr, "pthread_attr_init:(%d)\n", rc);
		return rc;
        }
	
	/* or should PTHREAD_CREATE_DETACHED be used? */
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

        for (i = 0; i < num_of_threads; i++) {
                thread_data_array[i].std_progid = prog_id + i;
                thread_data_array[i].std_handle = 0;
                thread_data_array[i].std_transp = NULL;

                rc = pthread_create(&thread_data_array[i].std_handle, &attr,
                                    (void *(*)(void*))c2_net_service_thread,
				    &thread_data_array[i]);
                if (rc) {
                        fprintf(stderr, "pthread_create:(%d)\n", rc);
                        return rc;
                }
                nanosleep(&ts, NULL);
        }

	/* save this ops in the global operation table */
	g_c2_rpc_ops = ops;

	return 0;
}

int c2_net_service_stop(struct c2_service *service)
{
	int i;
        for (i = 0; i < service->s_number_of_threads; i++)
                pthread_kill(service->s_thread_data_array[i].std_handle, 9);

	return 0;
}

