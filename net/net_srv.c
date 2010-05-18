#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <assert.h>

#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>

#include "lib/cdefs.h"
#include "lib/memory.h"
#include "net/net.h"
#include "net/net_types.h"


/**
  Multithreaded sunrpc server implementation (designed by Nikita Danilov).

  The server executes rpc programs in the operations table in multiple threads
  concurrently. The following rpc related activity is still single-threaded:

      @li accepting socket connections and peek data from socket;
      @li parsing arguments (this can be easily fixed if necessary).

  The server starts N worker threads (c2_net_worker) and a scheduler thread
  (c2_net_scheduler()) listening for incoming data and distributing work among
  workers.

  When the scheduler detects incoming data, it, under the exclusive lock on
  "guard" read-write lock, calls svc_getreqset() function that eventually calls
  c2_net_dispatch(). c2_net_dispatch() creates "work item" (struct work_item)
  describing the rpc, queues it to a "requests" queue (stack actually), and
  signals "gotwork" condition variable on which all idle worker threads are
  waiting.

  A worker thread de-queues work item and executes coresponding operation.
  It then builds and sends a reply under a shared guard lock. After that,
  the dynamically allocated argument and reply buffer will be released.
 */

/**
  Work item, holding RPC operation argument and results.
 */
struct work_item {
	/**
	   argument.

	   @note this is dynamically allocated for every request, and should
           be freed after the request is done.
	 */
	void *wi_arg;
	/**
	   result.

	   @note this is dynamically allocated for every request, and should
           be freed after the request is done.
	 */
	void *wi_res;
	/**
	   operation.

	   Operation of this request, including xdr functions.
	 */
	const struct c2_rpc_op *wi_op;

	/**
	   Sunrpc transport
	   This is the sunrpc transport to which reply must be sent. This has
	   to be remembered separately, because this transport can (and
           usually is) different from the transport on which rpc has been
	   received (the latter is associated with listen(2)-ing socket, while
	   the former is associated with accept(3)-ed socket. */
	SVCXPRT          *wi_transp;
	/** request */
	struct svc_req   *wi_req;
	/** next work item in the queue, protect by 'req_guard' */
	struct work_item *wi_next;
};

/**
   queue of received and not yet processed work items

   This queue is protected by 'req_guard'.
 */
static struct work_item *requests = NULL;

/**
   read-write lock for synchronisation between the scheduler and the
   workers.

   Why this lock is needed? Because there is a shared state in sunrpc: network
   buffers, XDR decoding and encoding state, etc. If workers were allowed to
   send replies concurrently with scheduler parsing incoming connections, such
   state would be corrupted.

   Why this lock can be made read-write? Because the shared state in sunrpc is
   per SVCXPRT. Different workers work on different connections from the client
   (transport level connections, that is, different sockets returned by
   accept(3) on the master listening socket), because client waits for reply
   before sending next rpc through the same connections (CLIENT structure).

   Because of this, different workers can send replies in parallel. They cannot
   send replies in parallel with svc_getreqset() call in the scheduler because
   this call potentially works with all connections.
 */
static pthread_rwlock_t guard;

/** mutex protecting "requests" queue */
static pthread_mutex_t req_guard;

/**
   synchronization condition.

   A condition variable that the worker threads wait upon. It is signalled by
   the scheduler/dispatch after rpc arguments have been parsed and
   work item queued. */
static pthread_cond_t gotwork;

/**
   rpc operations table

   This rpc operations table is registered by c2_net_service_start().
   It will be used to handle all the incoming requests. This operations table
   is assumed to remain constant in the whole life cycle.
*/
static struct c2_rpc_op_table *g_c2_rpc_ops;


/**
   worker thread.

   This worker thread waits upon the 'gotwork' condition variable, until
   the 'requests' queue is not empy. It retrieves a request from the queue,
   and calls corresponding handler.

   This worker thread will not exit by itself, until it is killed when
   c2_net_service_stop() is called.
 */
