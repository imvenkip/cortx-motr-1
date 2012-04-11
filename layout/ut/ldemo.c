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
#include "layout/layout_db.h"
#include "layout/pdclust.h"
#include "layout/linear_enum.h" /* c2_linear_enum_build() */

/**
   @addtogroup layout
   @{
*/

enum {
	DEF_POOL_ID = 10
};

enum c2_pdclust_unit_type classify(const struct c2_pdclust_layout *play,
				   int unit)
{
	if (unit < play->pl_attr.pa_N)
		return PUT_DATA;
	else if (unit < play->pl_attr.pa_N + play->pl_attr.pa_K)
		return PUT_PARITY;
	else
		return PUT_SPARE;
}

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
	uint32_t                   where[play->pl_attr.pa_N + 2*play->pl_attr.pa_K];
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

	N = play->pl_attr.pa_N;
	K = play->pl_attr.pa_K;
	W = N + 2*K;

	printf("layout: N: %u K: %u P: %u C: %u L: %u\n",
	       N, K, P, play->pl_C, play->pl_L);

	for (group = 0; group < I ; ++group) {
		src.sa_group = group;
		for (unit = 0; unit < W; ++unit) {
			src.sa_unit = unit;
			c2_pdclust_layout_map(play, &src, &tgt);
			c2_pdclust_layout_inv(play, &tgt, &src1);
			C2_ASSERT(memcmp(&src, &src1, sizeof src) == 0);
			if (tgt.ta_frame < R)
				map[tgt.ta_frame][tgt.ta_obj] = src;
			where[unit] = tgt.ta_obj;
			usage[tgt.ta_obj][PUT_NR]++;
			usage[tgt.ta_obj][classify(play, unit)]++;
		}
		for (unit = 0; unit < W; ++unit) {
			for (i = 0; i < W; ++i)
				incidence[where[unit]][where[i]]++;
		}
	}
	printf("map: \n");
	for (frame = 0; frame < R; ++frame) {
		printf("%5i : ", (int)frame);
		for (obj = 0; obj < P; ++obj) {
			int d;

			d = classify(play, map[frame][obj].sa_unit);
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

/*
 * Creates dummy schema, domain, registers pdclust layout type and linear
 * enum type and creates dummy enum object.
 * These objects are called as dummy since they are not used by this ldemo
 * test.
 */
static int dummy_create(struct c2_layout_domain *domain,
			struct c2_dbenv *dbenv,
			struct c2_ldb_schema *schema,
			uint64_t lid, uint32_t pool_width,
			struct c2_layout_linear_enum **lin_enum)
{
	int rc;

	rc = c2_layout_domain_init(domain);
	C2_ASSERT(rc == 0);

	rc = c2_dbenv_init(dbenv, "ldemo-db", 0);
	C2_ASSERT(rc == 0);

	rc = c2_ldb_schema_init(schema, domain, dbenv);
	C2_ASSERT(rc == 0);
	C2_ASSERT(schema->ls_domain == domain);

	c2_ldb_type_register(domain, &c2_pdclust_layout_type);
	c2_ldb_enum_register(domain, &c2_linear_enum_type);

	rc = c2_linear_enum_build(domain, lid, pool_width, 100, 200, lin_enum);
	C2_ASSERT(rc == 0);


	return rc;
}

int main(int argc, char **argv)
{
	uint32_t N;
	uint32_t K;
	uint32_t P;
	int      R;
	int      I;
	int      result;
	uint64_t unitsize = 4096;
	struct c2_pdclust_layout      *play = NULL;
	struct c2_pool                 pool;
	uint64_t                       id;
	struct c2_uint128              seed;
	struct c2_layout_domain        domain;
	struct c2_dbenv                dbenv;
	struct c2_ldb_schema           schema;
	struct c2_layout_linear_enum  *le = NULL;
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

	id = 0x4A494E4E49455349; /* "jinniesi" */
	c2_uint128_init(&seed, "upjumpandpumpim,");

	result = c2_init();
	if (result != 0)
		return result;

	result = c2_pool_init(&pool, DEF_POOL_ID, P);
	if (result == 0) {
		/**
		 * Creating a dummy domain object here so as to supply it
		 * to c2_pdclust_build(), though it is not used in this test.
		 */
		result = dummy_create(&domain, &dbenv, &schema,
				      id, pool.po_width, &le);
		if (result == 0) {
			result = c2_pdclust_build(&domain, &pool, id, N, K,
						  unitsize, &seed,
						  &le->lle_base, &play);
			if (result == 0)
				layout_demo(play, P, R, I);
			c2_pool_fini(&pool);
		}

	}

	c2_fini();

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
