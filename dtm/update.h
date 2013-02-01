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

struct m0_dtm_update {
	struct m0_dtm_up                updt_up;
	const struct m0_dtm_update_ops *updt_ops;
};

struct m0_dtm_update_ops {
	int (*updto_redo)(struct m0_dtm_update *updt);
	int (*updto_undo)(struct m0_dtm_update *updt);
	const struct m0_dtm_update_type *updto_type;
};

struct m0_dtm_update_type {
	const char *updtt_name;
};

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
