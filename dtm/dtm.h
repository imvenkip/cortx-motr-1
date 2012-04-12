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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 04/01/2010
 */

#ifndef __COLIBRI_DTM_DTM_H__
#define __COLIBRI_DTM_DTM_H__

#include "db/db.h"

/**
   @defgroup dtm Distributed transaction manager
   @{
*/

/* export */
struct c2_dtm;
struct c2_dtx;
struct c2_epoch_id;
struct c2_update_id;

struct c2_dtm {};

enum c2_dtx_state {
	C2_DTX_INIT = 1,
	C2_DTX_OPEN,
	C2_DTX_DONE,
	C2_DTX_COMMIT,
	C2_DTX_STABLE
};

struct c2_dtx {
	/**
	   @todo placeholder for now.
	 */
	enum c2_dtx_state tx_state;
	struct c2_db_tx   tx_dbtx;
};

struct c2_update_id {
	uint32_t ui_node;
	uint64_t ui_update;
};

enum c2_update_state {
	C2_US_INVALID,
	C2_US_VOLATILE,
	C2_US_PERSISTENT,
	C2_US_NR
};

void c2_dtx_init(struct c2_dtx *tx);
int  c2_dtx_open(struct c2_dtx *tx, struct c2_dbenv *env);
void c2_dtx_done(struct c2_dtx *tx);

/** @} end of dtm group */

/* __COLIBRI_DTM_DTM_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
