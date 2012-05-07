/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 06/15/2010
 */
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif

#include <arpa/inet.h>
#include <rpc/rpc.h>
#include <pthread.h>  /* pthread_key */
#include <unistd.h>    /* close() */

#include "lib/misc.h"  /* C2_SET0 */
#include "lib/errno.h"
#include "lib/cdefs.h"
#include "lib/rwlock.h"
#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/list.h"
#include "lib/queue.h"
#include "lib/thread.h"
#include "lib/cond.h"
#include "fop/fop.h"
#include "net/net_internal.h"
#include "addb/addb.h"

#include "usunrpc.h"
#include "usunrpc_internal.h"

/**
   @addtogroup usunrpc User Level Sun RPC
   @{
 */

/*
 * Server code.
 */

enum {
	SERVER_THR_NR = 8,
	MIN_SERVER_THR_NR = 2
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
static const struct c2_addb_loc usunrpc_addb_server = {
	.al_name = "usunrpc-server"
};

/**
   Work item, holding RPC operation argument and results.
*/
struct work_item {
	/**
	   argument.

	   @note this is dynamically allocated for every request, and should
           be freed after the request is done.
	*/
	struct c2_fop *wi_arg;
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

C2_ADDB_EV_DEFINE(usunrpc_addb_req,       "req",
		  C2_ADDB_EVENT_USUNRPC_REQ, C2_ADDB_STAMP);
C2_ADDB_EV_DEFINE(usunrpc_addb_opnotsupp, "EOPNOTSUPP",
		  C2_ADDB_EVENT_USUNRPC_OPNOTSURPPORT, C2_ADDB_INVAL);

#define ADDB_ADD(service, ev, ...) \
C2_ADDB_ADD(&(service)->s_addb, &usunrpc_addb_server, ev , ## __VA_ARGS__)

#define ADDB_CALL(service, name, rc)				\
C2_ADDB_ADD(&(service)->s_addb, &usunrpc_addb_server,		\
            c2_addb_func_fail, (name), (rc))

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
	struct c2_fop           *ret;
	c2_time_t                rdelay;
        bool                     sleeping = false;

	xs = service->s_xport_private;

	c2_mutex_lock(&xs->s_req_guard);
	while (1) {
		while (!xs->s_shutdown && c2_queue_is_empty(&xs->s_requests)) {
                        sleeping = true;
			c2_cond_wait(&xs->s_gotwork, &xs->s_req_guard);
                }
		if (xs->s_shutdown)
			break;
		ql = c2_queue_get(&xs->s_requests);
		wi = container_of(ql, struct work_item, wi_linkage);
		c2_mutex_unlock(&xs->s_req_guard);
                c2_net_domain_stats_collect(service->s_domain, NS_STATS_IN,
                        wi->wi_arg->f_type->ft_top->fft_layout->fm_sizeof,
                        &sleeping);

		ret = NULL;
		service->s_handler(service, wi->wi_arg, &ret);

		/*
		 * Currently reqh uses sunrpc, which expects a synchronous
		 * reply, so this loop is to support async reply by reqh.
		 */
		if (service->s_domain->nd_xprt != &c2_net_usunrpc_minimal_xprt)
			while (ret == NULL)
				c2_nanosleep(c2_time_set(&rdelay, 0, 1000000),
					     NULL);

		c2_rwlock_read_lock(&xs->s_guard);
		if (ret != NULL && !svc_sendreply(wi->wi_transp,
					     (xdrproc_t)c2_fop_uxdrproc,
					     (caddr_t)ret)) {
			ADDB_CALL(service, "sendreply", 0);
			svcerr_systemerr(wi->wi_transp);
		}

		/* free the arg and res. They are allocated in dispatch() */
		if (!svc_freeargs(wi->wi_transp, (xdrproc_t)c2_fop_uxdrproc,
				  (caddr_t) wi->wi_arg))
			ADDB_CALL(service, "freeargs", 0);
		xdr_free((xdrproc_t)c2_fop_uxdrproc, (caddr_t)ret);

		c2_fop_free(ret);
		c2_fop_free(wi->wi_arg);

		c2_rwlock_read_unlock(&xs->s_guard);

		/* free the work item. It is allocated in dispatch() */
		c2_free(wi);
		c2_mutex_lock(&xs->s_req_guard);
	}
	c2_mutex_unlock(&xs->s_req_guard);
}

/**
   Allocates memory for the argument, decodes the argument and creates a work
   item, puts it into the queue. After that it signals the worker thread to
   handle this request concurrently.
 */
static void usunrpc_op(struct c2_service *service,
		       struct c2_fop_type *fopt, SVCXPRT *transp)
{
	struct usunrpc_service *xs;
	struct work_item       *wi;
	struct c2_fop          *arg;
	int                     result;

