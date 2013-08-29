/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 12-Jun-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_UT
#include "lib/trace.h"
#include "lib/types.h"		/* m0_uint128_eq */
#include "lib/misc.h"		/* M0_BITS */
#include "be/ut/helper.h"	/* m0_be_ut_backend */
#include "ut/ut.h"

void m0_be_ut_dict(void)
{
	struct m0_be_ut_backend ut_be;
	struct m0_be_ut_seg     ut_seg;
	struct m0_sm_group     *grp;
	struct m0_be_seg       *seg = &ut_seg.bus_seg;
	void                   *p;
	int                     i;
	int                     rc;
	struct {
		const char *name;
		void       **value;
	} dict[] = {
		{ "dead", (void*)0xdead },
		{ "beaf", (void*)0xbeaf },
		{ "cafe", (void*)0xcafe },
		{ "babe", (void*)0xbabe },
		{ "d00d", (void*)0xd00d },
		{ "8bad", (void*)0x8bad },
		{ "f00d", (void*)0xf00d },
	};

	m0_be_ut_backend_init(&ut_be);
	m0_be_ut_seg_init(&ut_seg, &ut_be, 1 << 20);
	m0_be_ut_seg_allocator_init(&ut_seg, &ut_be);

	grp = m0_be_ut_backend_sm_group_lookup(&ut_be);
	rc = m0_be_seg_dict_create(seg, grp);
	M0_UT_ASSERT(rc == 0);

	for (i = 0; i < ARRAY_SIZE(dict); ++i) {
		rc = m0_be_seg_dict_insert(seg, grp, dict[i].name,
					   dict[i].value);
		M0_UT_ASSERT(rc == 0);
	}

	for (i = 0; i < ARRAY_SIZE(dict); ++i) {
		rc = m0_be_seg_dict_lookup(seg, dict[i].name, &p);
		M0_UT_ASSERT(rc == 0 && dict[i].value == p);
	}

	for (i = 0; i < ARRAY_SIZE(dict); i+=2) {
		rc = m0_be_seg_dict_delete(seg, grp, dict[i].name);
		M0_UT_ASSERT(rc == 0);
	}

	for (i = 1; i < ARRAY_SIZE(dict); i+=2) {
		rc = m0_be_seg_dict_lookup(seg, dict[i].name, &p);
		M0_UT_ASSERT(rc == 0 && dict[i].value == p);
	}

	/* reload segment, check dictionary is persistent */
	m0_be_seg_close(seg);
	rc = m0_be_seg_open(seg);
	M0_UT_ASSERT(rc == 0);

	m0_be_seg_dict_init(seg);

	for (i = 1; i < ARRAY_SIZE(dict); i+=2) {
		rc = m0_be_seg_dict_lookup(seg, dict[i].name, &p);
		M0_UT_ASSERT(rc == 0 && dict[i].value == p);
	}

	m0_be_ut_seg_fini(&ut_seg);
	m0_be_ut_backend_fini(&ut_be);
}

#undef M0_TRACE_SUBSYSTEM
