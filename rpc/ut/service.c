#include "lib/ut.h"
#include "rpc/service.h"

enum {
	C2_RPC_SERVICE_TYPE_FOO = 1,
	C2_RPC_SERVICE_TYPE_BAR,
	C2_RPC_SERVICE_TYPE_NON_EXISTENT,
};

/* Forward declaration */
static struct c2_rpc_service *
foo_alloc_and_init(struct c2_rpc_service_type *service_type);

static struct c2_rpc_service *
bar_alloc_and_init(struct c2_rpc_service_type *service_type);


static const struct c2_rpc_service_type_ops foo_type_ops = {
	.rsto_alloc_and_init = foo_alloc_and_init,
};

C2_RPC_SERVICE_TYPE_DEFINE(static, foo_service_type, "foo",
				C2_RPC_SERVICE_TYPE_FOO, &foo_type_ops);

static bool foo_alloc_and_init_called;

static struct c2_rpc_service *
foo_alloc_and_init(struct c2_rpc_service_type *service_type)
{
	C2_UT_ASSERT(service_type == &foo_service_type);
	foo_alloc_and_init_called = true;
	return NULL;
}

static const struct c2_rpc_service_type_ops bar_type_ops = {
	.rsto_alloc_and_init = bar_alloc_and_init,
};

C2_RPC_SERVICE_TYPE_DEFINE(static, bar_service_type, "bar",
				C2_RPC_SERVICE_TYPE_BAR, &bar_type_ops);

static bool bar_alloc_and_init_called;

static struct c2_rpc_service *
bar_alloc_and_init(struct c2_rpc_service_type *service_type)
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

static void alloc_test(void)
{
	struct c2_rpc_service *service;

	foo_alloc_and_init_called = false;
	service = c2_rpc_service_alloc_and_init(&foo_service_type);
	C2_UT_ASSERT(foo_alloc_and_init_called);

	bar_alloc_and_init_called = false;
	service = c2_rpc_service_alloc_and_init(&bar_service_type);
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
			{ "service-type-register",    register_test   },
			{ "service-type-locate-fail", locate_failed_test },
			{ "service-alloc",            alloc_test      },
			{ "service-type-unregister",  unregister_test },
			{ NULL,                       NULL            }
	}
};

