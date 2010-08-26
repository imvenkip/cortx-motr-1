#include <CUnit/Basic.h>
#include <CUnit/Automated.h>

#include "lib/assert.h"
#include "lib/ut.h"

/**
   @addtogroup ut
   @{
 */

int c2_uts_init(void)
{
	C2_CASSERT(CUE_SUCCESS == 0);
	return -CU_initialize_registry();
}

void c2_uts_fini(void)
{
	CU_cleanup_registry();
	C2_ASSERT(CU_get_error() == 0);
}

void c2_ut_add(const struct c2_test_suite *ts)
{
	CU_pSuite pSuite;
	int i;

	/* add a suite to the registry */
	pSuite = CU_add_suite(ts->ts_name, ts->ts_init, ts->ts_fini);
	C2_ASSERT(pSuite != NULL);

	for (i = 0; ts->ts_tests[i].t_name != NULL; i++) {
		if (CU_add_test(pSuite, ts->ts_tests[i].t_name, 
				ts->ts_tests[i].t_proc) == NULL)
			break;
	}

	C2_ASSERT(ts->ts_tests[i].t_name == NULL);
}

void c2_ut_run(const char *log_file)
{
	CU_set_output_filename(log_file);

	/* run in make console output */
	CU_basic_set_mode(CU_BRM_VERBOSE);
	CU_basic_run_tests();
	CU_basic_show_failures(CU_get_failure_list());

#if 0	
	/* run and save results to xml */
	CU_automated_run_tests();
#endif
}

/** @} end of ut group. */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
