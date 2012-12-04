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

 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact

 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 07/20/2010
 */

#pragma once

#ifndef __MERO_LIB_UB_H__
#define __MERO_LIB_UB_H__

#include "lib/types.h"
#include "lib/cdefs.h"

/**
   @defgroup ub Unit Benchmarking.

   @{
 */

struct m0_ub_bench {
	const char *ut_name;
	uint32_t    ut_iter;
	void      (*ut_round)(int iter);
	void      (*ut_init)(void);
	void      (*ut_fini)(void);
	double      ut_total;
	double      ut_square;
	double      ut_min;
	double      ut_max;
};

struct m0_ub_set {
	const char        *us_name;
	void             (*us_init)(void);
	void             (*us_fini)(void);
	struct m0_ub_set  *us_prev;
	struct m0_ub_bench us_run[];
};

M0_INTERNAL void m0_ub_set_add(struct m0_ub_set *set);
M0_INTERNAL void m0_ub_run(uint32_t rounds);

/** @} end of ub group. */

/* __MERO_LIB_UB_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
