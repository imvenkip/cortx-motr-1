/* -*- C -*- */

#ifndef __COLIBRI_DTM_H__
#define __COLIBRI_DTM_H__

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

struct c2_dtx {
	/**
	   @todo placeholder for now.
	 */
	struct c2_db_tx tx_dbtx;
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

/** @} end of dtm group */

/* __COLIBRI_DTM_H__ */
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
