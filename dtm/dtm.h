/* -*- C -*- */

#ifndef __COLIBRI_DTM_H__
#define __COLIBRI_DTM_H__

#include "db/db.h"

/**
   @defgroup dtm Distributed transaction manager
   @{
*/

struct c2_dtm {};

struct c2_dtx {
	/**
	   @todo placeholder for now.
	 */
	struct c2_db_tx tx_dbtx;
};

/** @} end of copymachine group */

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