	xs  = service->s_xport_private;
	arg = c2_fop_alloc(fopt, NULL);
	C2_ALLOC_PTR(wi);
	if (arg != NULL && wi != NULL) {
		result = svc_getargs(transp, (xdrproc_t)c2_fop_uxdrproc,
				     (caddr_t)arg);
		if (result) {
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
			ADDB_CALL(service, "getargs", 0);
			svcerr_decode(transp);
			result = -EPROTO;
		}
	} else {
		ADDB_ADD(service, c2_addb_oom);
		svcerr_systemerr(transp);
		result = -ENOMEM;
	}
	if (result != 0) {
		c2_fop_free(arg);
		c2_free(wi);
	}
}

/**
   dispatch.

   This dispatch() is called by the scheduler thread:
   c2_net_scheduler() -> svc_getreqset() -> ... -> sunrpc_dispatch()

   It finds suitable operation from the operations table and calls unsunrpc_op()
   to hand the operation off to a worker thread.
*/
static void usunrpc_dispatch(struct svc_req *req, SVCXPRT *transp)
{
	struct c2_net_op_table  *tab;
	struct c2_fop_type      *fopt;
	struct c2_service       *service;

	service = usunrpc_service_get();
	C2_ASSERT(service != NULL);

	ADDB_ADD(service, usunrpc_addb_req);
	tab = &service->s_table;
	if (tab->not_start <= req->rq_proc &&
	    req->rq_proc < tab->not_start + tab->not_nr) {
		fopt = tab->not_fopt[req->rq_proc - tab->not_start];
		C2_ASSERT(fopt != NULL);
		usunrpc_op(service, fopt, transp);
	} else if (req->rq_proc == NULLPROC) {
		svc_sendreply(transp, (xdrproc_t)xdr_void, (caddr_t)NULL);
	} else {
		ADDB_ADD(service, usunrpc_addb_opnotsupp, -EOPNOTSUPP);
		svcerr_noproc(transp);
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
	SVCXPRT                   *transp;
	int                        result;

	usunrpc_service_set(service);
	xservice = service->s_xport_private;

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
			ADDB_CALL(service, "svc_register", 0);
			svc_destroy(xservice->s_transp);
		}
	} else
		ADDB_CALL(service, "svctcp_create", 0);
	return result;
}

/**
   sunrpc_scheduler: equivalent to svc_run()

   @todo use svc_pollfd.
*/
static void usunrpc_scheduler(struct c2_service *service)
{
	struct usunrpc_service *xservice;
	static fd_set           listen_local;
	int                     ret;
	struct timeval          tv;

	usunrpc_service_set(service);
	xservice = service->s_xport_private;

	while (1) {
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
			ADDB_CALL(service, "select", ret);
		c2_rwlock_write_unlock(&xservice->s_guard);
	}

	if (xservice->s_transp != NULL) {
		bool shut_any = false;
		int i;

		svc_destroy(xservice->s_transp);
		xservice->s_transp = NULL;

		/* Ensure all transports created internally to svc_tcp for
		   accepted connections are also destroyed.  svc_destroy() on
		   primary transport does not do this, unfortunately.  The
		   shutdown() on the remaining sockets causes the transports to
		   be cleaned up when svc_getreqset() is called.  svc_exit()
		   cleans up remaining interal sunrpc svc resources.
		 */
		listen_local = svc_fdset;
		for (i = 0; i < FD_SETSIZE; ++i) {
			if (FD_ISSET(i, &listen_local)) {
				ret = shutdown(i, SHUT_RD);
				if (ret == 0)
					shut_any = true;
			}
		}
		c2_rwlock_write_lock(&xservice->s_guard);
		if (shut_any)
			svc_getreqset(&listen_local);
		c2_rwlock_write_unlock(&xservice->s_guard);
		svc_exit();
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
	if (xs->s_socket != -1) {
		close(xs->s_socket);
		xs->s_socket = -1;
	}
}

static int usunrpc_service_start(struct c2_service *service,
				 struct usunrpc_service_id *xid, int nr_workers)
{
	struct sockaddr_in     addr;
	int                    i;
	int                    rc;
	struct usunrpc_service *xservice;

	xservice = service->s_xport_private;

	xservice->s_progid  = xid->ssi_prog;
	xservice->s_version = xid->ssi_ver;

	C2_ASSERT(xservice->s_socket == -1);

        xservice->s_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (xservice->s_socket == -1) {
		ADDB_CALL(service, "socket", errno);
		return -errno;
	}

	i = 1;
	if (setsockopt(xservice->s_socket, SOL_SOCKET, SO_REUSEADDR,
		       &i, sizeof(i)) < 0) {
		ADDB_CALL(service, "reuseaddr", errno);
		rc = -errno;
		close(xservice->s_socket);
		return rc;
	}

        C2_SET0(&addr);
	addr.sin_family = AF_INET;
        addr.sin_port   = htons(xid->ssi_port);
        if (bind(xservice->s_socket,
		 (struct sockaddr *)&addr, sizeof addr) == -1) {
		ADDB_CALL(service, "bind", errno);
		rc = -errno;
		goto err;
	}

        xservice->s_nr_workers = nr_workers;
	C2_ALLOC_ARR(xservice->s_workers, nr_workers);
        if (xservice->s_workers == NULL) {
                rc = -ENOMEM;
		ADDB_ADD(service, c2_addb_oom);
		goto err;
        }

	/* create the scheduler thread */
	rc = C2_THREAD_INIT(&xservice->s_scheduler_thread, struct c2_service *,
		            &usunrpc_scheduler_init, &usunrpc_scheduler,
			    service, "usunrpc_sched");
        if (rc != 0) {
		ADDB_CALL(service, "scheduler_thread", rc);
		goto err;
	}

	/* create the worker threads */
        for (i = 0; i < nr_workers; i++) {
                rc = C2_THREAD_INIT(&xservice->s_workers[i],
				    struct c2_service *, NULL,
				    &usunrpc_service_worker, service,
				    "usunrpc_serv%d", i);
                if (rc) {
			ADDB_CALL(service, "worker_thread", rc);
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
	int                        result;

	C2_ALLOC_PTR(xservice);
	if (xservice != NULL) {
		int num_threads;
		c2_queue_init(&xservice->s_requests);
		c2_mutex_init(&xservice->s_req_guard);
		c2_rwlock_init(&xservice->s_guard);
		c2_cond_init(&xservice->s_gotwork);
		service->s_xport_private = xservice;
		service->s_ops = &usunrpc_service_ops;
		xid = service->s_id->si_xport_private;
		xservice->s_socket = -1;
		C2_ASSERT(service->s_id->si_ops == &usunrpc_service_id_ops);
		if (service->s_domain->nd_xprt == &c2_net_usunrpc_minimal_xprt)
			num_threads = MIN_SERVER_THR_NR;
		else
			num_threads = SERVER_THR_NR;
		result = usunrpc_service_start(service, xid, num_threads);
	} else {
		C2_ADDB_ADD(&service->s_domain->nd_addb, &usunrpc_addb_server,
			    c2_addb_oom);
		result = -ENOMEM;
	}
	return result;
}

/**
   Implementation of c2_service_ops::sio_reply_post.
 */
static void usunrpc_reply_post(struct c2_service *service,
			       struct c2_fop *fop, void *cookie)
{
	struct c2_fop **ret = cookie;

	C2_ASSERT(*ret == NULL);
	*ret = fop;
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
	.so_fini       = usunrpc_service_fini,
	.so_reply_post = usunrpc_reply_post
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
