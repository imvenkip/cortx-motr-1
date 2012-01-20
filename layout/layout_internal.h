/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Trupti Patil <trupti_patil@xyratex.com>
 * Original creation date: 01/11/2011
 */

#ifndef __COLIBRI_LAYOUT_LAYOUT_INTERNAL_H__
#define __COLIBRI_LAYOUT_LAYOUT_INTERNAL_H__

/**
   @addtogroup layout
   @{
 */

#include "db/db.h"

struct c2_layout;
struct c2_ldb_rec;
struct c2_db_pair;
struct c2_ldb_schema;
struct c2_db_tx;
enum c2_layout_xcode_op;

enum {
	LID_NONE = 0
};

bool layout_invariant(const struct c2_layout *l);
bool ldb_rec_invariant(const struct c2_ldb_rec *l);

/**
   Write layout record to layouts table.
   Used from layout type specific implementation, with layout type
   specific record size.

   @param op - This enum parameter indicates what is the DB operation to be
   performed on the layout record which could be one of ADD/UPDATE/DELETE.
*/
int ldb_layout_write(struct c2_ldb_schema *schema,
		     enum c2_layout_xcode_op op,
		     uint64_t lid,
		     struct c2_db_pair *pair,
		     uint32_t recsize,
		     struct c2_db_tx *tx);


/** @} end group layout */

/* __COLIBRI_LAYOUT_LAYOUT_INTERNAL_H__ */
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
