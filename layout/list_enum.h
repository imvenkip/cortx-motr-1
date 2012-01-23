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
 * Original author: Trupti Patil <trupti_patil@xyratex.com>
 * Original creation date: 11/16/2011
 */

#ifndef __COLIBRI_LAYOUT_LIST_ENUM_H__
#define __COLIBRI_LAYOUT_LIST_ENUM_H__

/**
 * @defgroup list_enum List Enumeration Type.
 *
 * List Enumeration Type. A layout with list enumeration type lists all the
 * COB identifiers as a part of the layout itself.
 * @{
 */

/* import */
#include "db/db.h"      /* struct c2_table */
#include "fid/fid.h"    /* struct c2_fid */

#include "layout/layout.h"

/* export */
struct c2_layout_list_enum;

/**
 * Extension of generic c2_layout_enum for a list enumeration type.
 */
struct c2_layout_list_enum {
	/** super class */
	struct c2_layout_enum     lle_base;

	/** List of COB identifiers which are part of this layout */
	struct c2_tl              lle_list_of_cobs;
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
 * Structure used to store MAX_INLINE_COB_ENTRIES number of cob entries inline
 * into the layouts table.
 */
struct ldb_inline_cob_entries {
	/** Total number of COB Ids for the specific layout. */
	uint32_t                  llces_nr;

	/** Array for storing COB Ids. */
	struct ldb_list_cob_entry llces_cobs[MAX_INLINE_COB_ENTRIES];
};


void c2_layout_list_enum_init(struct c2_layout_list_enum *list_enum,
			      struct c2_tl *list_of_cobs,
			      struct c2_layout *l,
			      struct c2_layout_enum_type *lt,
			      struct c2_layout_enum_ops *ops);
void c2_layout_list_enum_fini(struct c2_layout_list_enum *list_enum);

extern const struct c2_layout_enum_type c2_list_enum_type;

/** @} end group list_enum */

/* __COLIBRI_LAYOUT_LIST_ENUM_H__ */
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
