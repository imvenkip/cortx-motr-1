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
 * Original creation date: 11/16/2011
 */

#ifndef __COLIBRI_LAYOUT_LIST_ENUM_H__
#define __COLIBRI_LAYOUT_LIST_ENUM_H__

/**
   @defgroup list_enum List Enumeration Type.

   List Enumeration Type. A layout with list enumeration type lists all the
   COB identifiers as a part of the layout itself.
   @{
 */

/* import */
#include "lib/tlist.h"	/* struct c2_tl */
#include "lib/mutex.h"	/* struct c2_mutex */
#include "db/db.h"	/* struct c2_table */

#include "layout/layout.h"

/* export */
struct c2_lay_list_enum;

/**
   Extension of generic c2_layout_enum for a list enumeration type.
 */
struct c2_lay_list_enum {
	/** super class */
	struct c2_layout_enum	lle_enum;

	/** List of COB identifiers which are part of this layout */
	struct c2_tl		lle_list_of_cobs;

	/** Lock to protect the list of COB identifiers */
	struct c2_mutex		lle_cob_mutex;
};

struct list_schema_data {
	/** Table to store COB lists for all the layout with LIST enum type. */
	struct c2_table		lsd_cob_lists;
};


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
