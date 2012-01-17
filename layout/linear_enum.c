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

#include "lib/tlist.h"	/* struct c2_tl */
#include "lib/vec.h"
#include "layout/layout_internal.h"
#include "layout/linear_enum.h"

/**
   @addtogroup linear_enum

   Notes:
   - Layout enumeration type specific register/unregister methods are not
     required for "linear" enumeration type, since the layout schema does not
     contain any separate tables specifically for "linear" enumeration type.

   @{
*/

/**
   Implementation of leto_recsize() for linear enumeration type.
   Returns record size for the part of the record required to store LINEAR enum
   details viz. nr, A, B.
*/
static uint64_t linear_recsize(void)
{
	return sizeof(struct c2_layout_linear_attr);
}


/**
   Implementation of leto_decode() for linear enumeration type.
   Reads linear enumeration type specific attributes from the buffer into
   the c2_layout_linear_enum::c2_layout_linear_attr object.
*/
static int linear_decode(struct c2_ldb_schema *schema,
			uint64_t lid,
			struct c2_bufvec_cursor *cur,
			enum c2_layout_xcode_op op,
			struct c2_db_tx *tx,
			struct c2_layout **out)
{
	struct c2_layout_striped     *stl;
	struct c2_layout_linear_enum *lin_enum;
	struct c2_layout_linear_attr *lin_attr;

	stl = container_of(*out, struct c2_layout_striped, ls_base);

	lin_enum = container_of(stl->ls_enum, struct c2_layout_linear_enum,
			lle_base);

	lin_attr = (struct c2_layout_linear_attr *)c2_bufvec_cursor_addr(cur);

	lin_enum->lle_attr.lla_nr = lin_attr->lla_nr;
	lin_enum->lle_attr.lla_A  = lin_attr->lla_A;
	lin_enum->lle_attr.lla_B  = lin_attr->lla_B;

	/**
	   There is no more data expected in the buffer for the linear
	   enumeration. Thus the buffer is expected to be at the end.
	 */
	C2_ASSERT(c2_bufvec_cursor_move(cur, 0));

	return 0;
}

/**
   Implementation of leto_encode() for linear enumeration type.
   Reads linear enumeration type specific attributes from the
   c2_layout_linear_enum object into the buffer.
*/
static int linear_encode(struct c2_ldb_schema *schema,
			 const struct c2_layout *l,
			 enum c2_layout_xcode_op op,
			 struct c2_db_tx *tx,
			 struct c2_bufvec_cursor *out)
{
	struct c2_layout_striped     *stl;
	struct c2_layout_linear_enum *lin_enum;

	stl = container_of(l, struct c2_layout_striped, ls_base);

	lin_enum = container_of(stl->ls_enum, struct c2_layout_linear_enum,
			lle_base);

	data_to_bufvec_copy(out, lin_enum,
			sizeof(struct c2_layout_linear_enum));

	return 0;
}

/**
   Implementation of leo_nr for LINEAR enumeration.
   Rerurns number of objects in the enumeration.
*/
static uint32_t linear_nr(const struct c2_layout_enum *le)
{
   /**
	@code
	struct c2_layout_linear_enum *lin;

	lin = container_of(le, struct c2_layout_linear_enum, lle_base);
	return lin->lle_attr.lla_nr;
	@endcode
   */
	return 0;
}

/**
   Implementation of leo_get for LINEAR enumeration.
   Rerurns idx-th object from the enumeration.
*/
static void linear_get(const struct c2_layout_enum *le,
		       uint32_t idx,
		       const struct c2_fid *gfid,
		       struct c2_fid *out)
{
   /**
	@code
	struct c2_layout_linear_enum *lin;

	lin = container_of(le, struct c2_layout_linear_enum, lle_base);

	out->f_key = gfid->f_key;
	out->f_container = lin->lle_attr.lla_A + idx * lin->lle_attr.lla_B;

	@endcode
   */
}


static const struct c2_layout_enum_ops linear_enum_ops = {
	.leo_nr         = linear_nr,
	.leo_get        = linear_get
};

static const struct c2_layout_enum_type_ops linear_type_ops = {
	.leto_register       = NULL,
	.leto_unregister     = NULL,
	.leto_recsize        = linear_recsize,
	.leto_decode         = linear_decode,
	.leto_encode         = linear_encode
};

const struct c2_layout_enum_type c2_linear_enum_type = {
	.let_name       = "linear",
	.let_id         = 0x4C494E454152454E, /* LINEAREN */
	.let_ops        = &linear_type_ops
};

/** @} end group linear_enum */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
