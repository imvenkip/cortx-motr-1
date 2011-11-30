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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 *                  Trupti Patil <trupti_patil@xyratex.com>
 * Original creation date: 07/09/2010
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "layout/layout.h"

/**
   @addtogroup layout
   @{
 */

void c2_lay_init(struct c2_lay *lay)
{
   /**
	@code
	Invoke lay->l_ops->lo_init().
	@endcode
   */
	return;
}

void c2_lay_fini(struct c2_lay *lay)
{
   /**
	@code
	Invoke lay->l_ops->lo_fini().
	@endcode
   */
}

/** Adds a reference to the layout. */
void c2_lay_get(struct c2_lay *lay)
{
   /**
	@code
	Invoke lay->l_ops->lo_get().
	@endcode
   */
}

/** Releases a reference on the layout. */
void c2_lay_put(struct c2_lay *lay)
{
   /**
	@code
	Invoke lay->l_ops->lo_put().
	@endcode
   */
}

/**
   This method builds an in-memory layout object from its representation
   either 'stored in the Layout DB' or 'received over the network'.

   Two use cases of c2_lay_decode()
   - Client decodes a buffer received over the network, into an in-memory
     layout structure.
   - Server decodes an on-disk layout record by reading it from the Layout
     DB, into an in-memory layout structure.

   @param fromDB - This flag indicates if the in-memory layout object is
   to be decoded 'from its representation stored in the Layout DB' or
   'from its representation received over the network'.
*/
int c2_lay_decode(bool fromDB, uint64_t lid,
		  const struct c2_bufvec_cursor *cur,
		  struct c2_lay **out)
{
   /**
	@code

	C2_PRE(cur != NULL);

	if (fromDB)
		C2_PRE(lid != INVALID_LID);
	else
		Confirm with C2_PRE() that the cursor does not contain data.

	if (fromDB) {
		struct c2_db_pair	pair;

		uint64_t recsize = sizeof(struct c2_ldb_rec);

		ret = ldb_layout_read(&lid, recsize, &pair)

		Set the cursor cur to point at the beginning of the key-val
		pair.
	}

	Parse generic layout fields from the buffer (pointed by *cur) and store
	them in the layout object. e.g. layout id, layout type id (lt_id),
	enumeration type id (et_id), ref counter.

	Now based on the layout type, call corresponding lto_decode() so as
        to continue decoding the layout type specific fields.

	uint64_t lt_id = *out->l_type->lt_id;
	schema->ls_types[lt_id]->lto_decode(fromDB, lid, cur, out);

	If the layout-enumeration type is LIST, then invoke respective
	leto_decode().

	uint64_t let_id = *out->l_enum->let_id;
	schema->ls_enum[let_id]->leto_decode(fromDB, lid, cur, out);

	@endcode
   */

	return 0;

}

/**
   This method uses an in-memory layout object and either 'stores it in the
   Layout DB' or 'converts it to a buffer that can be passed on over the
   network'.

   Two use cases of c2_lay_encode()
   - Server encodes an in-memory layout object into a buffer, so as to send
     it to the client.
   - Server encodes an in-memory layout object and stores it into the Layout
     DB.

   @param toDB - This flag indicates if 'the layout is to be stored in the
   Layout DB' or 'if it is to be stored in the buffer so that the buffer can
   be passed over the network'.
*/
int c2_lay_encode(bool toDB, const struct c2_lay *l,
		  struct c2_bufvec_cursor *out)
{
   /**
	@code

	C2_PRE(out != NULL);

	Read generic fields from the layout object and store those in
	the buffer.

	Based on the layout type, invoke corresponding lto_encode().

	If the layout-enumeration type is LIST, then invoke respective
	leto_encode().

	@endcode
   */

	return 0;
}


int c2_lays_init(void)
{
   /**
	@code
	Invoke lay->l_ops->lo_init() for all the registered layout types.
	@endcode
   */
	return 0;
}

void c2_lays_fini(void)
{
   /**
	@code
	Invoke lay->l_ops->lo_fini() for all the registered layout types.
	@endcode
   */
}


/**
   Read layout record from layouts table.
   Used from layout type specific implementation, with layout type
   specific record size.

   @todo Add schema and tx as arguments to these functions.

*/
int ldb_layout_read(uint64_t *lid, const uint32_t recsize,
		struct c2_db_pair *pair)
{
   /**
	@code
	struct c2_layout_rec	*rec;

	c2_db_pair_setup(&pair, &schema->ls_layout_entries,
			 lid, sizeof(uint64_t),
			 rec, recsize);

	c2_table_lookup(tx, &pair);

	@endcode
   */
	return 0;
}

/**
   Write layout record to layouts table.
   Used from layout type specific implementation, with layout type
   specific record size.

   @todo Add schema and tx as arguments to these functions.

*/
int ldb_layout_write(const uint32_t recsize,
		     struct c2_bufvec_cursor *cur)
{
   /**
	@code
	struct c2_layout_rec	*rec;
	struct c2_db_pair	 pair;

	Collect data into lid and rec from the buffer pointed by cur and
	by referring the recsize.

	c2_db_pair_setup(&pair, &schema->ls_layout_entries,
			 lid, sizeof(uint64_t),
			 rec, recsize);

	c2_table_insert(tx, &pair);

	@endcode
   */
	return 0;
}


/** @} end group layout */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
