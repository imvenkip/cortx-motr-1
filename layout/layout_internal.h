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
#include "fid/fid.h"
#include "lib/vec.h"

struct c2_db_pair;
struct c2_ldb_schema;
struct c2_db_tx;
enum c2_layout_xcode_op;

enum {
	LID_NONE = 0
};

struct list_schema_data {
	/** Table to store COB lists for all the layout with LIST enum type. */
	struct c2_table           lsd_cob_lists;
};

struct ldb_list_cob_entry {
	/** Index for the COB from the layout it is part of. */
	uint32_t                  llce_cob_index;

	/** COB identifier. */
	struct c2_fid             llce_cob_id;
};

enum {
	MAX_INLINE_COB_ENTRIES = 20
};

/**
   Structure used to store MAX_INLINE_COB_ENTRIES number of cob entries inline
   into the layouts table.
*/
struct ldb_inline_cob_entries {
	/** Total number of COB Ids for the specific layout. */
	uint32_t                  llces_nr;

	/** Array for storing COB Ids. */
	struct ldb_list_cob_entry llces_cobs[MAX_INLINE_COB_ENTRIES];
};



/**
   Write layout record to layouts table.
   Used from layout type specific implementation, with layout type
   specific record size.

   @param op - This enum parameter indicates what is the DB operation to be
   performed on the layout record which could be one of ADD/UPDATE/DELETE.
*/
int ldb_layout_write(struct c2_ldb_schema *schema,
		     enum c2_layout_xcode_op op,
		     uint32_t recsize,
		     struct c2_bufvec_cursor *cur,
		     struct c2_db_tx *tx);

/**
 * Copied verbatim from bufvec_xcode.c, need to see how to refactor it.
 * Initializes a c2_bufvec containing a single element of specified size.
 */
void data_to_bufvec(struct c2_bufvec *src_buf, void **data,
			   size_t *len);

/**
 * Copied verbatim from bufvec_xcode.c, need to see how to refactor it.
 * Helper functions to copy opaque data with specified size to and from a
 * c2_bufvec.
 */
int data_to_bufvec_copy(struct c2_bufvec_cursor *cur, void *data,
			       size_t len);



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
