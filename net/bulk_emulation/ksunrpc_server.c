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
 * Original author: David Cohrs <dave_cohrs@xyratex.com>
 * Original creation date: 06/06/2011
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <linux/kernel.h>
#include <linux/freezer.h>
#include <linux/string.h>
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/svcsock.h>

#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/thread.h"
#include "fop/fop.h"
#include "addb/addb.h"

#include "ksunrpc.h"
#include "sunrpc_io_k.h"
#include "net/bulk_emulation/sunrpc_xprt.h"

/**
   @addtogroup ksunrpc Sun RPC

   Sunrpc server implementation.  Based on the design of user-level
   sunrpc server implementation.

   @{
 */

enum {
	C2_SESSION_PROGRAM = 0x20000001,
	C2_DEF_RPC_VER = 1,
	KSUNRPC_XDRSIZE = C2_NET_BULK_SUNRPC_MAX_BUFFER_SIZE + 1024,
	KSUNRPC_BUFSIZE = C2_NET_BULK_SUNRPC_MAX_BUFFER_SIZE + 1024,
	KSUNRPC_MAXCONN = 1024,
	SERVER_THR_NR = 8,
	MIN_SERVER_THR_NR = 2
};

static struct svc_program ksunrpc_program;

static const struct c2_addb_loc ksunrpc_addb_server = {
	.al_name = "ksunrpc-server"
};

C2_ADDB_EV_DEFINE(ksunrpc_addb_req,       "req",
		  C2_ADDB_EVENT_USUNRPC_REQ, C2_ADDB_STAMP);
C2_ADDB_EV_DEFINE(ksunrpc_addb_opnotsupp, "EOPNOTSUPP",
		  C2_ADDB_EVENT_USUNRPC_OPNOTSURPPORT, C2_ADDB_INVAL);

