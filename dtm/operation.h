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


#pragma once

#ifndef __MERO_DTM_OPERATION_H__
#define __MERO_DTM_OPERATION_H__


/**
 * @defgroup dtm
 *
 * @{
 */

/* import */
#include "dtm/nucleus.h"
struct m0_dtm_update;
struct m0_dtm_object;

/* export */
struct m0_dtm_operation;

struct m0_dtm_operation {
	struct m0_dtm_op      oprt_op;
	unsigned              oprt_nr;
	unsigned              oprt_idx;
	struct m0_dtm_update *oprt_up;
};

M0_INTERNAL int  m0_dtm_operation_init(struct m0_dtm_operation *oper,
				       unsigned nr);
M0_INTERNAL void m0_dtm_operation_fini(struct m0_dtm_operation *oper);
M0_INTERNAL void m0_dtm_operation_add (struct m0_dtm_operation *oper,
				       struct m0_dtm_object *obj,
				       enum m0_dtm_up_rule rule,
				       m0_dtm_ver_t ver, m0_dtm_ver_t orig_ver);
/** @} end of dtm group */

#endif /* __MERO_DTM_OPERATION_H__ */


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