static void *c2_net_worker(void *used)
{
	struct work_item *wi;
	const struct c2_rpc_op *op;
	int ret;

	while (1) {
		pthread_mutex_lock(&req_guard);
		while (requests == NULL)
			pthread_cond_wait(&gotwork, &req_guard);
		wi = requests;
		requests = wi->wi_next;
		pthread_mutex_unlock(&req_guard);

		op = wi->wi_op;
		ret = (*op->ro_handler)(wi->wi_arg, wi->wi_res);

		pthread_rwlock_rdlock(&guard);
		if (ret > 0 && !svc_sendreply(wi->wi_transp,
					     (xdrproc_t)op->ro_xdr_result,
				             (caddr_t)wi->wi_res)) {
			svcerr_systemerr(wi->wi_transp);
		}

		/* free the arg and res. They are allocated in dispatch() */
		/* XXX They are allocated by c2_alloc(), but not freed by
                       c2_free(). This will report some memory leak. */
		if (!svc_freeargs(wi->wi_transp, (xdrproc_t)op->ro_xdr_arg,
				 (caddr_t) wi->wi_arg)) {
			/* bug */
		}
		xdr_free((xdrproc_t)op->ro_xdr_result, (caddr_t)wi->wi_res);

		pthread_rwlock_unlock(&guard);

		/* free the work item. It is allocated in dispatch() */
		c2_free(wi);
	}
	return NULL;
}

/**
   dispatch.

   This dispatch() is called by the scheduler thread:
   c2_net_scheduler() -> svc_getreqset() -> ... -> c2_net_dispatch()

   It finds suitable operation from the operations table, allocates
   proper argument and result buffer memory, decodes the argument,
   and then create a work item, put it into the queue. After that it
   signals the worker thread to handle this request concurrently.
 */
static void c2_net_dispatch(struct svc_req *req, SVCXPRT *transp)
{
	const struct c2_rpc_op *op;
	struct work_item       *wi;
	void *arg;
	void *res;

	op = c2_rpc_op_find(g_c2_rpc_ops, req->rq_proc);
	if (op == NULL) {
		svcerr_noproc(transp);
		return;
	}

	arg = c2_alloc(op->ro_arg_size);
	if (arg == NULL) {
		svcerr_systemerr(transp);
		return;
	}

	res  = c2_alloc(op->ro_result_size);
	if (res == NULL) {
		svcerr_systemerr(transp);
		goto out_arg;
	}

	wi = c2_alloc(sizeof *wi);
	if (wi == NULL) {
		svcerr_systemerr(transp);
		goto out_res;
	}

	if (!svc_getargs(transp, (xdrproc_t)op->ro_xdr_arg, (caddr_t) arg)) {
		svcerr_decode(transp);
		goto out_res;
	}

	wi->wi_op  = op;
	wi->wi_arg = arg;
	wi->wi_res = res;
	wi->wi_transp = transp;
	wi->wi_req = req;
	pthread_mutex_lock(&req_guard);
	wi->wi_next = requests;
	requests = wi;
	pthread_cond_signal(&gotwork);
	pthread_mutex_unlock(&req_guard);
	return;

out_res:
	c2_free(res);
out_arg:
	c2_free(arg);
}

/**
  c2_net_scheduler: equivalent to svc_run()
*/
static void *c2_net_scheduler(void *unused)
{
	while (1) {
		static fd_set listen_local;
		int ret;
		struct timeval tv;

		tv.tv_sec = 3;
		tv.tv_usec = 0;

		/* 'svc_fdset' is defined in sunrpc library */
		listen_local = svc_fdset;
		ret = select(FD_SETSIZE, &listen_local, NULL, NULL, &tv);

		pthread_rwlock_wrlock(&guard);
		if (ret > 0)
			svc_getreqset(&listen_local);
		else if (ret < 0)
			fprintf(stderr, "select failed\n");
		pthread_rwlock_unlock(&guard);
	}
	return NULL;
}


