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
 * Original author: Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 02/27/2012
 */

#include "lib/ut.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "rpc/service.h"
#include "rpc/session.h"
#include "rpc/rpc2.h"

enum {
	C2_RPC_SERVICE_TYPE_FOO = 1,
	C2_RPC_SERVICE_TYPE_BAR,
	C2_RPC_SERVICE_TYPE_NON_EXISTENT,
};

/* Forward declaration */
static struct c2_rpc_service *
foo_service_alloc_and_init(struct c2_rpc_service_type *service_type,
			   const char                 *ep_addr,
			   const struct c2_uuid       *uuid);

static void foo_service_fini_and_free(struct c2_rpc_service *service);

enum {
	FOO_SERVICE_DEFAULT_X = 10,
};

struct foo_service {
	struct c2_rpc_service f_service;
	int                   f_x;
};

static const struct c2_rpc_service_type_ops foo_service_type_ops = {
	.rsto_alloc_and_init = foo_service_alloc_and_init,
};

C2_RPC_SERVICE_TYPE_DEFINE(static, foo_service_type, "foo",
				C2_RPC_SERVICE_TYPE_FOO, &foo_service_type_ops);

static const struct c2_rpc_service_ops foo_service_ops = {
	.rso_fini_and_free = foo_service_fini_and_free,
};

static bool foo_alloc_and_init_called;

static struct c2_rpc_service *
foo_service_alloc_and_init(struct c2_rpc_service_type *service_type,
			   const char                 *ep_addr,
			   const struct c2_uuid       *uuid)
{
	struct foo_service *foo_service;
	int                 rc;

	C2_UT_ASSERT(service_type == &foo_service_type);
	foo_alloc_and_init_called = true;

	C2_ALLOC_PTR(foo_service);
	if (foo_service == NULL)
		goto out_err;

	rc = c2_rpc__service_init(&foo_service->f_service,
					&foo_service_type, ep_addr,
					uuid, &foo_service_ops);
	if (rc != 0)
		goto out_err;

	/* service type specific initialisation */
	foo_service->f_x = FOO_SERVICE_DEFAULT_X;

	foo_service->f_service.svc_state = C2_RPC_SERVICE_STATE_INITIALISED;

	return &foo_service->f_service;

out_err:
	if (foo_service != NULL)
		c2_free(foo_service);
	return NULL;
}

static bool foo_service_fini_and_free_called;

static void foo_service_fini_and_free(struct c2_rpc_service *service)
{
	struct foo_service *foo_service;

	C2_PRE(service != NULL && c2_rpc_service_bob_check(service));

	foo_service_fini_and_free_called = true;

	foo_service = container_of(service, struct foo_service, f_service);

	/* Finalise service type specific fields of @service */
	foo_service->f_x = 0;

	c2_rpc__service_fini(service);
	c2_free(foo_service);
}

static struct c2_rpc_service *
bar_alloc_and_init(struct c2_rpc_service_type *service_type,
		   const char                 *ep_addr,
		   const struct c2_uuid       *uuid);

static const struct c2_rpc_service_type_ops bar_type_ops = {
	.rsto_alloc_and_init = bar_alloc_and_init,
};

C2_RPC_SERVICE_TYPE_DEFINE(static, bar_service_type, "bar",
				C2_RPC_SERVICE_TYPE_BAR, &bar_type_ops);

static bool bar_alloc_and_init_called;

static struct c2_rpc_service *
bar_alloc_and_init(struct c2_rpc_service_type *service_type,
		   const char                 *ep_addr,
		   const struct c2_uuid       *uuid)
{
	C2_UT_ASSERT(service_type == &bar_service_type);
	bar_alloc_and_init_called = true;
	return NULL;
}

static int rpc_service_ut_init(void)
{
	return 0;
}

static int rpc_service_ut_fini(void)
{
	return 0;
}

static void register_test(void)
{
	c2_rpc_service_type_register(&foo_service_type);
	C2_UT_ASSERT(c2_rpc_service_type_locate(C2_RPC_SERVICE_TYPE_FOO) ==
				&foo_service_type);

	c2_rpc_service_type_register(&bar_service_type);
	C2_UT_ASSERT(c2_rpc_service_type_locate(C2_RPC_SERVICE_TYPE_BAR) ==
				&bar_service_type);
}

static void locate_failed_test(void)
{
	C2_UT_ASSERT(
		c2_rpc_service_type_locate(C2_RPC_SERVICE_TYPE_NON_EXISTENT) ==
			NULL
	);
}

