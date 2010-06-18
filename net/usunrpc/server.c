/* -*- C -*- */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif

#include <arpa/inet.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <rpc/svc.h>
#include <pthread.h>  /* pthread_key */
#include <unistd.h>    /* close() */

#include "lib/cdefs.h"
#include "lib/rwlock.h"
#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/list.h"
#include "lib/queue.h"
#include "lib/thread.h"
#include "lib/cond.h"
#include "net/net.h"

#include "usunrpc_internal.h"

/**
   @addtogroup usunrpc User Level Sun RPC
   @{
 */

/*
 * Server code.
 */

enum {
	SERVER_THR_NR = 8
};

/**
   Multi-threaded sunrpc server implementation (designed by Nikita Danilov).

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
struct usunrpc_service {
	/**
	   SUNRPC transport handle for this service
	*/
	SVCXPRT                       *s_transp;
	/**
	   program ID for this sunrpc service
	*/
	unsigned long    	       s_progid;
	/**
	   program version
	*/
	unsigned long                  s_version;
	/**
	   service socket
	*/
	int			       s_socket;

	/**
	   scheduler thread handle
	*/
	struct c2_thread	       s_scheduler_thread;

	/**
	   number of worker threads
	*/
	int 			       s_nr_workers;
	/**
	   worker thread array
	*/
	struct c2_thread              *s_workers;

	/**
	   queue of received and not yet processed work items

	   This queue is protected by 'req_guard'.
	*/
	struct c2_queue s_requests;

	/** mutex protecting "requests" queue */
	struct c2_mutex s_req_guard;

	/**
	   read-write lock for synchronisation between the scheduler and the
	   workers.

	   Why this lock is needed? Because there is a shared state in sunrpc:
	   network buffers, XDR decoding and encoding state, etc. If workers
	   were allowed to send replies concurrently with scheduler parsing
	   incoming connections, such state would be corrupted.

	   Why this lock can be made read-write? Because the shared state in
	   sunrpc is per SVCXPRT. Different workers work on different
	   connections from the client (transport level connections, that is,
	   different sockets returned by accept(3) on the master listening
	   socket), because client waits for reply before sending next rpc
	   through the same connections (CLIENT structure).

	   Because of this, different workers can send replies in parallel. They
	   cannot send replies in parallel with svc_getreqset() call in the
	   scheduler because this call potentially works with all connections.
	*/
	struct c2_rwlock s_guard;

	/**
	   synchronization condition.

	   A condition variable that the worker threads wait upon. It is
	   signalled by the scheduler/dispatch after rpc arguments have been
	   parsed and work item queued. 
	*/
	struct c2_cond s_gotwork;
	/**
	   The service is being dismantled.
	*/
	bool           s_shutdown;
};

static pthread_key_t usunrpc_service_key;
static bool usunrpc_service_key_initialised = false;

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
	   operation.

	   Operation of this request, including xdr functions.
	*/
	const struct c2_rpc_op *wi_op;

	/**
	   sunrpc transport

	   This is the sunrpc transport to which reply must be sent. This has
	   to be remembered separately, because this transport can (and
           usually is) different from the transport on which rpc has been
	   received (the latter is associated with listen(2)-ing socket, while
	   the former is associated with accept(3)-ed socket.
	*/
	SVCXPRT          *wi_transp;
	/** linkage in the queue, protect by 'req_guard' */
	struct c2_queue_link wi_linkage;
};

static void usunrpc_service_set(struct c2_service *service)
{
	int rc;

	rc = pthread_setspecific(usunrpc_service_key, service);
	C2_ASSERT(rc == 0);
}

static struct c2_service *usunrpc_service_get(void)
{
	C2_ASSERT(usunrpc_service_key_initialised);
	return pthread_getspecific(usunrpc_service_key);
}

