#include "lib/ut.h"
#include "lib/memory.h"
#include "rpc/service.h"

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

static struct c2_rpc_service *
bar_alloc_and_init(struct c2_rpc_service_type *service_type,
		   const char                 *ep_addr,
		   const struct c2_uuid       *uuid);

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

	return &foo_service->f_service;

out_err:
	if (foo_service != NULL)
		c2_free(foo_service);
	return NULL;
}

static void foo_service_fini_and_free(struct c2_rpc_service *service)
{
	struct foo_service *foo_service;

	C2_PRE(service != NULL && c2_rpc_service_bob_check(service));

	foo_service = container_of(service, struct foo_service, f_service);

	/* Finalise service type specific fields of @service */
	foo_service->f_x = 0;

	c2_rpc__service_fini(service);
	c2_free(foo_service);
}

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

static const char      foo_ep_addr[] = "127.0.0.1:12345:1";
static struct c2_uuid  foo_uuid; /* Leave it unitialised */

static void alloc_test(void)
{
	struct c2_rpc_service *service;
	struct foo_service    *foo_service;

	foo_alloc_and_init_called = false;
	service = c2_rpc_service_alloc_and_init(&foo_service_type, foo_ep_addr,
						&foo_uuid);
	C2_UT_ASSERT(foo_alloc_and_init_called);

	foo_service = container_of(service, struct foo_service, f_service);
	C2_UT_ASSERT(foo_service->f_x == FOO_SERVICE_DEFAULT_X);
	C2_UT_ASSERT(strcmp(foo_ep_addr, c2_rpc_service_get_ep_addr(service))
				== 0);

	bar_alloc_and_init_called = false;
	service = c2_rpc_service_alloc_and_init(&bar_service_type, NULL, NULL);
	C2_UT_ASSERT(bar_alloc_and_init_called);
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
			{ "service-type-register",    register_test   },
			{ "service-type-locate-fail", locate_failed_test },
			{ "service-alloc",            alloc_test      },
			{ "service-type-unregister",  unregister_test },
			{ NULL,                       NULL            }
	}
};

