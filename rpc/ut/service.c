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
#include "rpc/rpc2.h"
#include "rpc/rpc2_internal.h"

enum {
	C2_RPC_SERVICE_TYPE_FOO = 1,
	C2_RPC_SERVICE_TYPE_BAR,
	C2_RPC_SERVICE_TYPE_NON_EXISTENT,
};

/* Forward declaration */
static int
foo_service_alloc_and_init(struct c2_rpc_service_type *service_type,
			   const char                 *ep_addr,
			   const struct c2_uuid       *uuid,
			   struct c2_rpc_service     **out);

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

static int
foo_service_alloc_and_init(struct c2_rpc_service_type *service_type,
			   const char                 *ep_addr,
			   const struct c2_uuid       *uuid,
			   struct c2_rpc_service     **out)
{
	struct c2_rpc_service *service;
	struct foo_service    *foo_service;
	int                    rc;

	C2_UT_ASSERT(service_type == &foo_service_type);
	foo_alloc_and_init_called = true;

	*out = NULL;

	C2_ALLOC_PTR(foo_service);
	C2_UT_ASSERT(foo_service != NULL);

	service = &foo_service->f_service;
	rc = c2_rpc__service_init(service, &foo_service_type, ep_addr,
				  uuid, &foo_service_ops);
	C2_UT_ASSERT(rc == 0);

	/* service type specific initialisation */
	foo_service->f_x = FOO_SERVICE_DEFAULT_X;

	service->svc_state = C2_RPC_SERVICE_STATE_INITIALISED;
	*out = service;
	return 0;
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

static int
bar_alloc_and_init(struct c2_rpc_service_type *service_type,
		   const char                 *ep_addr,
		   const struct c2_uuid       *uuid,
		   struct c2_rpc_service     **out);

static const struct c2_rpc_service_type_ops bar_type_ops = {
	.rsto_alloc_and_init = bar_alloc_and_init,
};

C2_RPC_SERVICE_TYPE_DEFINE(static, bar_service_type, "bar",
			   C2_RPC_SERVICE_TYPE_BAR, &bar_type_ops);

static bool bar_alloc_and_init_called;

static int
bar_alloc_and_init(struct c2_rpc_service_type *service_type,
		   const char                 *ep_addr,
		   const struct c2_uuid       *uuid,
		   struct c2_rpc_service     **out)
{
	C2_UT_ASSERT(service_type == &bar_service_type);
	bar_alloc_and_init_called = true;
	*out = NULL;
	return -1;
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
	int                    rc;

	foo_alloc_and_init_called = false;
	rc = c2_rpc_service_alloc_and_init(&foo_service_type, foo_ep_addr,
					   &foo_uuid, &fr_service);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(foo_alloc_and_init_called);
	C2_UT_ASSERT(fr_service != NULL);

	foo_service = container_of(fr_service, struct foo_service, f_service);
	C2_UT_ASSERT(foo_service->f_x == FOO_SERVICE_DEFAULT_X);
	C2_UT_ASSERT(strcmp(foo_ep_addr, c2_rpc_service_get_ep_addr(fr_service))
		     == 0);

	bar_alloc_and_init_called = false;
	rc = c2_rpc_service_alloc_and_init(&bar_service_type, NULL, NULL,
					   &service);
	C2_UT_ASSERT(bar_alloc_and_init_called);
	C2_UT_ASSERT(rc != 0 && service == NULL);
}

static void conn_attach_detach_test(void)
{
	struct c2_rpc_machine     mock_rpc_machine;
	struct c2_rpc_chan        mock_rpc_chan;
	struct c2_rpc_conn        mock_conn;
	struct c2_net_end_point   mock_destep; /* Destination end-point */
	char                     *copy_of_foo_ep_addr;

	C2_SET0(&mock_rpc_machine);
	C2_SET0(&mock_conn);
	C2_SET0(&mock_rpc_chan);
	C2_SET0(&mock_destep);

	c2_sm_group_init(&mock_rpc_machine.rm_sm_grp);
	/* prepare mock rpc_machine */
	c2_rpc_services_tlist_init(&mock_rpc_machine.rm_services);

	/* prepare mock_destep */
	copy_of_foo_ep_addr = c2_alloc(strlen(foo_ep_addr) + 1);
	C2_UT_ASSERT(copy_of_foo_ep_addr != NULL);

	strcpy(copy_of_foo_ep_addr, foo_ep_addr);
	mock_destep.nep_addr = copy_of_foo_ep_addr;

	/* prepare mock_rpc_chan */
	mock_rpc_chan.rc_destep = &mock_destep;

	/* prepare mock rpc connection */
	mock_conn.c_sm.sm_state = C2_RPC_CONN_ACTIVE;
	mock_conn.c_rpc_machine = &mock_rpc_machine;
	mock_conn.c_rpcchan     = &mock_rpc_chan;

	c2_rpc_service_conn_attach(fr_service, &mock_conn);
	C2_UT_ASSERT(c2_rpc_service_invariant(fr_service));
	C2_UT_ASSERT(fr_service->svc_state ==
		     C2_RPC_SERVICE_STATE_CONN_ATTACHED);
	C2_UT_ASSERT(fr_service->svc_conn == &mock_conn);
	C2_UT_ASSERT(
		c2_rpc_services_tlist_head(&mock_rpc_machine.rm_services) ==
		     fr_service);

	c2_rpc_service_conn_detach(fr_service);
	C2_UT_ASSERT(c2_rpc_service_invariant(fr_service));
	C2_UT_ASSERT(fr_service->svc_state ==
		     C2_RPC_SERVICE_STATE_INITIALISED);
	C2_UT_ASSERT(
		c2_rpc_services_tlist_is_empty(&mock_rpc_machine.rm_services));


	c2_rpc_services_tlist_fini(&mock_rpc_machine.rm_services);

	c2_free(copy_of_foo_ep_addr);
	c2_sm_group_fini(&mock_rpc_machine.rm_sm_grp);
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
	.ts_name  = "rpc-service-ut",
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
C2_EXPORTED(rpc_service_ut);
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