/**
   worker thread.

   The worker thread waits upon the usunrpc_service::s_gotwork condition
   variable, until the usunrpc_service::s_requests queue is not empty. It
   retrieves a request from the queue, and calls corresponding handler.

   The worker thread exits when usunrpc_service::s_shudown is set by
   service_stop().
*/
static void usunrpc_service_worker(struct c2_service *service)
{
	struct usunrpc_service  *xs;
	struct work_item        *wi;
	struct c2_queue_link    *ql;
	const struct c2_rpc_op  *op;
	int                      ret;
	void                    *res;

	xs = service->s_xport_private;

	c2_mutex_lock(&xs->s_req_guard);
	while (1) {
		while (!xs->s_shutdown && c2_queue_is_empty(&xs->s_requests))
			c2_cond_wait(&xs->s_gotwork, &xs->s_req_guard);
		if (xs->s_shutdown)
			break;
		ql = c2_queue_get(&xs->s_requests);
		wi = container_of(ql, struct work_item, wi_linkage);
		c2_mutex_unlock(&xs->s_req_guard);

		op = wi->wi_op;
		ret = (*op->ro_handler)(op, wi->wi_arg, &res);

		c2_rwlock_read_lock(&xs->s_guard);
		if (ret > 0 && !svc_sendreply(wi->wi_transp,
					      (xdrproc_t)op->ro_xdr_result,
					      (caddr_t)res)) {
			svcerr_systemerr(wi->wi_transp);
		}

		/* free the arg and res. They are allocated in dispatch() */
		/* XXX They are allocated by c2_alloc(), but not freed by
		   c2_free(). This will report some memory leak on some
		   platforms. */
		if (!svc_freeargs(wi->wi_transp, (xdrproc_t)op->ro_xdr_arg,
				  (caddr_t) wi->wi_arg)) {
			/* XXX bug */
		}
		xdr_free((xdrproc_t)op->ro_xdr_result, (caddr_t)res);

		c2_free(res);
		c2_free(wi->wi_arg);

		c2_rwlock_read_unlock(&xs->s_guard);

		/* free the work item. It is allocated in dispatch() */
		c2_free(wi);
		c2_mutex_lock(&xs->s_req_guard);
	}
	c2_mutex_unlock(&xs->s_req_guard);
}

/**
   dispatch.

   This dispatch() is called by the scheduler thread:
   c2_net_scheduler() -> svc_getreqset() -> ... -> sunrpc_dispatch()

   It finds suitable operation from the operations table, allocates proper
   argument and result buffer memory, decodes the argument, and then create a
   work item, puts it into the queue. After that it signals the worker thread to
   handle this request concurrently.
*/
static void usunrpc_dispatch(struct svc_req *req, SVCXPRT *transp)
{
	const struct c2_rpc_op  *op;
	struct work_item        *wi = NULL;
	struct c2_service       *service;
	struct usunrpc_service  *xs;
	void *arg = NULL;
	int   result;

	service = usunrpc_service_get();
	C2_ASSERT(service != NULL);

	xs = service->s_xport_private;
	op = c2_rpc_op_find(service->s_table, req->rq_proc);

	if (op != NULL) {
		arg = c2_alloc(op->ro_arg_size);
		C2_ALLOC_PTR(wi);
		if (arg != NULL && wi != NULL) {
			result = svc_getargs(transp, (xdrproc_t)op->ro_xdr_arg,
					     (caddr_t) arg);
			if (result) {
				wi->wi_op  = op;
				wi->wi_arg = arg;
				wi->wi_transp = transp;
				c2_mutex_lock(&xs->s_req_guard);
				c2_queue_put(&xs->s_requests, &wi->wi_linkage);
				c2_cond_signal(&xs->s_gotwork, 
					       &xs->s_req_guard);
				c2_mutex_unlock(&xs->s_req_guard);
				result = 0;
			} else {
				/* TODO XXX
				   How to pass the error code back to client?
				   If code reaches here, the client got timeout,
				   instead of error.
				*/
				svcerr_decode(transp);
				result = -EPROTO;
			}
		} else {
			svcerr_systemerr(transp);
			result = -ENOMEM;
		}
	} else {
		svcerr_noproc(transp);
		result = -EINVAL;
	}

	if (result != 0) {
		c2_free(arg);
		c2_free(wi);
	}
}

