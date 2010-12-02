/* -*- C -*- */

#include <stdio.h>  /* printf */
#include <stdlib.h> /* atoi */
#include <math.h>   /* sqrt */

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/arith.h"
#include "colibri/init.h"

#include "pool/pool.h"
#include "layout/layout.h"
#include "layout/pdclust.h"

/**
   @addtogroup layout
   @{
*/

void layout_demo(struct c2_pdclust_layout *play, uint32_t P, int R, int I)
{
	uint64_t                   group;
	uint64_t                   frame;
	uint32_t                   unit;
	uint32_t                   obj;
	uint32_t                   N;
	uint32_t                   K;
	uint32_t                   W;
	int                        i;
	struct c2_pdclust_src_addr src;
	struct c2_pdclust_tgt_addr tgt;
	struct c2_pdclust_src_addr src1;
	struct c2_pdclust_src_addr map[R][P];
	uint32_t                   incidence[P][P];
	uint32_t                   usage[P][PUT_NR + 1];
	uint32_t                   where[play->pl_N + 2*play->pl_K];
	const char                *brace[PUT_NR] = { "[]", "<>", "{}" };
	const char                *head[PUT_NR+1] = { "D", "P", "S", "total" };

	uint32_t min;
	uint32_t max;
	uint64_t sum;
	uint32_t u;
	double   sq;
	double   avg;

	C2_SET_ARR0(usage);
	C2_SET_ARR0(incidence);

	N = play->pl_N;
	K = play->pl_K;
	W = N + 2*K;

	printf("layout: N: %u K: %u P: %u C: %u L: %u\n",
	       N, K, P, play->pl_C, play->pl_L);

	for (group = 0; group < I ; ++group) {
		src.sa_group = group;
		for (unit = 0; unit < W; ++unit) {
			src.sa_unit = unit;
			c2_pdclust_layout_map(play, &src, &tgt);
			printf("src.sa_unit=%lu, src.sa_group=%lu"
			       "tgt.ta_frame=%lu, tgt.ta_obj=%lu\n"
			       , src.sa_unit, src.sa_group, tgt.ta_frame, tgt.ta_obj);

			c2_pdclust_layout_inv(play, &tgt, &src1);
			C2_ASSERT(memcmp(&src, &src1, sizeof src) == 0);
			if (tgt.ta_frame < R)
				map[tgt.ta_frame][tgt.ta_obj] = src;
			where[unit] = tgt.ta_obj;
			usage[tgt.ta_obj][PUT_NR]++;
			usage[tgt.ta_obj][c2_pdclust_unit_classify(play, unit)]++;
		}
		printf("---\n");
		for (unit = 0; unit < W; ++unit) {
			for (i = 0; i < W; ++i)
				incidence[where[unit]][where[i]]++;
		}
	}

	printf("hahaha:\n");
	for (group = 0; group < I ; ++group) {
		src.sa_group = group;
		for (unit = 0; unit < W; ++unit) {
			src.sa_unit = unit;
			c2_pdclust_layout_map(play, &src, &tgt);
			printf("src.sa_unit=%lu, src.sa_group=%lu"
			       "tgt.ta_frame=%lu, tgt.ta_obj=%lu\n"
			       , src.sa_unit, src.sa_group, tgt.ta_frame, tgt.ta_obj);			
		}
		printf("---\n");
	}
	

	printf("map: \n");
	for (frame = 0; frame < R; ++frame) {
		printf("%5i : ", (int)frame);
		for (obj = 0; obj < P; ++obj) {
			int d;

			d = c2_pdclust_unit_classify(play, map[frame][obj].sa_unit);
			printf("%c%2i, %1i%c ", 
			       brace[d][0],
			       (int)map[frame][obj].sa_group, 
			       (int)map[frame][obj].sa_unit,
			       brace[d][1]);
		}
		printf("\n");
	}
	printf("usage : \n");
	for (i = 0; i < PUT_NR + 1; ++i) {
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
}

int main(int argc, char **argv)
{
	uint32_t N;
	uint32_t K;
	uint32_t P;
	int      R;
	int      I;
	int      result;
	struct c2_pdclust_layout  *play;
	struct c2_pool             pool;
	struct c2_uint128          id;
	struct c2_uint128          seed;

	if (argc != 6) {
		printf(
"\t\tldemo N K P R I\nwhere\n"
"\tN: number of data units in a parity group\n"
"\tK: number of parity units in a parity group\n"
"\tP: number of target objects to stripe over\n"
"\tR: number of frames to show in a layout map\n"
"\tI: number of groups to iterate over while\n"
"\t   calculating incidence and frame distributions\n"
"\noutput:\n"
"\tmap:       an R*P map showing initial fragment of layout\n"
"\t                   [G, U] - data unit U from a group G\n"
"\t                   <G, U> - parity unit U from a group G\n"
"\t                   {G, U} - spare unit U from a group G\n"
"\tusage:     counts of data, parity, spare and total frames\n"
"\t           occupied on each target object, followed by MIN,\n"
"\t           MAX, AVG, STD/AVG\n"
"\tincidence: a matrix showing a number of parity groups having\n"
"\t           units on a given pair of target objects, followed by\n"
"\t           MIN, MAX, AVG, STD/AVG\n");
		return 1;
	}
	N = atoi(argv[1]);
	K = atoi(argv[2]);
	P = atoi(argv[3]);
	R = atoi(argv[4]);
	I = atoi(argv[5]);

	c2_uint128_init(&id,   "jinniesisjillous");
	c2_uint128_init(&seed, "upjumpandpumpim,");

	result = c2_init();
	if (result == 0) {
		result = c2_pool_init(&pool, P);
		if (result == 0) {
			result = c2_pdclust_build(&pool, &id, N, K, &seed, 
						  &play);
			if (result == 0)
				layout_demo(play, P, R, I);
			c2_pool_fini(&pool);
		}
		c2_fini();
	}
	return result;
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
