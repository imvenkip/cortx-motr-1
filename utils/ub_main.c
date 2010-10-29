/* -*- C -*- */

#include <stdlib.h>             /* atoi */

#include "lib/ub.h"
#include "utils/common.h"

extern struct c2_ub_set c2_list_ub;
extern struct c2_ub_set c2_thread_ub;
extern struct c2_ub_set c2_memory_ub;
extern struct c2_ub_set c2_trace_ub;
extern struct c2_ub_set c2_adieu_ub;
extern struct c2_ub_set c2_ad_ub;
extern struct c2_ub_set c2_cob_ub;
extern struct c2_ub_set c2_db_ub;
extern struct c2_ub_set c2_emap_ub;
extern struct c2_ub_set c2_fol_ub;
extern struct c2_ub_set c2_parity_math_ub;

#define UB_SANDBOX "./ub-sandbox"

int main(int argc, char *argv[])
{
	uint32_t rounds;

	if (argc > 1)
		rounds = atoi(argv[1]);
	else
		rounds = ~0;

	if (unit_start(UB_SANDBOX) == 0) {
                /* Note these tests are run in reverse order from the way
                   they are listed here */
		c2_ub_set_add(&c2_memory_ub);
                c2_ub_set_add(&c2_adieu_ub);
                c2_ub_set_add(&c2_ad_ub);
                c2_ub_set_add(&c2_db_ub);
                c2_ub_set_add(&c2_cob_ub);
                c2_ub_set_add(&c2_emap_ub);
                c2_ub_set_add(&c2_fol_ub);
                c2_ub_set_add(&c2_list_ub);
                c2_ub_set_add(&c2_parity_math_ub);
		c2_ub_set_add(&c2_thread_ub);
		c2_ub_set_add(&c2_trace_ub);
		c2_ub_run(rounds);

		unit_end(UB_SANDBOX);
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
