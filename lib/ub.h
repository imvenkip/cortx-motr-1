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

#ifndef __COLIBRI_LIB_UB_H__
#define __COLIBRI_LIB_UB_H__

#include "lib/types.h"
#include "lib/cdefs.h"

/**
   @defgroup ub Unit Benchmarking.

   @{
 */

struct c2_ub_bench {
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

struct c2_ub_set {
	const char        *us_name;
	void             (*us_init)(void);
	void             (*us_fini)(void);
	struct c2_ub_set  *us_prev;
	struct c2_ub_bench us_run[];
};

void c2_ub_set_add(struct c2_ub_set *set);
void c2_ub_run(uint32_t rounds);

/** @} end of ub group. */

/* __COLIBRI_LIB_UB_H__ */
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
