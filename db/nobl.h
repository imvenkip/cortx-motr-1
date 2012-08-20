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
 * Original creation date: 08/13/2010
 */

#pragma once

#ifndef __COLIBRI_DB_NOBL_H__
#define __COLIBRI_DB_NOBL_H__

#include "lib/chan.h"

/**
   @addtogroup db

   <b>Non-blocking db interface (nobl)</b>

   nobl allows non-blocking db calls where the caller remains control without
   waiting for db operation completion. The completion is signalled later via
   channel.

   The goal of nobl is to integrate db with non-blocking request handler.

   Only some db operations are supported by nobo. Others (data-base environment
   operations, table initialisation and finalisation, flush and few others) are
   not supposed to be called in non-blockable contexts anyway.

   @{
 */

/* export */
enum c2_nobl_state;
enum c2_nobl_opcode;
struct c2_nobl_ctx;

/**
   A context in which nobl operation proceeds.

   Context object is created by a nobl user and supplied to nobl entry
   point. A context object is used to notify the caller about operation
   progress and to track resources associated with the operation.

   A context object is embedded into a c2_db_tx.
 */
struct c2_nobl_ctx {
	enum c2_nobl_opcode  nc_opcode;
	enum c2_nobl_state   nc_state;
	int32_t              nc_rc;
	struct c2_chan       nc_signal;
	struct c2_db_pair   *nc_pair;
	struct c2_db_cursor *nc_cur;
};

enum c2_nobl_state {
	NC_INITIALISED = 1,
	NC_ONGOING,
	NC_DONE
};

enum c2_nobl_opcode {
	NOBO_TX_INIT,
	NOBO_TX_ABORT,
	NOBO_TX_COMMIT,
	NOBO_TABLE_INSERT,
	NOBO_TABLE_UPDATE,
	NOBO_TABLE_LOOKUP,
	NOBO_TABLE_DELETE,
	NOBO_CURSOR_GET,
	NOBO_CURSOR_GET,
	NOBO_CURSOR_NEXT,
	NOBO_CURSOR_PREV,
	NOBO_CURSOR_FIRST,
	NOBO_CURSOR_LAST,
	NOBO_CURSOR_SET,
	NOBO_CURSOR_ADD,
	NOBO_CURSOR_DEL,
	NOBO_NR
};

/** @} end of db group */

/* __COLIBRI_DB_NOBL_H__ */
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