static struct c2_rpc_service *fr_service = NULL; /* fr_ for foo_rpc_ */
static const char             foo_ep_addr[] = "127.0.0.1:12345:1";
static struct c2_uuid         foo_uuid; /* Leave it unitialised */

static void alloc_test(void)
{
	struct c2_rpc_service *service;
	struct foo_service    *foo_service;

	foo_alloc_and_init_called = false;
	fr_service = c2_rpc_service_alloc_and_init(&foo_service_type, foo_ep_addr,
						  &foo_uuid);
	C2_UT_ASSERT(foo_alloc_and_init_called);
	C2_UT_ASSERT(fr_service != NULL);

	foo_service = container_of(fr_service, struct foo_service, f_service);
	C2_UT_ASSERT(foo_service->f_x == FOO_SERVICE_DEFAULT_X);
	C2_UT_ASSERT(strcmp(foo_ep_addr, c2_rpc_service_get_ep_addr(fr_service))
				== 0);

	bar_alloc_and_init_called = false;
	service = c2_rpc_service_alloc_and_init(&bar_service_type, NULL, NULL);
	C2_UT_ASSERT(bar_alloc_and_init_called);
}

static void conn_attach_detach_test(void)
{
	struct c2_rpcmachine mock_rpcmachine;
	struct c2_rpc_conn   mock_conn;

	C2_SET0(&mock_rpcmachine);
	C2_SET0(&mock_conn);

	/* prepare mock rpcmachine */
	c2_rpc_services_tlist_init(&mock_rpcmachine.cr_services);
	c2_mutex_init(&mock_rpcmachine.cr_session_mutex);

	/* prepare mock rpc connection */
	mock_conn.c_state = C2_RPC_CONN_ACTIVE;
	mock_conn.c_rpcmachine = &mock_rpcmachine;
	c2_mutex_init(&mock_conn.c_mutex);

	c2_rpc_service_attach_conn(fr_service, &mock_conn);
	C2_UT_ASSERT(c2_rpc_service_invariant(fr_service));
	C2_UT_ASSERT(fr_service->svc_state == C2_RPC_SERVICE_STATE_CONN_ATTACHED);
	C2_UT_ASSERT(fr_service->svc_conn == &mock_conn);
	C2_UT_ASSERT(c2_rpc_services_tlist_head(&mock_rpcmachine.cr_services) ==
				fr_service);

	c2_rpc_service_detach_conn(fr_service);
	C2_UT_ASSERT(c2_rpc_service_invariant(fr_service));
	C2_UT_ASSERT(fr_service->svc_state ==
			C2_RPC_SERVICE_STATE_CONN_DETACHED);
	C2_UT_ASSERT(
		c2_rpc_services_tlist_is_empty(&mock_rpcmachine.cr_services));

	c2_mutex_fini(&mock_conn.c_mutex);

	c2_mutex_fini(&mock_rpcmachine.cr_session_mutex);
	c2_rpc_services_tlist_fini(&mock_rpcmachine.cr_services);
}

static void service_free_test(void)
{
	foo_service_fini_and_free_called = false;
	c2_rpc_service_fini_and_free(fr_service);
	fr_service = NULL;
	C2_UT_ASSERT(foo_service_fini_and_free_called);
}

static void unregister_test(void)
{
	c2_rpc_service_type_unregister(&foo_service_type);
	C2_UT_ASSERT(c2_rpc_service_type_locate(C2_RPC_SERVICE_TYPE_FOO) ==
				NULL);

	c2_rpc_service_type_unregister(&bar_service_type);
	C2_UT_ASSERT(c2_rpc_service_type_locate(C2_RPC_SERVICE_TYPE_BAR) ==
				NULL);
}

const struct c2_test_suite rpc_service_ut = {
	.ts_name  = "rpc-service",
	.ts_init  = rpc_service_ut_init,
	.ts_fini  = rpc_service_ut_fini,
	.ts_tests = {
			/* Order of these tests matter */
			{ "service-type-register",    register_test      },
			{ "service-type-locate-fail", locate_failed_test },
			{ "service-alloc",            alloc_test         },
			{ "conn-attach-detach",       conn_attach_detach_test },
			{ "service-free",             service_free_test  },
			{ "service-type-unregister",  unregister_test    },
			{ NULL,                       NULL               }
	}
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

