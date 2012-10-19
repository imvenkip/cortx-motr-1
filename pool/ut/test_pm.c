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
 * Original author: Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 08/15/2012
 */

#include "lib/ut.h"
#include "lib/memory.h"
#include "lib/cdefs.h"
#include "lib/misc.h"
#include "pool/pool.h"

static int verbose = 0;

static void pm_test_init_fini(void)
{
	struct c2_poolmach pm;
	int                rc;

	C2_SET0(&pm);
	rc = c2_poolmach_init(&pm, NULL);
	C2_UT_ASSERT(rc == 0);
	c2_poolmach_fini(&pm);
}

static void dump_version(struct c2_pool_version_numbers *v)
{
	if (verbose)
		printf("readv = %llx writev = %llx\n",
			(unsigned long long)v->pvn_version[PVE_READ],
			(unsigned long long)v->pvn_version[PVE_WRITE]);
}

static void dump_event(struct c2_pool_event *e)
{
	if (verbose)
		printf("pe_type  = %10s pe_index = %2x pe_state=%10s\n",
			e->pe_type == C2_POOL_DEVICE ? "device":"node",
			e->pe_index,
			e->pe_state == C2_PNDS_ONLINE? "ONLINE" :
			    e->pe_state == C2_PNDS_FAILED? "FAILED" :
				e->pe_state == C2_PNDS_OFFLINE? "OFFLINE" :
					"RECOVERING"
		);
}

static void dump_event_list(struct c2_tl *head)
{
	struct c2_pool_event_link *scan;

	c2_tl_for(poolmach_events, head, scan) {
		dump_event(scan->pel_event);
		dump_version(&scan->pel_new_version);
	} c2_tl_endfor;
	if (verbose)
		printf("=====\n");
}

