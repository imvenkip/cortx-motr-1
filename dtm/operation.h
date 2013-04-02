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
#include "dtm/update.h"
struct m0_dtm_remote;
struct m0_dtm;
struct m0_tl;

/* export */
struct m0_dtm_oper;
struct m0_dtm_oper_ops;

struct m0_dtm_oper {
	struct m0_dtm_op oprt_op;
	struct m0_tl     oprt_uu;
};
M0_INTERNAL bool m0_dtm_oper_invariant(const struct m0_dtm_oper *oper);

struct m0_dtm_oper_descr {
	uint32_t                    od_nr;
	struct m0_dtm_update_descr *od_update;
} M0_XCA_SEQUENCE;

M0_INTERNAL void m0_dtm_oper_init(struct m0_dtm_oper *oper, struct m0_dtm *dtm,
				  struct m0_tl *uu);
M0_INTERNAL void m0_dtm_oper_fini(struct m0_dtm_oper *oper);
M0_INTERNAL void m0_dtm_oper_add(struct m0_dtm_oper *oper,
				 struct m0_dtm_update *update,
				 struct m0_dtm_history *history,
				 const struct m0_dtm_update_data *data);
M0_INTERNAL void m0_dtm_oper_close(const struct m0_dtm_oper *oper);
M0_INTERNAL void m0_dtm_oper_prepared(const struct m0_dtm_oper *oper);
M0_INTERNAL void m0_dtm_oper_done(const struct m0_dtm_oper *oper,
				  const struct m0_dtm_remote *dtm);
M0_INTERNAL void m0_dtm_oper_pack(const struct m0_dtm_oper *oper,
				  const struct m0_dtm_remote *dtm,
				  struct m0_dtm_oper_descr *ode);
M0_INTERNAL void m0_dtm_oper_unpack(struct m0_dtm_oper *oper,
				    const struct m0_dtm_oper_descr *ode);
M0_INTERNAL int  m0_dtm_oper_build(struct m0_dtm_oper *oper, struct m0_tl *uu,
				   const struct m0_dtm_oper_descr *ode);
M0_INTERNAL void m0_dtm_reply_pack(const struct m0_dtm_oper *oper,
				   const struct m0_dtm_oper_descr *request,
				   struct m0_dtm_oper_descr *reply);
M0_INTERNAL void m0_dtm_reply_unpack(struct m0_dtm_oper *oper,
				     const struct m0_dtm_oper_descr *reply);

M0_INTERNAL struct m0_dtm_update *m0_dtm_oper_get(const struct m0_dtm_oper *oper,
						  uint8_t label);

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