/**
   Init-call for sunrpc scheduler thread (sunrpc_scheduler()).

   @note svctcp_create() and svc_register() calls must be made in this thread,
   because they initialise sunrpc library internal per-thread state.
*/
static int usunrpc_scheduler_init(struct c2_service *service)
{
	struct usunrpc_service    *xservice;
	struct usunrpc_service_id *xid;
	SVCXPRT                   *transp;
	int                        result;

	usunrpc_service_set(service);
	xservice = service->s_xport_private;
	xid = service->s_id->si_xport_private;

	C2_ASSERT(xservice->s_socket >= 0);

	result = -EINVAL;
        transp = svctcp_create(xservice->s_socket, 0, 0);
        if (transp != NULL) {
		xservice->s_transp = transp;
		if (svc_register(transp, xservice->s_progid, 
				 xservice->s_version,
				 usunrpc_dispatch, 0))
			result = 0;
		else {
			fprintf(stderr, "error registering (%lu, %lu, %i).\n",
				xservice->s_progid, 
				xservice->s_version, xid->ssi_port);
			svc_destroy(xservice->s_transp);
		}
	} else
                fprintf(stderr, "svctcp_create failed\n");
	return result;
}

/**
   sunrpc_scheduler: equivalent to svc_run()

   @todo use svc_pollfd.
*/
static void usunrpc_scheduler(struct c2_service *service)
{
	struct usunrpc_service *xservice;

	usunrpc_service_set(service);
	xservice = service->s_xport_private;

	while (1) {
		static fd_set  listen_local;
		int            ret;
		struct timeval tv;

		tv.tv_sec  = 1;
		tv.tv_usec = 0;

		/* 'svc_fdset' is defined in sunrpc library */
		listen_local = svc_fdset;
		ret = select(FD_SETSIZE, &listen_local, NULL, NULL, &tv);

		if (xservice->s_shutdown)
			break;

		c2_rwlock_write_lock(&xservice->s_guard);
		if (ret > 0)
			svc_getreqset(&listen_local);
		else if (ret < 0)
			fprintf(stderr, "select failed\n");
		c2_rwlock_write_unlock(&xservice->s_guard);
	}

	if (xservice->s_transp != NULL) {
		svc_destroy(xservice->s_transp);
		xservice->s_transp = NULL;
	}
}

static void usunrpc_service_stop(struct usunrpc_service *xs)
{
	struct work_item      *wi;
	struct c2_queue_link  *ql;
	struct c2_thread      *thr;
	int i;

	/*
	 * Broadcast the condition on which workers are waiting, so that they
	 * see shutdown and exit.
	 */
	c2_mutex_lock(&xs->s_req_guard);
	xs->s_shutdown = true;
	c2_cond_broadcast(&xs->s_gotwork, &xs->s_req_guard);
	c2_mutex_unlock(&xs->s_req_guard);

	if (xs->s_workers != NULL) {
		for (i = 0; i < xs->s_nr_workers; i++) {
			thr = &xs->s_workers[i];
			if (thr->t_func != NULL) {
				c2_thread_join(thr);
				c2_thread_fini(thr);
			}
		}
		c2_free(xs->s_workers);
		xs->s_workers = NULL;
	}

	if (xs->s_scheduler_thread.t_func != NULL) {
		/* 
		 * Wait until scheduler sees the shutdown and exits. This might
		 * wait for select(2) timeout. See
		 * sunrpc_scheduler(). Alternatively, use a signal to kill
		 * scheduler thread and call svc_exit() from the signal
		 * handler. 
		 */
		c2_thread_join(&xs->s_scheduler_thread);
		c2_thread_fini(&xs->s_scheduler_thread);
	}
	/* Free all the remaining work items. Strictly speaking, the lock is not
	   needed, because all the threads are dead by now. */
        c2_mutex_lock(&xs->s_req_guard);
        while ((ql = c2_queue_get(&xs->s_requests)) != NULL) {
		wi = container_of(ql, struct work_item, wi_linkage);
		c2_free(wi->wi_arg);
		c2_free(wi);
	}
        c2_mutex_unlock(&xs->s_req_guard);

	/* close the service socket */
	/*
	 * XXX nikita: shouldn't sunrpc lib do this for us? If the library
	 * doesn't close the socket, shouldn't we call shutdown(2) here too?
	 */
	if (xs->s_socket != -1) {
		close(xs->s_socket);
		xs->s_socket = -1;
	}
}

