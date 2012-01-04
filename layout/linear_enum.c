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
#include "layout/linear_enum.h"

/**
   @addtogroup linear_enum

   Notes:
   - Layout enumeration type specific register/unregister methods are not
     required for "linear" enumeration type, since the layout schema does not
     contain any separate tables specifically for "linear" enumeration type.
   - Layout enumeration type specific encode/decode methods are not required
     for "linear" enumeration type, since the attributes required specifically
     for a linear enumeration are stored in the layouts table itself.
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
   Implementation of leto_rec_decode() for linear enumeration type.
   Reads linear enumeration type specific attributes from the buffer into
   the c2_layout_linear_enum::c2_layout_linear_attr object.
*/
static int linear_rec_decode(const struct c2_bufvec_cursor *cur,
			     struct c2_layout_enum *e)
{
   /**
	@code
	Container_of(e) would give an object of the type c2_layout_linear_enum,
	say le.

	Read the linear enumeration type specific attributes like nr, A and B
	from buffer (pointed by cur) into le->lle_attr.
	@endcode
   */
	return 0;
}

/**
   Implementation of leto_rec_encode() for linear enumeration type.
   Reads linear enumeration type specific attributes from the
   c2_layout_linear_enum object into the buffer.
*/
static int linear_rec_encode(const struct c2_layout_enum *e,
			     struct c2_bufvec_cursor *cur)
{
   /**
	@code
	Container_of(e) would give an object of the type c2_layout_linear_enum,
	say le.

	Read the linear enumeration type specific attributes like nr, A and B
	from le->lle_attr, into the buffer (pointed by cur).
	@endcode
   */
	return 0;
}


/**
   Implementation of leo_nr for LINEAR enumeration.
   Rerurns number of objects in the enumeration.
*/
static uint32_t linear_nr(const struct c2_layout_enum *le,
			  struct c2_fid *gfid)
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
		       struct c2_fid *gfid,
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
	.leto_rec_decode     = linear_rec_decode,
	.leto_rec_encode     = linear_rec_encode,
	.leto_decode         = NULL,
	.leto_encode         = NULL
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
