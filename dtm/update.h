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
 * Original creation date: 01-Feb-2013
 */


#pragma once

#ifndef __MERO_DTM_UPDATE_H__
#define __MERO_DTM_UPDATE_H__


/**
 * @defgroup dtm
 *
 * @{
 */

#include "dtm/nucleus.h"

/* export */
struct m0_dtm_update;
struct m0_dtm_update_ops;
struct m0_dtm_update_type;
struct m0_dtm_cupdate;
struct m0_dtm_cupdate_ops;
struct m0_dtm_update_descr;
struct m0_dtm_cupdate_descr;

/* import */
struct m0_dtm_history_type;

struct m0_dtm_update {
	struct m0_dtm_up                upd_up;
	uint32_t                        upd_label;
	const struct m0_dtm_update_ops *upd_ops;
};

enum {
	M0_DTM_USER_UPDATE_BASE = 0x1000
};

struct m0_dtm_update_ops {
	int (*updo_redo)(struct m0_dtm_update *updt);
	int (*updo_undo)(struct m0_dtm_update *updt);
	const struct m0_dtm_update_type *updto_type;
};

struct m0_dtm_update_type {
	const char                       *updtt_name;
	const struct m0_dtm_history_type *updtt_htype;
};

struct m0_dtm_update_descr {
	uint32_t          udd_htype;
	uint32_t          udd_label;
	uint64_t          udd_rule;
	uint64_t          udd_ver;
	uint64_t          udd_orig_ver;
	struct m0_uint128 udd_id;
} M0_XCA_RECORD;

M0_INTERNAL void m0_dtm_update_init(struct m0_dtm_update *update,
				    struct m0_dtm_history *history,
				    struct m0_dtm_oper *oper,
				    enum m0_dtm_up_rule rule,
				    m0_dtm_ver_t ver, m0_dtm_ver_t orig_ver);
M0_INTERNAL bool m0_dtm_update_is_user(const struct m0_dtm_update *update);
M0_INTERNAL void m0_dtm_update_pack(const struct m0_dtm_update *update,
				    struct m0_dtm_update_descr *updd);
M0_INTERNAL void m0_dtm_update_unpack(struct m0_dtm_update *update,
				      const struct m0_dtm_update_descr *updd);
M0_INTERNAL int m0_dtm_update_build(struct m0_dtm_update *update,
				    struct m0_dtm_oper *oper,
				    const struct m0_dtm_update_descr *updd);

/** @} end of dtm group */

#endif /* __MERO_DTM_UPDATE_H__ */


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
