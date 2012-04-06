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
#include "layout/layout.h"

struct c2_fid;

/* export */
struct c2_layout_list_enum;

/**
 * Extension of generic c2_layout_enum for a list enumeration type.
 */
struct c2_layout_list_enum {
	/** Super class. */
	struct c2_layout_enum   lle_base;

	/** Number of elements present in the enumeration. */
	uint32_t                lle_nr;

	/**
	 * Pointer to an array of COB identifiers for the component objects
	 * which are part of 'the layout this enum is assocaited with'.
	 * @todo In kernel any allocation over 4KB is not safe. Thus, this
	 * array can safely hold only upto 256 number of COB identifiers,
	 * (c2_fid being 16 bytes in size).
	 * This issue is to be addressed later.
	 */
	struct c2_fid          *lle_list_of_cobs;

	uint64_t                lle_magic;
};

bool c2_list_enum_invariant(const struct c2_layout_list_enum *list_enum,
			    uint64_t lid);
int c2_list_enum_build(struct c2_layout_domain *dom,
		       uint64_t lid, struct c2_fid *cob_list, uint32_t nr,
		       struct c2_layout_list_enum **out);

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
