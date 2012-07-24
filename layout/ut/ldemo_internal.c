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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 07/15/2010
 */

#include "lib/misc.h"       /* C2_SET_ARR0 */
#include "layout/pdclust.h"

/**
   @addtogroup layout
   @{
*/

enum c2_pdclust_unit_type classify(const struct c2_pdclust_layout *play,
				   int unit)
{
	if (unit < play->pl_attr.pa_N)
		return C2_PUT_DATA;
	else if (unit < play->pl_attr.pa_N + play->pl_attr.pa_K)
		return C2_PUT_PARITY;
	else
		return C2_PUT_SPARE;
}

/**
 * @todo Allocate the arrays globally so that it does not result into
 * going beyond the stack limit in the kernel mode.
 */
void layout_demo(struct c2_pdclust_instance *pi, uint32_t P, int R, int I,
		 bool print)
{
	uint64_t                   group;
	uint32_t                   unit;
	uint32_t                   N;
	uint32_t                   K;
	uint32_t                   W;
	int                        i;
	struct c2_pdclust_src_addr src;
	struct c2_pdclust_tgt_addr tgt;
	struct c2_pdclust_src_addr src1;
	struct c2_pdclust_src_addr map[R][P];
	uint32_t                   incidence[P][P];
	uint32_t                   usage[P][C2_PUT_NR + 1];
	uint32_t                   where[pi->pi_layout->pl_attr.pa_N +
					 2*pi->pi_layout->pl_attr.pa_K];

#ifndef __KERNEL__
	uint64_t                   frame;
	uint32_t                   obj;
	const char                *brace[C2_PUT_NR] = { "[]", "<>", "{}" };
	const char                *head[C2_PUT_NR+1] = { "D", "P", "S",
							 "total" };
	uint32_t                   min;
	uint32_t                   max;
	uint64_t                   sum;
	uint32_t                   u;
	double                     sq;
	double                     avg;
#endif

	C2_SET_ARR0(usage);
	C2_SET_ARR0(incidence);

	N = pi->pi_layout->pl_attr.pa_N;
	K = pi->pi_layout->pl_attr.pa_K;
	W = N + 2*K;

#ifndef __KERNEL__
	if (print) {
		printf("layout: N: %u K: %u P: %u C: %u L: %u\n",
				N, K, P, pi->pi_layout->pl_C,
				pi->pi_layout->pl_L);
	}
#endif

	for (group = 0; group < I ; ++group) {
		src.sa_group = group;
		for (unit = 0; unit < W; ++unit) {
			src.sa_unit = unit;
			c2_pdclust_instance_map(pi, &src, &tgt);
			c2_pdclust_instance_inv(pi, &tgt, &src1);
			C2_ASSERT(memcmp(&src, &src1, sizeof src) == 0);
			if (tgt.ta_frame < R)
				map[tgt.ta_frame][tgt.ta_obj] = src;
			where[unit] = tgt.ta_obj;
			usage[tgt.ta_obj][C2_PUT_NR]++;
			usage[tgt.ta_obj][classify(pi->pi_layout, unit)]++;
		}
		for (unit = 0; unit < W; ++unit) {
			for (i = 0; i < W; ++i)
				incidence[where[unit]][where[i]]++;
		}
	}
	if (!print)
		return;

#ifndef __KERNEL__
	printf("map: \n");
	for (frame = 0; frame < R; ++frame) {
		printf("%5i : ", (int)frame);
		for (obj = 0; obj < P; ++obj) {
			int d;

			d = classify(pi->pi_layout, map[frame][obj].sa_unit);
			printf("%c%2i, %1i%c ",
			       brace[d][0],
			       (int)map[frame][obj].sa_group,
			       (int)map[frame][obj].sa_unit,
			       brace[d][1]);
		}
		printf("\n");
	}
	printf("usage : \n");
	for (i = 0; i < C2_PUT_NR + 1; ++i) {
		max = sum = sq = 0;
		min = ~0;
		printf("%5s : ", head[i]);
		for (obj = 0; obj < P; ++obj) {
			u = usage[obj][i];
			printf("%7i ", u);
			min = min32u(min, u);
			max = max32u(max, u);
			sum += u;
			sq += u*u;
		}
		avg = ((double)sum)/P;
		printf(" | %7i %7i %7i %7.2f%%\n", min, max, (int)avg,
		       sqrt(sq/P - avg*avg)*100.0/avg);
	}
	printf("\nincidence:\n");
	for (obj = 0; obj < P; ++obj) {
		max = sum = sq = 0;
		min = ~0;
		for (i = 0; i < P; ++i) {
			if (obj != i) {
				u = incidence[obj][i];
				min = min32u(min, u);
				max = max32u(max, u);
				sum += u;
				sq += u*u;
				printf("%5i ", u);
			} else
				printf("    * ");
		}
		avg = ((double)sum)/(P - 1);
		printf(" | %5i %5i %5i %5.2f%%\n", min, max, (int)avg,
		       sqrt(sq/(P - 1) - avg*avg)*100.0/avg);
	}
#endif
}

/** @} end of layout group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
