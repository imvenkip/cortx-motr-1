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


#include <stdio.h>         /* printf */

#ifdef ENABLE_FAULT_INJECTION

#include <stdlib.h>        /* random */
#include <unistd.h>        /* getpid */

#include "lib/mutex.h"     /* m0_mutex */
#include "lib/time.h"      /* m0_time_now */
#include "lib/finject.h"
#include "lib/finject_internal.h"


M0_INTERNAL int m0_fi_init(void)
{
	unsigned int random_seed;

	m0_mutex_init(&fi_states_mutex);

	/*
	 * Initialize pseudo random generator, which is used in M0_FI_RANDOM
	 * triggering algorithm
	 */
	random_seed = m0_time_now() ^ getpid();
	srandom(random_seed);

	fi_states_init();

	return 0;
}

M0_INTERNAL void fi_states_fini(void);

M0_INTERNAL void m0_fi_fini(void)
{
	fi_states_fini();
	m0_mutex_fini(&fi_states_mutex);
}

enum {
	FI_RAND_PROB_SCALE   = 100,
};

/**
 * Returns random value in range [0..FI_RAND_PROB_SCALE]
 */
M0_INTERNAL uint32_t fi_random(void)
{
	return (double)random() / RAND_MAX * FI_RAND_PROB_SCALE;
}

M0_INTERNAL void m0_fi_print_info(void)
{
	int i;

	const struct m0_fi_fpoint_state *state;
	struct m0_fi_fpoint_state_info   si;

	printf("%s", m0_fi_states_headline[0]);
	printf("%s", m0_fi_states_headline[1]);

	for (i = 0; i < m0_fi_states_get_free_idx(); ++i) {

		state = &m0_fi_states_get()[i];
		m0_fi_states_get_state_info(state, &si);

		printf(m0_fi_states_print_format,
			si.si_idx, si.si_enb, si.si_total_hit_cnt,
			si.si_total_trigger_cnt, si.si_hit_cnt,
			si.si_trigger_cnt, si.si_type, si.si_data, si.si_module,
			si.si_file, si.si_line_num, si.si_func, si.si_tag);
	}

	return;
}

#else /* ENABLE_FAULT_INJECTION */

M0_INTERNAL int m0_fi_init(void)
{
	return 0;
}

M0_INTERNAL void m0_fi_fini(void)
{
}

M0_INTERNAL void m0_fi_print_info(void)
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
