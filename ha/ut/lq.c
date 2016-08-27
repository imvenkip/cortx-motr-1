/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <max.medved@seagate.com>
 * Original creation date: 24-Jul-2016
 */

/**
 * @addtogroup ha
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_UT
#include "lib/trace.h"

#include "ha/lq.h"
#include "ut/ut.h"

#include "lib/memory.h"         /* M0_ALLOC_PTR */
#include "fid/fid.h"            /* M0_FID */
#include "ha/msg.h"             /* m0_ha_msg */
#include "ha/link.h"            /* m0_ha_link_tags_initial */

void m0_ha_ut_lq(void)
{
	struct m0_ha_link_tags  tags;
	struct m0_ha_lq_cfg     lq_cfg;
	struct m0_ha_msg       *msg;
	struct m0_ha_msg       *msg2;
	struct m0_ha_lq        *lq;
	uint64_t                tag;
	uint64_t                tag2;
	bool                    success;

	M0_ALLOC_PTR(lq);
	M0_UT_ASSERT(lq != NULL);
	M0_ALLOC_PTR(msg);
	M0_UT_ASSERT(msg != NULL);
	lq_cfg = (struct m0_ha_lq_cfg){
	};
	m0_ha_lq_init(lq, &lq_cfg);
	m0_ha_link_tags_initial(&tags, false);
	m0_ha_lq_tags_set(lq, &tags);
	*msg = (struct m0_ha_msg){
		.hm_fid            = M0_FID_INIT(1, 2),
		.hm_source_process = M0_FID_INIT(3, 4),
		.hm_source_service = M0_FID_INIT(5, 6),
		.hm_time           = 0,
		.hm_data = {
			.hed_type = M0_HA_MSG_STOB_IOQ,
		},
	};
	M0_UT_ASSERT(!m0_ha_lq_has_next(lq));
	tag = m0_ha_lq_enqueue(lq, msg);
	M0_UT_ASSERT(tag <  m0_ha_lq_tag_assign(lq));
	M0_UT_ASSERT(tag == m0_ha_lq_tag_next(lq));
	M0_UT_ASSERT(m0_ha_lq_has_tag(lq, tag));
	msg2 = m0_ha_lq_msg(lq, tag);
	M0_UT_ASSERT(m0_ha_msg_eq(msg, msg2));
	M0_UT_ASSERT(m0_ha_lq_has_next(lq));
	M0_UT_ASSERT(!m0_ha_lq_is_delivered(lq, tag));

	msg2 = m0_ha_lq_next(lq);
	M0_UT_ASSERT(m0_ha_msg_eq(msg, msg2));
	M0_UT_ASSERT(tag <  m0_ha_lq_tag_next(lq));
	M0_UT_ASSERT(tag == m0_ha_lq_tag_delivered(lq));
	M0_UT_ASSERT(m0_ha_lq_has_tag(lq, tag));
	M0_UT_ASSERT(!m0_ha_lq_has_next(lq));
	M0_UT_ASSERT(!m0_ha_lq_is_delivered(lq, tag));

	success = m0_ha_lq_try_unnext(lq);
	M0_UT_ASSERT(success);
	success = m0_ha_lq_try_unnext(lq);
	M0_UT_ASSERT(!success);
	msg2 = m0_ha_lq_next(lq);
	M0_UT_ASSERT(m0_ha_msg_eq(msg, msg2));

	m0_ha_lq_mark_delivered(lq, tag);
	M0_UT_ASSERT(tag <  m0_ha_lq_tag_delivered(lq));
	M0_UT_ASSERT(tag == m0_ha_lq_tag_confirmed(lq));
	M0_UT_ASSERT(m0_ha_lq_has_tag(lq, tag));
	M0_UT_ASSERT(!m0_ha_lq_has_next(lq));
	M0_UT_ASSERT(m0_ha_lq_is_delivered(lq, tag));

	tag2 = m0_ha_lq_dequeue(lq);
	M0_UT_ASSERT(tag2 == tag);
	M0_UT_ASSERT(tag2 < m0_ha_lq_tag_confirmed(lq));
	M0_UT_ASSERT(m0_ha_lq_has_tag(lq, tag));
	M0_UT_ASSERT(!m0_ha_lq_has_next(lq));
	M0_UT_ASSERT(m0_ha_lq_is_delivered(lq, tag));

	m0_ha_lq_fini(lq);
	m0_free(msg);
	m0_free(lq);
}

#undef M0_TRACE_SUBSYSTEM

/** @} end of ha group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