static int usunrpc_service_start(struct c2_service *service,
				 enum c2_rpc_service_id prog_id,
				 int prog_version,
				 uint16_t port,
				 int nr_workers,
				 struct c2_rpc_op_table *ops)
{
	struct sockaddr_in     addr;
	int                    i;
	int                    rc;
	struct usunrpc_service *xservice;

	xservice = service->s_xport_private;

	xservice->s_progid  = prog_id;
	xservice->s_version = prog_version;

	C2_ASSERT(xservice->s_socket == -1);

        xservice->s_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (xservice->s_socket == -1) {
                fprintf(stderr, "socket error: %d\n", errno);
		return -errno;
	}

	i = 1;
	if (setsockopt(xservice->s_socket, SOL_SOCKET, SO_REUSEADDR,
		       &i, sizeof(i)) < 0) {
		rc = -errno;
		fprintf(stderr, "set socket option error: %d\n", errno);
		close(xservice->s_socket);
		return rc;
	}

        memset(&addr, 0, sizeof addr);
        addr.sin_port = htons(port);
        if (bind(xservice->s_socket, 
		 (struct sockaddr *)&addr, sizeof addr) == -1) {
                fprintf(stderr, "bind error: %d\n", errno);
		rc = -errno;
		goto err;
	}

        xservice->s_nr_workers = nr_workers;
	C2_ALLOC_ARR(xservice->s_workers, nr_workers);
        if (xservice->s_workers == NULL) {
                fprintf(stderr, "alloc thread handle failure\n");
                rc = -ENOMEM;
		goto err;
        }

	/* create the scheduler thread */
	rc = C2_THREAD_INIT(&xservice->s_scheduler_thread, struct c2_service *,
		            &usunrpc_scheduler_init, &usunrpc_scheduler,
			    service);
        if (rc != 0) {
                fprintf(stderr, "scheduler thread create: %d\n", rc);
		goto err;
	}

	/* create the worker threads */
        for (i = 0; i < nr_workers; i++) {
                rc = C2_THREAD_INIT(&xservice->s_workers[i], 
				    struct c2_service *, NULL, 
				    &usunrpc_service_worker, service);
                if (rc) {
                        fprintf(stderr, "worker thread create: %d\n", rc);
                        goto err;
                }
        }
	return 0;

 err:
	usunrpc_service_stop(xservice);
	return rc;
}

static void usunrpc_service_fini(struct c2_service *service)
{
	struct usunrpc_service *xs;

	xs = service->s_xport_private;

	usunrpc_service_stop(xs);

	C2_ASSERT(xs->s_workers == NULL);
	C2_ASSERT(xs->s_socket == -1);
	c2_queue_fini(&xs->s_requests);
	c2_cond_fini(&xs->s_gotwork);
	c2_mutex_fini(&xs->s_req_guard);
	c2_rwlock_fini(&xs->s_guard);
	c2_free(xs);
}

int usunrpc_service_init(struct c2_service *service)
{
	struct usunrpc_service    *xservice;
	struct usunrpc_service_id *xid;
	int                       result;

	C2_ALLOC_PTR(xservice);
	if (xservice != NULL) {
		c2_queue_init(&xservice->s_requests);
		c2_mutex_init(&xservice->s_req_guard);
		c2_rwlock_init(&xservice->s_guard);
		c2_cond_init(&xservice->s_gotwork);
		service->s_xport_private = xservice;
		service->s_ops = &usunrpc_service_ops;
		xid = service->s_id->si_xport_private;
		xservice->s_socket = -1;
		C2_ASSERT(service->s_id->si_ops == &usunrpc_service_id_ops);
		result = usunrpc_service_start(service, C2_SESSION_PROGRAM, 
					       C2_DEF_RPC_VER,
					       xid->ssi_port, SERVER_THR_NR,
					       service->s_table);
	} else
		result = -ENOMEM;
	return result;
}

int usunrpc_server_init(void)
{
	usunrpc_service_key_initialised = true;
	return pthread_key_create(&usunrpc_service_key, NULL);
}

void usunrpc_server_fini(void)
{
	pthread_key_delete(usunrpc_service_key);
	usunrpc_service_key_initialised = false;
}

const struct c2_service_ops usunrpc_service_ops = {
	.so_fini = usunrpc_service_fini
};

/** @} end of group usunrpc */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
