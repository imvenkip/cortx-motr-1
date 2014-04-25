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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 09/27/2011
 */

#include "ut/ut.h"
#include "ut/cs_service.h"
#include "fop/fom_generic.h"
#include "lib/misc.h"           /* M0_IN() */

enum { SUITES_MAX = 4096 };
static const struct m0_test_suite *suites[SUITES_MAX + 1];
static unsigned                    used;

M0_INTERNAL void m0_ut_add(const struct m0_test_suite *ts)
{
	M0_ASSERT(used < ARRAY_SIZE(suites));
	suites[used++] = ts;
}

M0_INTERNAL void m0_ut_submit_all(void)
{
	unsigned i;

	for (i = 0; i < used; ++i)
		m0_ut_submit(suites[i]);
}

#ifndef __KERNEL__

#include <stdlib.h>                       /* qsort */

static int order[SUITES_MAX + 1];

static int cmp(const struct m0_test_suite **s0, const struct m0_test_suite **s1)
{
	int i0 = s0 - suites;
	int i1 = s1 - suites;

	return order[i0] - order[i1];
}

M0_INTERNAL void m0_ut_shuffle(unsigned seed)
{
	unsigned i;

	M0_ASSERT(used > 0);

	srand(seed);
	for (i = 1; i < used; ++i)
		order[i] = rand();
	qsort(suites + 1, used - 1, sizeof suites[0], (void *)&cmp);
}
#endif

void m0_ut_fom_phase_set(struct m0_fom *fom, int phase)
{
	switch (m0_fom_phase(fom)) {
	case M0_FOPH_SUCCESS:
		m0_fom_phase_set(fom, M0_FOPH_FOL_REC_ADD);
		/* fall through */
	case M0_FOPH_FAILURE:
		m0_fom_phase_set(fom, M0_FOPH_TXN_COMMIT);
		m0_fom_phase_set(fom, M0_FOPH_QUEUE_REPLY);
		/* fall through */
	default:
		m0_fom_phase_set(fom, phase);
	}
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
