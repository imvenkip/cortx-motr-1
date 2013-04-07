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
 * Original creation date: 21-Feb-2013
 */


#pragma once

#ifndef __MERO_DTM_HISTORY_H__
#define __MERO_DTM_HISTORY_H__

#include "lib/tlist.h"
#include "lib/queue.h"

#include "dtm/nucleus.h"

/**
 * @defgroup dtm
 *
 * @{
 */

/* import */
#include "dtm/operation.h"
struct m0_dtm;

/* export */
struct m0_dtm_history;
struct m0_dtm_remote;
struct m0_dtm_remote_ops;
struct m0_dtm_history_ops;
struct m0_dtm_history_type;
struct m0_dtm_history_type_ops;

struct m0_dtm_history {
	struct m0_dtm_hi                 h_hi;
	struct m0_queue_link             h_pending;
	struct m0_tlink                  h_catlink;
	struct m0_dtm_remote            *h_dtm;
	struct m0_dtm_update            *h_persistent;
	const struct m0_dtm_history_ops *h_ops;
};
M0_INTERNAL bool m0_dtm_history_invariant(const struct m0_dtm_history *history);

enum m0_dtm_history_flags {
	M0_DHF_CLOSED = M0_DHF_LAST
};

struct m0_dtm_history_ops {
	const struct m0_dtm_history_type *hio_type;
	void (*hio_id        )(const struct m0_dtm_history *history,
			       struct m0_uint128 *id);
	void (*hio_persistent)(struct m0_dtm_history *history);
	void (*hio_fixed     )(struct m0_dtm_history *history);
	int  (*hio_update    )(struct m0_dtm_history *history, uint8_t id,
			       struct m0_dtm_update *update);
};

struct m0_dtm_history_type {
	uint8_t                               hit_id;
	const char                           *hit_name;
	const struct m0_dtm_history_type_ops *hit_ops;
};

struct m0_dtm_history_type_ops {
	int (*hito_find)(const struct m0_dtm_history_type *ht,
			 const struct m0_uint128 *id,
			 struct m0_dtm_history **out);
};

struct m0_dtm_controlh {
	struct m0_dtm_history ch_history;
	struct m0_dtm_oper    ch_clop;
	struct m0_dtm_update  ch_clup;
};

struct m0_dtm_remote {
	const struct m0_dtm_remote_ops *re_ops;
	struct m0_dtm_controlh          re_fol;
};

struct m0_dtm_remote_ops {
};

M0_INTERNAL void m0_dtm_history_init(struct m0_dtm_history *history,
				     struct m0_dtm *dtm);
M0_INTERNAL void m0_dtm_history_fini(struct m0_dtm_history *history);

M0_INTERNAL void m0_dtm_history_persistent(struct m0_dtm_history *history,
					   m0_dtm_ver_t upto);
M0_INTERNAL void m0_dtm_history_close(struct m0_dtm_history *history);

M0_INTERNAL void
m0_dtm_history_type_register(struct m0_dtm *dtm,
			     const struct m0_dtm_history_type *ht);
M0_INTERNAL void
m0_dtm_history_type_deregister(struct m0_dtm *dtm,
			       const struct m0_dtm_history_type *ht);
M0_INTERNAL const struct m0_dtm_history_type *
m0_dtm_history_type_find(struct m0_dtm *dtm, uint8_t id);

M0_INTERNAL void m0_dtm_history_add_nop(struct m0_dtm_history *history,
					struct m0_dtm_oper *oper,
					struct m0_dtm_update *cupdate);
M0_INTERNAL void m0_dtm_history_add_close(struct m0_dtm_history *history,
					  struct m0_dtm_oper *oper,
					  struct m0_dtm_update *cupdate);

M0_INTERNAL void m0_dtm_controlh_init(struct m0_dtm_controlh *ch,
				      struct m0_dtm *dtm);
M0_INTERNAL void m0_dtm_controlh_fini(struct m0_dtm_controlh *ch);
M0_INTERNAL void m0_dtm_controlh_add(struct m0_dtm_controlh *ch,
				     struct m0_dtm_oper *oper);
M0_INTERNAL void m0_dtm_controlh_close(struct m0_dtm_controlh *ch);
M0_INTERNAL int m0_dtm_controlh_update(struct m0_dtm_history *history,
				       uint8_t id,
				       struct m0_dtm_update *update);
M0_INTERNAL bool
m0_dtm_controlh_update_is_close(const struct m0_dtm_update *update);

M0_INTERNAL void m0_dtm_remote_add(struct m0_dtm_remote *dtm,
				   struct m0_dtm_oper *oper,
				   struct m0_dtm_history *history,
				   struct m0_dtm_update *update);

M0_INTERNAL void m0_dtm_remote_init(struct m0_dtm_remote *remote,
				    struct m0_dtm *local);
M0_INTERNAL void m0_dtm_remote_fini(struct m0_dtm_remote *remote);

/** @} end of dtm group */

#endif /* __MERO_DTM_HISTORY_H__ */


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
