/* -*- C -*- */

#include <CUnit/CUnit.h>

#include "lib/ut.h"
#include "lib/trace.h"
#include "utils/common.h"

extern const struct c2_test_suite libc2_ut;
extern const struct c2_test_suite adieu_ut;
extern const struct c2_test_suite ad_ut;
extern const struct c2_test_suite fop_ut;
extern const struct c2_test_suite db_ut;
extern const struct c2_test_suite emap_ut;

#define UT_SANDBOX "./ut-sandbox"

int main(int argc, char *argv[])
{
	if (unit_start(UT_SANDBOX) == 0) {
		if (argc == 2 && !strcmp(argv[1], "trace")) {
			c2_trace_parse();
		} else {
			c2_ut_add(&libc2_ut);
			c2_ut_add(&adieu_ut);
			c2_ut_add(&ad_ut);
			c2_ut_add(&fop_ut);
			c2_ut_add(&db_ut);
			c2_ut_add(&emap_ut);
			c2_ut_run("c2ut.log");
		}
		unit_end(UT_SANDBOX);
	}

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