static void pm_test_transit(void)
{
	struct c2_poolmach             pm;
	int                            rc;
	bool                           equal;
	struct c2_pool_event           events[4];
	struct c2_pool_event           e_invalid;
	struct c2_pool_version_numbers v0;
	struct c2_pool_version_numbers v1;
	struct c2_pool_version_numbers v2;
	struct c2_pool_version_numbers v_invalid;
	struct c2_tl                   events_list;
	struct c2_pool_event_link     *scan;
	uint32_t                       count;
	uint32_t                       index;

	C2_SET0(&pm);
	rc = c2_poolmach_init(&pm, NULL);
	C2_UT_ASSERT(rc == 0);

	rc = c2_poolmach_current_version_get(&pm, &v0);
	C2_UT_ASSERT(rc == 0);
	rc = c2_poolmach_current_version_get(&pm, &v1);
	C2_UT_ASSERT(rc == 0);
	rc = c2_poolmach_current_version_get(&pm, &v2);
	C2_UT_ASSERT(rc == 0);

	equal = c2_poolmach_version_equal(&v0, &v1);
	C2_UT_ASSERT(equal);
	equal = c2_poolmach_version_equal(&v0, &v2);
	C2_UT_ASSERT(equal);

	events[0].pe_type  = C2_POOL_DEVICE;
	events[0].pe_index = 1;
	events[0].pe_state = C2_PNDS_FAILED;
	rc = c2_poolmach_state_transit(&pm, &events[0]);
	C2_UT_ASSERT(rc == 0);

	events[1].pe_type  = C2_POOL_DEVICE;
	events[1].pe_index = 3;
	events[1].pe_state = C2_PNDS_OFFLINE;
	rc = c2_poolmach_state_transit(&pm, &events[1]);
	C2_UT_ASSERT(rc == 0);

	rc = c2_poolmach_current_version_get(&pm, &v1);
	C2_UT_ASSERT(rc == 0);

	events[2].pe_type  = C2_POOL_DEVICE;
	events[2].pe_index = 3;
	events[2].pe_state = C2_PNDS_ONLINE;
	rc = c2_poolmach_state_transit(&pm, &events[2]);
	C2_UT_ASSERT(rc == 0);

	events[3].pe_type  = C2_POOL_NODE;
	events[3].pe_index = 0;
	events[3].pe_state = C2_PNDS_OFFLINE;
	rc = c2_poolmach_state_transit(&pm, &events[3]);
	C2_UT_ASSERT(rc == 0);

	rc = c2_poolmach_current_version_get(&pm, &v2);
	C2_UT_ASSERT(rc == 0);

	equal = c2_poolmach_version_equal(&v0, &v1);
	C2_UT_ASSERT(!equal);
	equal = c2_poolmach_version_equal(&v0, &v2);
	C2_UT_ASSERT(!equal);
	equal = c2_poolmach_version_equal(&v1, &v2);
	C2_UT_ASSERT(!equal);
	dump_event_list(&pm.pm_state.pst_events_list);
	dump_version(&v0);
	dump_version(&v1);
	dump_version(&v2);

	/* case 1: from v0 to v1 */
	poolmach_events_tlist_init(&events_list);
	rc = c2_poolmach_state_query(&pm,
				     &v0,
				     &v1,
				     &events_list);
	C2_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	C2_UT_ASSERT(count == 2);
	index = 0;
	c2_tl_for(poolmach_events, &events_list, scan) {
		struct c2_pool_event *e = scan->pel_event;
		C2_UT_ASSERT(events[index].pe_state == e->pe_state);
		C2_UT_ASSERT(events[index].pe_type  == e->pe_type);
		C2_UT_ASSERT(events[index].pe_index == e->pe_index);

		index++;
		poolmach_events_tlink_del_fini(scan);
		c2_free(scan->pel_event);
		c2_free(scan);
	} c2_tl_endfor;

	/* case 2: from v1 to v2 */
	poolmach_events_tlist_init(&events_list);
	rc = c2_poolmach_state_query(&pm,
				     &v1,
				     &v2,
				     &events_list);
	C2_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	C2_UT_ASSERT(count == 2);
	index = 2;
	c2_tl_for(poolmach_events, &events_list, scan) {
		struct c2_pool_event *e = scan->pel_event;
		C2_UT_ASSERT(events[index].pe_state == e->pe_state);
		C2_UT_ASSERT(events[index].pe_type  == e->pe_type);
		C2_UT_ASSERT(events[index].pe_index == e->pe_index);
		index++;
		poolmach_events_tlink_del_fini(scan);
		c2_free(scan->pel_event);
		c2_free(scan);
	} c2_tl_endfor;

	/* case 3: from v0 to v2 */
	poolmach_events_tlist_init(&events_list);
	rc = c2_poolmach_state_query(&pm,
				     &v0,
				     &v2,
				     &events_list);
	C2_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	C2_UT_ASSERT(count == 4);
	index = 0;
	c2_tl_for(poolmach_events, &events_list, scan) {
		struct c2_pool_event *e = scan->pel_event;
		C2_UT_ASSERT(events[index].pe_state == e->pe_state);
		C2_UT_ASSERT(events[index].pe_type  == e->pe_type);
		C2_UT_ASSERT(events[index].pe_index == e->pe_index);
		index++;
		poolmach_events_tlink_del_fini(scan);
		c2_free(scan->pel_event);
		c2_free(scan);
	} c2_tl_endfor;

	/* case 4: from NULL to v1 */
	poolmach_events_tlist_init(&events_list);
	rc = c2_poolmach_state_query(&pm,
				     NULL,
				     &v1,
				     &events_list);
	C2_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	C2_UT_ASSERT(count == 2);
	index = 0;
	c2_tl_for(poolmach_events, &events_list, scan) {
		struct c2_pool_event *e = scan->pel_event;
		C2_UT_ASSERT(events[index].pe_state == e->pe_state);
		C2_UT_ASSERT(events[index].pe_type  == e->pe_type);
		C2_UT_ASSERT(events[index].pe_index == e->pe_index);
		index++;
		poolmach_events_tlink_del_fini(scan);
		c2_free(scan->pel_event);
		c2_free(scan);
	} c2_tl_endfor;

	/* case 5: from v1 to NULL */
	poolmach_events_tlist_init(&events_list);
	rc = c2_poolmach_state_query(&pm,
				     &v1,
				     NULL,
				     &events_list);
	C2_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	C2_UT_ASSERT(count == 2);
	index = 2;
	c2_tl_for(poolmach_events, &events_list, scan) {
		struct c2_pool_event *e = scan->pel_event;
		C2_UT_ASSERT(events[index].pe_state == e->pe_state);
		C2_UT_ASSERT(events[index].pe_type  == e->pe_type);
		C2_UT_ASSERT(events[index].pe_index == e->pe_index);
		index++;
		poolmach_events_tlink_del_fini(scan);
		c2_free(scan->pel_event);
		c2_free(scan);
	} c2_tl_endfor;

	/* case 6: from NULL to NULL */
	poolmach_events_tlist_init(&events_list);
	rc = c2_poolmach_state_query(&pm,
				     NULL,
				     NULL,
				     &events_list);
	C2_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	C2_UT_ASSERT(count == 4);
	index = 0;
	c2_tl_for(poolmach_events, &events_list, scan) {
		struct c2_pool_event *e = scan->pel_event;
		C2_UT_ASSERT(events[index].pe_state == e->pe_state);
		C2_UT_ASSERT(events[index].pe_type  == e->pe_type);
		C2_UT_ASSERT(events[index].pe_index == e->pe_index);
		index++;
		poolmach_events_tlink_del_fini(scan);
		c2_free(scan->pel_event);
		c2_free(scan);
	} c2_tl_endfor;

	/* this is an invalid version */
	v_invalid.pvn_version[PVE_READ]  = -1;
	v_invalid.pvn_version[PVE_WRITE] = -2;

	/* case 7: from v0 to invalid */
	poolmach_events_tlist_init(&events_list);
	rc = c2_poolmach_state_query(&pm,
				     &v0,
				     &v_invalid,
				     &events_list);
	C2_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	C2_UT_ASSERT(count == 4);
	index = 0;
	c2_tl_for(poolmach_events, &events_list, scan) {
		struct c2_pool_event *e = scan->pel_event;
		C2_UT_ASSERT(events[index].pe_state == e->pe_state);
		C2_UT_ASSERT(events[index].pe_type  == e->pe_type);
		C2_UT_ASSERT(events[index].pe_index == e->pe_index);
		index++;
		poolmach_events_tlink_del_fini(scan);
		c2_free(scan->pel_event);
		c2_free(scan);
	} c2_tl_endfor;

	/* case 8: from NULL to invalid */
	poolmach_events_tlist_init(&events_list);
	rc = c2_poolmach_state_query(&pm,
				     NULL,
				     &v_invalid,
				     &events_list);
	C2_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	C2_UT_ASSERT(count == 4);
	index = 0;
	c2_tl_for(poolmach_events, &events_list, scan) {
		struct c2_pool_event *e = scan->pel_event;
		C2_UT_ASSERT(events[index].pe_state == e->pe_state);
		C2_UT_ASSERT(events[index].pe_type  == e->pe_type);
		C2_UT_ASSERT(events[index].pe_index == e->pe_index);
		index++;
		poolmach_events_tlink_del_fini(scan);
		c2_free(scan->pel_event);
		c2_free(scan);
	} c2_tl_endfor;

	/* case 9: from invalid to v2 */
	poolmach_events_tlist_init(&events_list);
	rc = c2_poolmach_state_query(&pm,
				     &v_invalid,
				     &v2,
				     &events_list);
	C2_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	C2_UT_ASSERT(count == 0);

	/* case 10: from invalid to NULL */
	poolmach_events_tlist_init(&events_list);
	rc = c2_poolmach_state_query(&pm,
				     &v_invalid,
				     NULL,
				     &events_list);
	C2_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	C2_UT_ASSERT(count == 0);

	/* case 11: from invalid to invalid */
	poolmach_events_tlist_init(&events_list);
	rc = c2_poolmach_state_query(&pm,
				     &v_invalid,
				     &v_invalid,
				     &events_list);
	C2_UT_ASSERT(rc == 0);
	count = poolmach_events_tlist_length(&events_list);
	C2_UT_ASSERT(count == 0);

	/* invalid event. case 1: invalid type*/
	e_invalid.pe_type  = C2_POOL_NODE + 5;
	e_invalid.pe_index = 0;
	e_invalid.pe_state = C2_PNDS_OFFLINE;
	rc = c2_poolmach_state_transit(&pm, &e_invalid);
	C2_UT_ASSERT(rc == -EINVAL);

	/* invalid event. case 2: invalid index */
	e_invalid.pe_type  = C2_POOL_NODE;
	e_invalid.pe_index = 100;
	e_invalid.pe_state = C2_PNDS_OFFLINE;
	rc = c2_poolmach_state_transit(&pm, &e_invalid);
	C2_UT_ASSERT(rc == -EINVAL);

	/* invalid event. case 3: invalid state */
	e_invalid.pe_type  = C2_POOL_NODE;
	e_invalid.pe_index = 0;
	e_invalid.pe_state = C2_PNDS_RECOVERING + 1;
	rc = c2_poolmach_state_transit(&pm, &e_invalid);
	C2_UT_ASSERT(rc == -EINVAL);

	/* invalid event. case 4: invalid state */
	e_invalid.pe_type  = C2_POOL_DEVICE;
	e_invalid.pe_index = 0;
	e_invalid.pe_state = C2_PNDS_NR;
	rc = c2_poolmach_state_transit(&pm, &e_invalid);
	C2_UT_ASSERT(rc == -EINVAL);

	/* finally */
	c2_poolmach_fini(&pm);
}

const struct c2_test_suite poolmach_ut = {
	.ts_name = "poolmach-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "pm_test init & fini",   pm_test_init_fini},
		{ "pm_test state transit", pm_test_transit  },
		{ NULL,                    NULL             }
	}
};
C2_EXPORTED(poolmach_ut);
