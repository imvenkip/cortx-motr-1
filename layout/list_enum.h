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
#include "db/db.h" /* struct c2_table */
#include "layout/layout.h"

struct c2_fid;

/* export */
struct c2_layout_list_enum;

/** Extension of generic c2_layout_enum for a list enumeration type. */
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

/**
 * Builds list enumeration object.
 * @post ergo(rc == 0, list_invariant_internal(lin_enum))
 *
 * @note Enum object need not be finalised explicitly by the user. It is
 * finalised internally through c2_layout__striped_fini().
 */
int c2_list_enum_build(struct c2_layout_domain *dom,
		       struct c2_fid *cob_list, uint32_t nr,
		       struct c2_layout_list_enum **out);

/**
 * Finalises list enumeration object.
 * @note This interface is expected to be used only in cases where layout
 * build operation fails and the user (for example c2t1fs) needs to get rid of
 * the enumeration object created prior to attempting the layout build
 * operation. In the other regular cases, enumeration object is finalised
 * internally through c2_layout__striped_fini().
 */
void c2_list_enum_fini(struct c2_layout_list_enum *e);

extern struct c2_layout_enum_type c2_list_enum_type;

/** @} end group list_enum */

/**
 * Following structure is part of the internal implementation. It is required to
 * be accessed by the UT as well. Hence, is placed here in the header file.
 *
 * Structure used to store cob entries inline into the layouts table - maximum
 * upto LDB_MAX_INLINE_COB_ENTRIES number of those.
 */
struct cob_entries_header {
	/** Total number of COB Ids for the specific layout. */
	uint32_t  ces_nr;

	/**
	 * Payload storing list of cob ids (struct c2_fid), max upto
	 * LDB_MAX_INLINE_COB_ENTRIES number of those.
	 */
	char      ces_cobs[0];
};

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
