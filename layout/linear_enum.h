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

#ifndef __COLIBRI_LAYOUT_LINEAR_ENUM_H__
#define __COLIBRI_LAYOUT_LINEAR_ENUM_H__

#include "layout/layout.h"

/**
   @defgroup linear_enum Linear Enumeration Type.

   A layout with linear enumeration type stores a formula that is
   used to enumerate all the component object identifiers. Thus, this
   type of a layout does not store the component object identifiers.
   @{
*/

/* import */

/* export */

/**
   Extension of generic c2_layout_enum for a linear enumeration type.
 */
struct c2_layout_linear_enum {
	/** super class */
	struct c2_layout_enum         lline_enum;

	const struct c2_layout_linear_formula *lline_form;
	const struct c2_layout_linear_parameter *lline_actuals;
};

struct c2_layout_linear_formula {
	const struct c2_layout_type        *llinf_type;
	const uint64_t                      llinf_id;
	const struct c2_layout_formula_ops *llinf_ops;
};

struct c2_layout_linear_formula_ops {
	int (*llinfo_subst)(const struct c2_layout_linear_formula *form,
			    uint16_t nr,
			    const struct c2_layout_linear_parameter *actuals,
			    struct c2_layout **out);
};

struct c2_layout_linear_parameter {
	const struct c2_layout_parameter_type *llinp_type;
	const void                            *llinp_value;
};

struct c2_layout_linear_parameter_type {
	const char *llinpt_name;
	int       (*llinpt_convert)(const struct c2_layout_parameter *other,
				    struct c2_layout_parameter *out);
};


/**
   Implementation of lto_decode() for pdclust layout type.
   Continues to decode layout representation stored in the buffer and
   to create the layout.
*/
static int pdclust_decode(const struct c2_bufvec_cursor *cur,
		          struct c2_layout **l_out)
{
   /**
   @code
	Read pdclust layout type specific fields from the buffer.
	Based on the layout-enumeration type, call respective leto_decode().
   @endcode
   */

	return 0;
}

/**
   Implementation of lto_encode() for pdclust layout type.
   Stores layout representation in the buffer.
*/
static int pdclust_encode(const struct c2_layout *l,
		          struct c2_bufvec_cursor *cur_out)
{
   /**
   @code
	Store pdclust layout type specific fields like N, K into the buffer.

	Based on the layout-enumeration type, call respective leto_encode().
   @endcode
   */

	return 0;
}


/**
   Implementation of lto_rec_add for PDCLUST layout type.
*/
int pdclust_rec_add(const struct c2_bufvec_cursor *cur,
		struct c2_layout_schema *l_schema,
		struct c2_db_tx *tx)
{
   /**
	@code
	Adds a layout entry into the layout_entries table.
	Invokes enumeration type specific leto_rec_add().
	@endcode
   */
}

/**
   Implementation of lto_rec_delete for PDCLUST layout type.
*/
int pdclust_rec_delete(const struct c2_bufvec_cursor *cur,
		struct c2_layout_schema *l_schema,
		struct c2_db_tx *tx)
{
   /**
	@code
	If the enumeration type is LIST, delete the layout record
	from the layout_ntries table if the reference count is 0.
	And invoke enumeration type specific leto_rec_add().

	If the enumeration type is LINEAR, do not delete the
	layout record even if the reference count is 0. (PDCLUST
	type of layout with LINEAR enumberation type is never
	deleted.)

	@endcode
   */
}

/**
   Implementation of lto_rec_update for PDCLUST layout type.
*/
int pdclust_rec_update(const struct c2_bufvec_cursor *cur,
		struct c2_layout_schema *l_schema,
		struct c2_db_tx *tx)
{
   /**
	@code
	Updates the layout entry in the layout_entries table.
	Invokes enumeration type specific leto_rec_update().
	@endcode
   */
}

/**
   Implementation of l_rec_lookup for PDCLUST layout type.
*/
int pdclust_rec_lookup(const struct c2_layout_id l_id,
		struct c2_layout_schema *l_schema,
		struct c2_db_tx *tx,
		struct c2_bufvec_cursor *cur)
{
   /**
	@code
	Obtains the layout record with the specified layout id, from the
	layout_entries table.
	Invoke layout enumeration type specific leto_rec_lookup.
	@endcode
   */
}


extern const struct c2_layout_enum_type c2_layout_linear_enum_type;

/** @} end group linear_enum */

/* __COLIBRI_LAYOUT_LINEAR_ENUM_H__ */
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