#define ADDB_ADD(service, ev, ...) \
C2_ADDB_ADD(&(service)->s_addb, &ksunrpc_addb_server, ev , ## __VA_ARGS__)

#define ADDB_CALL(service, name, rc)				\
C2_ADDB_ADD(&(service)->s_addb, &ksunrpc_addb_server,		\
            c2_addb_func_fail, (name), (rc))

static struct c2_list ksunrpc_svc_list;
static struct c2_rwlock ksunrpc_lock;

struct ksunrpc_service {
	/**
	   back-pointer to c2_service
	 */
	struct c2_service      *s_service;
	/**
	   SUNRPC service handle for this service
	 */
	struct svc_serv	       *s_serv;
	/**
	   SUNRPC request handle for this service
	 */
	struct svc_rqst	       *s_rqst;
	/**
	   worker thread handle
	 */
	struct c2_thread	s_worker_thread;
	/**
	   The service is being shut down.
	 */
	bool			s_shutdown;
	/**
	   Service mutex, must be held across various sunrpc svc calls.
	 */
	struct c2_mutex		s_svc_mutex;
	/**
	   ksunrpc service link list
	 */
	struct c2_list_link     s_svc_link;
};

/**
   Allow all access in this limited implementation.
 */
static int ksunrpc_authenticate(struct svc_rqst *req)
{
	return SVC_OK;
}

static __be32 ksunrpc_proc_msg(struct svc_rqst *req, void *argp, void *resp)
{
	C2_IMPOSSIBLE("implement me");
	return 0;
}

static __be32 ksunrpc_proc_get(struct svc_rqst *req, void *argp, void *resp)
{
	C2_IMPOSSIBLE("implement me");
	return 0;
}

static __be32 ksunrpc_proc_put(struct svc_rqst *req, void *argp, void *resp)
{
	C2_IMPOSSIBLE("implement me");
	return 0;
}

/*
 * This is the generic XDR function. rqstp is either a rpc_rqst (client
 * side) or svc_rqst pointer (server side).
 * Encode functions always assume there's enough room in the buffer.
 */
static int ksunrpc_decode_msg(void *rqstp, __be32 *data, void *obj)
{
	C2_IMPOSSIBLE("implement me");
	return 0;
}

static int ksunrpc_encode_msg_resp(void *rqstp, __be32 *data, void *obj)
{
	C2_IMPOSSIBLE("implement me");
	return 0;
}

static int ksunrpc_encode_msg_rel(void *rqstp, __be32 *data, void *obj)
{
	C2_IMPOSSIBLE("implement me");
	return 0;
}

static int ksunrpc_decode_get(void *rqstp, __be32 *data, void *obj)
{
	C2_IMPOSSIBLE("implement me");
	return 0;
}

static int ksunrpc_encode_get_resp(void *rqstp, __be32 *data, void *obj)
{
	C2_IMPOSSIBLE("implement me");
	return 0;
}

static int ksunrpc_encode_get_rel(void *rqstp, __be32 *data, void *obj)
{
	C2_IMPOSSIBLE("implement me");
	return 0;
}

static int ksunrpc_decode_put(void *rqstp, __be32 *data, void *obj)
{
	C2_IMPOSSIBLE("implement me");
	return 0;
}

static int ksunrpc_encode_put_resp(void *rqstp, __be32 *data, void *obj)
{
	C2_IMPOSSIBLE("implement me");
	return 0;
}

static int ksunrpc_encode_put_rel(void *rqstp, __be32 *data, void *obj)
{
	C2_IMPOSSIBLE("implement me");
	return 0;
}

static void ksunrpc_op(struct c2_service *service,
		       struct c2_fop_type *fopt, struct svc_rqst *req)
{
	C2_IMPOSSIBLE("implement me");
}

static int ksunrpc_dispatch(struct svc_rqst *req, __be32 *statp)
{
	struct c2_net_op_table  *tab;
	struct c2_fop_type      *fopt;
	struct c2_service       *service = NULL;
	struct ksunrpc_service  *xs;

	c2_rwlock_read_lock(&ksunrpc_lock);
	c2_list_for_each_entry(&ksunrpc_svc_list,
			       xs, struct ksunrpc_service, s_svc_link) {
		if (xs->s_rqst == req) {
			service = xs->s_service;
			break;
		}
	}
	c2_rwlock_read_unlock(&ksunrpc_lock);
	C2_ASSERT(service != NULL);

	ADDB_ADD(service, ksunrpc_addb_req);
	tab = &service->s_table;
	if (tab->not_start <= req->rq_proc &&
	    req->rq_proc < tab->not_start + tab->not_nr) {
		fopt = tab->not_fopt[req->rq_proc - tab->not_start];
		C2_ASSERT(fopt != NULL);
		ksunrpc_op(service, fopt, req);
		*statp = rpc_success;
	} else {
		ADDB_ADD(service, ksunrpc_addb_opnotsupp, -EOPNOTSUPP);
		*statp = rpc_proc_unavail;
		C2_IMPOSSIBLE("how to really handle this?");
	}
	/* 0 == dropit, non-zero means send response */
	return 1;
}

static void ksunrpc_worker(struct c2_service *service)
{
	struct ksunrpc_service *xs = service->s_xport_private;

	/* try_to_freeze() is called from svc_recv() */
	set_freezable();

	while (!xs->s_shutdown) {
		int rc;
		long timeout = msecs_to_jiffies(1000);

		rc = svc_recv(xs->s_rqst, timeout);
		if (rc == -EAGAIN || rc == -EINTR)
			continue;

		if (rc < 0) {
			ADDB_CALL(service, "svc_recv", rc);
			schedule_timeout_interruptible(HZ);
			continue;
		}
		svc_process(xs->s_rqst);
	}
}

static void ksunrpc_service_stop(struct ksunrpc_service *xs)
{
	xs->s_shutdown = true;

	if (xs->s_worker_thread.t_func != NULL) {
		c2_thread_join(&xs->s_worker_thread);
		c2_thread_fini(&xs->s_worker_thread);
	}

	/* svc_* functions require protection by a mutex or BKL */
	c2_mutex_lock(&xs->s_svc_mutex);
	if (xs->s_rqst != NULL) {
		svc_exit_thread(xs->s_rqst);
		xs->s_rqst = NULL;
	}
	xs->s_serv = NULL;
	c2_mutex_unlock(&xs->s_svc_mutex);
}

static int ksunrpc_service_start(struct c2_service *service,
				 struct ksunrpc_service_id *xid)
{
	struct ksunrpc_service *xs = service->s_xport_private;
	struct svc_serv *serv;
	struct svc_xprt *xprt;
	struct svc_rqst *rqst;
	int rc = 0;

	C2_ASSERT(xs->s_serv == NULL);
	C2_ASSERT(xid->ssi_ver == C2_DEF_RPC_VER);

	/* svc_* functions require protection by a mutex or BKL */
	c2_mutex_lock(&xs->s_svc_mutex);

	serv = svc_create(&ksunrpc_program, KSUNRPC_BUFSIZE, NULL);
	if (serv == NULL) {
		rc = -ENOMEM;
		ADDB_ADD(service, c2_addb_oom);
		goto done;
	}
	xs->s_serv = serv;

	/* create transport/socket if it does not already exist */
	xprt = svc_find_xprt(serv, "tcp", PF_INET, 0);
	if (xprt != NULL) {
		svc_xprt_put(xprt);
	} else {
		rc = svc_create_xprt(serv, "tcp", PF_INET, xid->ssi_port,
				     SVC_SOCK_DEFAULTS);
		if (rc != 0)
			goto done;
	}

	/* set up for creating worker thread */
	rqst = svc_prepare_thread(serv, &serv->sv_pools[0]);
	if (IS_ERR(rqst)) {
		rc = PTR_ERR(rqst);
		ADDB_CALL(service, "svc_prepare_thread", rc);
		goto done;
	}
	xs->s_rqst = rqst;
	svc_sock_update_bufs(serv);
	serv->sv_maxconn = KSUNRPC_MAXCONN;

	/* create the worker thread */
	rc = C2_THREAD_INIT(&xs->s_worker_thread, struct c2_service *, NULL,
		            &ksunrpc_worker, service, "ksunrpc_worker");
        if (rc != 0) {
		ADDB_CALL(service, "ksunrpc_worker", rc);
		goto done;
	}
done:
	/* svc_destroy is a "put" and both svc_create and svc_prepare_thread
	   act like "get", so must always call svc_destroy once here; this
	   thread no longer uses the svc_serv.
	 */
	if (xs->s_serv != NULL)
		svc_destroy(xs->s_serv);
	c2_mutex_unlock(&xs->s_svc_mutex);

	if (rc != 0)
		ksunrpc_service_stop(xs);
	return rc;
}

static void ksunrpc_service_fini(struct c2_service *service)
{
	struct ksunrpc_service *xs = service->s_xport_private;

	ksunrpc_service_stop(xs);

	C2_ASSERT(xs->s_serv == NULL);
	c2_rwlock_write_lock(&ksunrpc_lock);
	c2_list_del(&xs->s_svc_link);
	c2_rwlock_write_unlock(&ksunrpc_lock);
	c2_mutex_fini(&xs->s_svc_mutex);
	c2_free(xs);
}

int ksunrpc_service_init(struct c2_service *service)
{
	struct ksunrpc_service    *xs;
	struct ksunrpc_service_id *xid;
	int                        result;

	C2_ALLOC_PTR(xs);
	if (xs != NULL) {
		c2_mutex_init(&xs->s_svc_mutex);
		c2_list_link_init(&xs->s_svc_link);
		xs->s_service = service;
		service->s_xport_private = xs;
		service->s_ops = &ksunrpc_service_ops;
		xid = service->s_id->si_xport_private;
		C2_ASSERT(service->s_id->si_ops == &ksunrpc_service_id_ops);
		result = ksunrpc_service_start(service, xid);
		if (result == 0) {
			c2_rwlock_write_lock(&ksunrpc_lock);
			c2_list_add(&ksunrpc_svc_list, &xs->s_svc_link);
			c2_rwlock_write_unlock(&ksunrpc_lock);
		}
	} else {
		C2_ADDB_ADD(&service->s_domain->nd_addb, &ksunrpc_addb_server,
			    c2_addb_oom);
		result = -ENOMEM;
	}
	return result;
}

/**
   Implementation of c2_service_ops::sio_reply_post.
 */
static void ksunrpc_reply_post(struct c2_service *service,
			       struct c2_fop *fop, void *cookie)
{
	struct c2_fop **ret = cookie;

	C2_ASSERT(*ret == NULL);
	*ret = fop;
}

int ksunrpc_server_init(void)
{
	c2_rwlock_init(&ksunrpc_lock);
	c2_list_init(&ksunrpc_svc_list);
	return 0;
}

void ksunrpc_server_fini(void)
{
	c2_list_fini(&ksunrpc_svc_list);
	c2_rwlock_fini(&ksunrpc_lock);
}

const struct c2_service_ops ksunrpc_service_ops = {
	.so_fini       = ksunrpc_service_fini,
	.so_reply_post = ksunrpc_reply_post
};

#define PROC(num, name, respsize)				\
 [num] = {							\
   .pc_func	= (svc_procfunc) ksunrpc_proc_##name,		\
   .pc_decode	= (kxdrproc_t) ksunrpc_decode_##name,		\
   .pc_encode	= (kxdrproc_t) ksunrpc_encode_##name##_resp,	\
   .pc_release	= (kxdrproc_t) ksunrpc_encode_##name##_rel,	\
   .pc_argsize	= sizeof(struct sunrpc_##name),			\
   .pc_ressize	= sizeof(struct sunrpc_##name##_resp),		\
   .pc_xdrressize = respsize,					\
 }

/**
   This would be generated by fop2c, but sunrpc is deprecated so this
   table, required by kernel sunrpc, is hard-coded.
 */
static struct svc_procedure ksunrpc_procedures[] = {
	PROC(30, msg, 0),
	PROC(31, get, 0),
	PROC(32, put, 0),
};

/**
   Service version.  In this limited implementation, only a single
   version is supported.
 */
static struct svc_version ksunrpc_version1 = {
	.vs_vers	= 1,
	.vs_nproc	= ARRAY_SIZE(ksunrpc_procedures),
	.vs_proc	= ksunrpc_procedures,
	.vs_xdrsize	= KSUNRPC_XDRSIZE,
	.vs_dispatch    = ksunrpc_dispatch
};

/**
   Table of versions.
 */
static struct svc_version *ksunrpc_versions[] = {
	[1] = &ksunrpc_version1,
};

/**
   Service statistics
 */
static struct svc_stat ksunrpc_svc_stats;

/**
   Sunrpc service program information
 */
#define KSUNRPC_NRVERS	ARRAY_SIZE(ksunrpc_versions)
static struct svc_program ksunrpc_program = {
	.pg_prog		= C2_SESSION_PROGRAM,
	.pg_nvers		= C2_DEF_RPC_VER,
	.pg_vers		= ksunrpc_versions,
	.pg_name		= "c2_service",
	.pg_class		= "c2_service",
	.pg_stats		= &ksunrpc_svc_stats,
	.pg_authenticate	= ksunrpc_authenticate
};

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