int c2_net_service_start(enum c2_rpc_service_id prog_id,
			 int prog_version,
			 int port,
			 int number_of_worker_threads,
			 struct c2_rpc_op_table *ops,
			 struct c2_service *service)
{
	struct c2_service_thread_data *worker_thread_array;
        pthread_attr_t attr;
        SVCXPRT *transp;
	struct sockaddr_in addr;
	int sock;
	int i;
	int rc;

        setbuf(stdout, NULL);
        setbuf(stderr, NULL);

        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1) {
                fprintf(stderr, "socket error: %d\n", errno);
		return -1;
	}

        memset(&addr, 0, sizeof addr);
        addr.sin_port = htons(port);
        if (bind(sock, (struct sockaddr *)&addr, sizeof addr) == -1) {
                fprintf(stderr, "bind error: %d\n", errno);
		rc = -1;
		goto out_socket;
	}

        pmap_unset(prog_id, prog_version);

        pthread_rwlock_init(&guard, NULL);
        pthread_mutex_init(&req_guard, NULL);
        pthread_cond_init(&gotwork, NULL);


        service->s_number_of_worker_threads = number_of_worker_threads;
	worker_thread_array = (struct c2_service_thread_data *)
               c2_alloc(number_of_worker_threads * sizeof(struct c2_service_thread_data));

        if (worker_thread_array == NULL) {
                fprintf(stderr, "alloc thread handle failure\n");
                rc = -ENOMEM;
		goto out_socket;
        }
	service->s_worker_thread_array = worker_thread_array;

        rc = pthread_attr_init(&attr);
        if (rc) {
                fprintf(stderr, "pthread_attr_init:(%d)\n", rc);
		goto out_free_array;
        }
	/* or should PTHREAD_CREATE_DETACHED be used? */
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

        transp = svctcp_create(sock, 0, 0);
        if (transp == NULL) {
                fprintf(stderr, "svctcp_create failed\n");
                rc = -1;
		goto out_free_array;
        }
        if (!svc_register(transp, prog_id, prog_version, c2_net_dispatch, 0)) {
                fprintf(stderr, "unable to register (%d, %d, tcpport=%d).\n",
			prog_id, prog_version, port);
		rc = -1;
		goto out_transp;
        }
	service->s_transp = transp;

	/* create the scheduler thread */
	rc = pthread_create(&service->s_scheduler_thread, &attr,
		             c2_net_scheduler, service);
        if (rc) {
                fprintf(stderr, "scheduler pthread_create:(%d)\n", rc);
		goto out_transp;
	}

	/* create the worker threads */
        for (i = 0; i < number_of_worker_threads; i++) {
                worker_thread_array[i].std_handle = 0;

                rc = pthread_create(&worker_thread_array[i].std_handle, &attr,
                                    c2_net_worker,
				    &worker_thread_array[i]);
                if (rc) {
                        fprintf(stderr, "worker pthread_create:(%d)\n", rc);
                        goto out_kill_scheduler;
                }
        }

	/* save this ops in the global operation table */
	g_c2_rpc_ops = ops;
	return 0;

out_kill_scheduler:
	pthread_kill(service->s_scheduler_thread, 9);
out_transp:
	svc_destroy(transp);
out_free_array:
	free(worker_thread_array);
out_socket:
	close(sock);

	service->s_scheduler_thread = 0;
	service->s_transp = NULL;
	service->s_worker_thread_array = NULL;
	return rc;
}

int c2_net_service_stop(struct c2_service *service)
{
	int i;

	/* kill worker thread */
	if (service->s_worker_thread_array) {
		for (i = 0; i < service->s_number_of_worker_threads; i++)
			pthread_kill(service->s_worker_thread_array[i].std_handle, 9);
		free(service->s_worker_thread_array);
		service->s_worker_thread_array = NULL;
	}

	/* kill scheduler thread */
	pthread_kill(service->s_scheduler_thread, 9);
	service->s_scheduler_thread = 0;

	service->s_transp = NULL;
	/* close the service socket */
	close(service->s_socket);

	return 0;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
