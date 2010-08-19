/* -*- C -*- */

#include <stdio.h> /* setbuf */
#include <stdlib.h> /* atoi */

#include "lib/ub.h"
#include "colibri/init.h"

extern struct c2_ub_set c2_list_ub;
extern struct c2_ub_set c2_thread_ub;
extern struct c2_ub_set c2_memory_ub;
extern struct c2_ub_set c2_adieu_ub;
extern struct c2_ub_set c2_trace_ub;
extern struct c2_ub_set c2_db_ub;
extern struct c2_ub_set c2_emap_ub;

int main(int argc, char *argv[])
{
	uint32_t rounds;

	if (argc > 1)
		rounds = atoi(argv[1]);
	else
		rounds = ~0;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	if (c2_init() == 0) {

		c2_ub_set_add(&c2_memory_ub);
		c2_ub_set_add(&c2_thread_ub);
		c2_ub_set_add(&c2_list_ub);
		c2_ub_set_add(&c2_adieu_ub);
		c2_ub_set_add(&c2_trace_ub);
		c2_ub_set_add(&c2_db_ub);
		c2_ub_set_add(&c2_emap_ub);
		c2_ub_run(rounds);

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
