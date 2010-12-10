/* -*- C -*- */

#include <stdlib.h>        /* exit */
#include <CUnit/CUnit.h>

#include "lib/ut.h"
#include "lib/trace.h"
#include "lib/thread.h"    /* LAMBDA */
#include "lib/getopts.h"
#include "utils/common.h"

extern const struct c2_test_suite libc2_ut;
extern const struct c2_test_suite adieu_ut;
extern const struct c2_test_suite ad_ut;
extern const struct c2_test_suite cob_ut;
extern const struct c2_test_suite db_ut;
extern const struct c2_test_suite emap_ut;
extern const struct c2_test_suite fol_ut;
extern const struct c2_test_suite fop_ut;
extern const struct c2_test_suite parity_math_ut;

#define UT_SANDBOX "./ut-sandbox"

int main(int argc, char *argv[])
{
	int  result;
	bool keep   = false;

	result = C2_GETOPTS("ut", argc, argv,
			    C2_VOIDARG('T', "parse trace log produced earlier",
				       LAMBDA(void, (void) {
						       c2_trace_parse();
						       exit(0);
					       })),
			    C2_FLAGARG('k', "keep the sandbox directory", 
				       &keep));
	if (result != 0)
		return result;

	if (unit_start(UT_SANDBOX) == 0) {
		c2_ut_add(&libc2_ut);
		c2_ut_add(&adieu_ut);
		c2_ut_add(&ad_ut);
		c2_ut_add(&cob_ut);
		c2_ut_add(&db_ut);
		c2_ut_add(&emap_ut);
		c2_ut_add(&fol_ut);
		c2_ut_add(&fop_ut);
		c2_ut_add(&parity_math_ut);
		c2_ut_run("c2ut.log");
		if (!keep)
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
