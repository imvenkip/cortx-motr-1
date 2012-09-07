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

#include "layout/ut/ldemo_internal.c"

/**
   @addtogroup layout
   @{
*/

/*
 * Creates dummy domain, registers pdclust layout type and linear
 * enum type and creates dummy enum object.
 * These objects are called as dummy since they are not used by this ldemo
 * test.
 */
static int dummy_create(struct c2_layout_domain *domain,
			struct c2_dbenv *dbenv,
			uint64_t lid,
			struct c2_pdclust_attr *attr,
			struct c2_pdclust_layout **pl)
{
	int                           rc;
	struct c2_layout_linear_attr  lin_attr;
	struct c2_layout_linear_enum *lin_enum;

	rc = c2_dbenv_init(dbenv, "ldemo-db", 0);
	C2_ASSERT(rc == 0);

	rc = c2_layout_domain_init(domain, dbenv);
	C2_ASSERT(rc == 0);

	rc = c2_layout_standard_types_register(domain);
	C2_ASSERT(rc == 0);

	lin_attr.lla_nr = attr->pa_P;
	lin_attr.lla_A  = 100;
	lin_attr.lla_B  = 200;
	rc = c2_linear_enum_build(domain, &lin_attr, &lin_enum);
	C2_ASSERT(rc == 0);

	rc = c2_pdclust_build(domain, lid, attr, &lin_enum->lle_base, pl);
	C2_ASSERT(rc == 0);
	return rc;
}

int main(int argc, char **argv)
{
	uint32_t                    N;
	uint32_t                    K;
	uint32_t                    P;
	int                         R;
	int                         I;
	int                         rc;
	uint64_t                    unitsize = 4096;
	struct c2_pdclust_layout   *play;
	struct c2_pdclust_attr      attr;
	struct c2_pool              pool;
	uint64_t                    id;
	struct c2_uint128           seed;
	struct c2_layout_domain     domain;
	struct c2_dbenv             dbenv;
	struct c2_pdclust_instance *pi;
	struct c2_fid               gfid;
	struct c2_layout_instance  *li;
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

	rc = c2_init();
	if (rc != 0)
		return rc;

	rc = c2_pool_init(&pool, P);
	if (rc == 0) {
		attr.pa_N = N;
		attr.pa_K = K;
		attr.pa_P = pool.po_width;
		attr.pa_unit_size = unitsize;
		attr.pa_seed = seed;

		rc = dummy_create(&domain, &dbenv, id, &attr, &play);
		if (rc == 0) {
			c2_fid_set(&gfid, 0, 999);
			rc = c2_layout_instance_build(c2_pdl_to_layout(play),
						      &gfid, &li);
			pi = c2_layout_instance_to_pdi(li);
			if (rc == 0) {
				layout_demo(pi, P, R, I, true);
				pi->pi_base.li_ops->lio_fini(&pi->pi_base);
			}
			c2_layout_put(c2_pdl_to_layout(play));
			c2_layout_standard_types_unregister(&domain);
			c2_layout_domain_fini(&domain);
			c2_dbenv_fini(&dbenv);
		}
		c2_pool_fini(&pool);
	}

	c2_fini();
	return rc;
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
