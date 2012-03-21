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

#include <linux/kernel.h>  /* UINT_MAX */
#include <linux/random.h>  /* random32 */

#include "lib/mutex.h"     /* c2_mutex */
#include "lib/time.h"      /* c2_time_now */
#include "lib/finject.h"


extern struct c2_mutex  fi_states_mutex;
static struct rnd_state rnd_state;

int c2_fi_init(void)
{
	u64 rnd_seed;

	c2_mutex_init(&fi_states_mutex);

	/*
	 * Initialize pseudo random generator, used in C2_FI_RANDOM triggering
	 * algorithm
	 */
	rnd_seed = c2_time_now() ^ current->pid;
	prandom32_seed(&rnd_state, rnd_seed);

	return 0;
}

void fi_states_fini(void);

void c2_fi_fini(void)
{
	fi_states_fini();
	c2_mutex_fini(&fi_states_mutex);
}

enum {
	FI_RAND_PROB_SCALE = 100,
	FI_RAND_SCALE_UNIT = UINT_MAX / FI_RAND_PROB_SCALE,
};

/**
 * Returns random value in range [0..FI_RAND_PROB_SCALE]
 */
uint32_t fi_random(void)
{
	u32 rnd     = prandom32(&rnd_state);
	u32 roundup = rnd % FI_RAND_SCALE_UNIT ? 1 : 0;

	return rnd / FI_RAND_SCALE_UNIT + roundup;
}

#else /* ENABLE_FAULT_INJECTION */

int c2_fi_init(void)
{
	return 0;
}

void c2_fi_fini(void)
{
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
