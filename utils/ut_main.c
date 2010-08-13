/* -*- C -*- */

#include <stdio.h> /* setbuf */
#include <CUnit/CUnit.h>

#include "lib/ut.h"
#include "lib/trace.h"
#include "colibri/init.h"

extern const struct c2_test_suite libc2_ut;
extern const struct c2_test_suite adieu_ut;
extern const struct c2_test_suite fop_ut;
extern const struct c2_test_suite db_ut;

int main(int argc, char *argv[])
{
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	if (c2_init() == 0) {
		if (argc == 2 && !strcmp(argv[1], "trace")) {
			c2_trace_parse();
		} else {
			c2_ut_add(&libc2_ut);
			c2_ut_add(&adieu_ut);
			c2_ut_add(&fop_ut);
			c2_ut_add(&db_ut);
			c2_ut_run("c2ut.log");
		}
		c2_fini();
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
