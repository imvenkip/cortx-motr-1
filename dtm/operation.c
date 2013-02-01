/* -*- C -*- */
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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 27-Jan-2013
 */


/**
 * @addtogroup dtm
 *
 * @{
 */

#include "lib/errno.h"
#include "lib/memory.h"

#include "dtm/update.h"
#include "dtm/operation.h"

M0_INTERNAL int m0_dtm_operation_init(struct m0_dtm_operation *oper,
				      unsigned nr)
{
	M0_ALLOC_ARR(oper->oprt_up, nr);
	oper->oprt_nr  = nr;
	oper->oprt_idx = 0;
	m0_dtm_op_init(&oper->oprt_op);
	return oper->oprt_up != NULL ? 0 : -ENOMEM;
}

M0_INTERNAL void m0_dtm_operation_fini(struct m0_dtm_operation *oper)
{
	m0_dtm_op_fini(&oper->oprt_op);
	if (oper->oprt_up != NULL)
		m0_free(oper->oprt_up);
}

M0_INTERNAL void m0_dtm_operation_add(struct m0_dtm_operation *oper,
				      struct m0_dtm_object *obj,
				      enum m0_dtm_up_rule rule,
				      m0_dtm_ver_t ver, m0_dtm_ver_t orig_ver)
{
	M0_PRE(oper->oprt_idx < oper->oprt_nr);
	m0_dtm_update_init(&oper->oprt_up[oper->oprt_idx++],
			   obj, &oper->oprt_op, rule, ver, orig_ver);
}


/** @} end of dtm group */


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
