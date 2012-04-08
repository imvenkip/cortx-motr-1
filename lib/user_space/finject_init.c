/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 02/22/2012
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef ENABLE_FAULT_INJECTION

#include <stdlib.h>        /* random */
#include <stdio.h>         /* printf */
#include <unistd.h>        /* getpid */

#include "lib/mutex.h"     /* c2_mutex */
#include "lib/time.h"      /* c2_time_now */
#include "lib/finject.h"
#include "lib/finject_internal.h"


int c2_fi_init(void)
{
	unsigned int random_seed;

	c2_mutex_init(&fi_states_mutex);

	/*
	 * Initialize pseudo random generator, which is used in C2_FI_RANDOM
	 * triggering algorithm
	 */
	random_seed = c2_time_now() ^ getpid();
	srandom(random_seed);

	fi_states_init();

	return 0;
}

void fi_states_fini(void);

void c2_fi_fini(void)
{
	fi_states_fini();
	c2_mutex_fini(&fi_states_mutex);
}

enum {
	FI_RAND_PROB_SCALE   = 100,
};

/**
 * Returns random value in range [0..FI_RAND_PROB_SCALE]
 */
uint32_t fi_random(void)
{
	return (double)random() / RAND_MAX * FI_RAND_PROB_SCALE;
}

void c2_fi_print_info(void)
{
	int i;

	const struct c2_fi_fpoint_state *state;
	struct c2_fi_fpoint_state_info   si;

	printf("%s", c2_fi_states_headline[0]);
	printf("%s", c2_fi_states_headline[1]);

	for (i = 0; i < c2_fi_states_get_free_idx(); ++i) {

		state = &c2_fi_states_get()[i];
		c2_fi_states_get_state_info(state, &si);

		printf(c2_fi_states_print_format,
			si.si_idx, si.si_enb, si.si_total_hit_cnt,
			si.si_total_trigger_cnt, si.si_hit_cnt,
			si.si_trigger_cnt, si.si_type, si.si_data, si.si_module,
			si.si_file, si.si_line_num, si.si_func, si.si_tag);
	}

	return;
}

#else /* ENABLE_FAULT_INJECTION */

int c2_fi_init(void)
{
	return 0;
}

void c2_fi_fini(void)
{
}

void c2_fi_print_info(void)
{
	fprintf(stderr, "Fault injection is not available, because it was"
			" disabled during build\n");
}

#endif /* ENABLE_FAULT_INJECTION */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
