/* -*- C -*- */

#include <stdio.h>
#include <CUnit/CUnit.h>

#include "lib/ut.h"
#include "lib/ub.h"
#include "colibri/init.h"

extern const struct c2_test_suite libc2_ut;
extern const struct c2_test_suite adieu_ut;
extern const struct c2_test_suite fop_ut;

extern struct c2_ub_set c2_list_ub;
extern struct c2_ub_set c2_thread_ub;
extern struct c2_ub_set c2_memory_ub;

int main(int argc, char *argv[])
{
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	c2_init();

	c2_ut_add(&libc2_ut);
	c2_ut_add(&adieu_ut);
	c2_ut_add(&fop_ut);
	//c2_ut_run("c2ut.log");

	c2_ub_set_add(&c2_memory_ub);
	c2_ub_set_add(&c2_thread_ub);
	c2_ub_set_add(&c2_list_ub);
	c2_ub_run(~0);

	c2_fini();

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
