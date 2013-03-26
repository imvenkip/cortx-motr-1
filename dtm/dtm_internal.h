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
 * Original creation date: 19-Mar-2013
 */


#pragma once

#ifndef __MERO_DTM_DTM_INTERNAL_H__
#define __MERO_DTM_DTM_INTERNAL_H__

/**
 * @defgroup dtm
 *
 * @{
 */

/* import */
#include "lib/tlist.h"
struct m0_dtm_up;
struct m0_dtm_update;
struct m0_dtm_hi;
struct m0_dtm_nu;
struct m0_dtm_history;
struct m0_dtm_remote;

M0_TL_DESCR_DECLARE(hi, M0_EXTERN);
M0_TL_DECLARE(hi, M0_EXTERN, struct m0_dtm_up);
M0_TL_DESCR_DECLARE(op, M0_EXTERN);
M0_TL_DECLARE(op, M0_EXTERN, struct m0_dtm_up);

#define up_for(o, up)				\
do {						\
	struct m0_dtm_up *up;			\
						\
	m0_tl_for(op, &(o)->op_ups, up)

#define up_endfor				\
	m0_tl_endfor;				\
} while (0)

#define hi_for(h, up)				\
do {						\
	struct m0_dtm_up *up;			\
						\
	m0_tl_for(hi, &(h)->hi_ups, up)

#define hi_endfor				\
	m0_tl_endfor;				\
} while (0)

M0_TL_DESCR_DECLARE(history, M0_EXTERN);
M0_TL_DECLARE(history, M0_EXTERN, struct m0_dtm_update);
M0_TL_DESCR_DECLARE(oper, M0_EXTERN);
M0_TL_DECLARE(oper, M0_EXTERN, struct m0_dtm_update);

#define oper_for(o, update)				\
do {							\
	struct m0_dtm_update *update;			\
							\
	m0_tl_for(oper, &(o)->oprt_op.op_ups, update)

#define oper_endfor				\
	m0_tl_endfor;				\
} while (0)

#define history_for(h, update)				\
do {							\
	struct m0_dtm_update *update;			\
							\
	m0_tl_for(history, &(h)->h_hi.hi_ups, update)

#define history_endfor				\
	m0_tl_endfor;				\
} while (0)

#define UPDATE_UP(update)				\
({							\
	typeof(update) __update = (update);		\
	__update != NULL ? &__update->upd_up : NULL;	\
})

M0_INTERNAL struct m0_dtm *nu_dtm(struct m0_dtm_nu *nu);
M0_INTERNAL struct m0_dtm_history *hi_history(struct m0_dtm_hi *hi);
M0_INTERNAL struct m0_dtm_update *up_update(struct m0_dtm_up *up);
M0_INTERNAL struct m0_dtm_history_remote *
history_remote(const struct m0_dtm_history *history,
	       const struct m0_dtm_remote  *dtm);

/** @} end of dtm group */

#endif /* __MERO_DTM_DTM_INTERNAL_H__ */


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
