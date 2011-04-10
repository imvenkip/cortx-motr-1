/* -*- C -*- */

#include "lib/ut.h"
#include "lib/ub.h"
#include "rm/rm.h"

/**
   @addtogroup rm

   @{
 */

/**
   @name Use cases and unit tests
 */

/** @{ */

static int rm_reset(void)
{
	return 0;
}

/**
   <b>Intent mode.</b>
 */
static void intent_mode_test(void)
{
}

/**
   <b>WBC mode.</b>
 */
static void wbc_mode_test(void)
{
}

/**
   <b>Resource manager separate from resource provider.<b>
 */
static void separate_test(void)
{
}

/**
   <b>Network partitioning, leases.</b>
 */
static void lease_test(void)
{
}

/**
   <b>Optimistic concurrency.</b>
 */
static void optimist_test(void)
{
}

/**
   <b>A use case for each resource type.</b>
 */
static void resource_type_test(void)
{
}

/**
   <b>Use cases involving multiple resources acquired.</b>
 */
static void multiple_test(void)
{
}

/**
   <b>Cancellation.</b>
 */
static void cancel_test(void)
{
}

/**
   <b>Caching.</b>
 */
static void caching_test(void)
{
}

/**
   <b>Call-back.</b>
 */
static void callback_test(void)
{
}

/**
   <b>Hierarchy.</b>
 */
static void hierarchy_test(void)
{
}

/**
   <b>Extents and multiple readers-writers (scalability).</b>
 */
static void ext_multi_test(void)
{
}


const struct c2_test_suite rm_ut = {
	.ts_name = "librm-ut",
	.ts_init = rm_reset,
	.ts_fini = rm_reset,
	.ts_tests = {
		{ "intent mode", intent_mode_test },
		{ "wbc", wbc_mode_test },
		{ "separate", separate_test },
		{ "lease", lease_test },
		{ "optimist", optimist_test },
		{ "resource type", resource_type_test },
		{ "multiple", multiple_test },
		{ "cancel", cancel_test },
		{ "caching", caching_test },
		{ "callback", callback_test },
		{ "hierarchy", hierarchy_test },
		{ "extents multiple", ext_multi_test },
		{ NULL, NULL }
	}
};

/** @} end of use cases and unit tests */

/*
 * UB
 */

enum {
	UB_ITER = 200000,
};

static void ub_init(void)
{
}

static void ub_fini(void)
{
}

struct c2_ub_set c2_rm_ub = {
	.us_name = "rm-ub",
	.us_init = ub_init,
	.us_fini = ub_fini,
	.us_run  = {
		{ .ut_name = NULL }
	}
};

/** @} end of rm group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
