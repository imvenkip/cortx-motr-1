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

#ifdef ENABLE_FAULT_INJECTION

#include <linux/kernel.h>  /* UINT_MAX */
#include <linux/random.h>  /* random32 */
#include <linux/sched.h>   /* current */

#include "lib/mutex.h"     /* c2_mutex */
#include "lib/time.h"      /* c2_time_now */
#include "lib/finject.h"
#include "lib/finject_internal.h"


enum {
	FI_RAND_PROB_SCALE = 100,
	FI_RAND_SCALE_UNIT = UINT_MAX / FI_RAND_PROB_SCALE,
};


C2_INTERNAL int c2_fi_init(void)
{
	c2_mutex_init(&fi_states_mutex);
	fi_states_init();
	return 0;
}

C2_INTERNAL void c2_fi_fini(void)
{
	fi_states_fini();
	c2_mutex_fini(&fi_states_mutex);
}

/**
 * Returns random value in range [0..FI_RAND_PROB_SCALE]
 */
C2_INTERNAL uint32_t fi_random(void)
{
	u32 rnd     = random32();
	u32 roundup = rnd % FI_RAND_SCALE_UNIT ? 1 : 0;

	return rnd / FI_RAND_SCALE_UNIT + roundup;
}

#else /* ENABLE_FAULT_INJECTION */

C2_INTERNAL int c2_fi_init(void)
{
	return 0;
}

C2_INTERNAL void c2_fi_fini(void)
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
