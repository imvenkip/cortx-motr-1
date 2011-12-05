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

/**
   Initializes layout schema, creates generic table to store layout records.
   Registers all the layout types amd enum types by creating layout type and
   enum type specific tables, if they do not exist already.
*/
int c2_layouts_init(void)
{
   /**
	@code
	Invoke c2_ldb_schema_init().

	Register pdclust layout type, using c2_ldb_type_register().
	Register composite layout type, using c2_ldb_type_register().

	Register list enumeration type, using c2_ldb_enum_register().
	Register formula enumeration type, using c2_ldb_enum_register().
	@endcode
   */
	return 0;
}

/** Unregisters all the layout types amd enum types. */
void c2_layouts_fini(void)
{
   /**
	@code
	Unregister pdclust layout type, using c2_ldb_type_unregister().
	Unregister composite layout type, using c2_ldb_type_unregister().

	Unregister list enumeration type, using c2_ldb_enum_unregister().
	Unregister formula enumeration type, using c2_ldb_enum_unregister().
	@endcode
   */
}

void c2_layout_init(struct c2_layout *lay,
		    const uint64_t id,
		    const struct c2_layout_type *type,
		    const struct c2_layout_enum *e,
		    const struct c2_layout_ops *ops)
{
   /**
	@code
	lay->l_id	= id;
	lay->l_type	= type;
	lay->l_enum	= e;
	lay->l_ops	= ops;
	@endcode
   */
	return;
}

void c2_layout_fini(struct c2_layout *lay)
{
}

/** Adds a reference to the layout. */
void c2_layout_get(struct c2_layout *lay)
{
   /**
	@code
	Increases reference on layout by incrementing c2_layout::l_ref and
	uses c2_ldb_rec_update() to increase refernce on the layout record from
	the layout DB.
	@endcode
   */
}

/** Releases a reference on the layout. */
void c2_layout_put(struct c2_layout *lay)
{
   /**
	@code
	Decreases reference on layout by decrementing c2_layout::l_ref and
	uses c2_ldb_rec_update() to decrease refernce on the layout record from
	the layout DB.
	@endcode
   */
}

/**
   This method builds an in-memory layout object from its representation
   either 'stored in the Layout DB' or 'received over the network'.

   Two use cases of c2_layout_decode()
   - Client decodes a buffer received over the network, into an in-memory
     layout structure.
   - Server decodes an on-disk layout record by reading it from the Layout
     DB, into an in-memory layout structure.

   @param fromDB - This flag indicates if the in-memory layout object is
   to be decoded 'from its representation stored in the Layout DB' or
   'from its representation received over the network'.
*/
int c2_layout_decode(bool fromDB, const uint64_t lid,
		     struct c2_ldb_schema *schema,
		     struct c2_db_tx *tx,
		     const struct c2_bufvec_cursor *cur,
		     struct c2_layout **out)
{
   /**
	@code


	if (fromDB) {
		C2_PRE(lid != LID_NONE);
		C2_PRE(schema != NULL);
		C2_PRE(tx != NULL);
		C2_PRE(cur == NULL);

		Allocate bufvec using C2_BUFVEC_INIT_BUF.
		Have cursor cur pointing to it using C2_BUFVEC_INIT_BUF.
	} else {
		C2_PRE(cur != NULL);
	}


	if (fromDB) {
		struct c2_db_pair	pair;

		uint64_t recsize = sizeof(struct c2_ldb_rec);

		ret = ldb_layout_read(&lid, recsize, &pair, schema, tx)

		Set the cursor cur to point at the beginning of the key-val
		pair.
	}

	Parse generic layout fields from the buffer (pointed by *cur) and store
	them in the layout object. e.g. layout id (l_id), layout type id,
	enumeration type id, ref counter.

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

   Two use cases of c2_layout_encode()
   - Server encodes an in-memory layout object into a buffer, so as to send
     it to the client.
   - Server encodes an in-memory layout object and stores it into the Layout
     DB.

   @param toDB - This flag indicates if 'the layout is to be stored in the
   Layout DB' or 'if it is to be stored in the buffer so that the buffer can
   be passed over the network'.

   @param ifupdate - This flag indicates if 'the layout record is to be written
   to the Layout DB' or 'if it is to be updated'.
*/
int c2_layout_encode(bool toDB, bool ifupdate,
		     const struct c2_layout *l,
		     struct c2_ldb_schema *schema,
		     struct c2_db_tx *tx,
		     struct c2_bufvec_cursor *out)
{
   /**
	@code
	if (toDB) {
		C2_PRE(schema != NULL);
		C2_PRE(tx != NULL);
		C2_PRE(out == NULL);

		Allocate bufvec using C2_BUFVEC_INIT_BUF.
		Have cursor cur pointing to it using C2_BUFVEC_INIT_BUF.
	} else {
		C2_PRE(out != NULL);
	}

	Read generic fields from the layout object and store those in
	the buffer pointed by cur

	Based on the layout type, invoke corresponding lto_encode().

	If the layout-enumeration type is LIST, then invoke respective
	leto_encode().

	@endcode
   */

	return 0;
}


/**
   Read layout record from layouts table.
   Used from layout type specific implementation, with layout type
   specific record size.
*/
int ldb_layout_read(uint64_t *lid, const uint32_t recsize,
		    struct c2_db_pair *pair,
		    struct c2_ldb_schema *schema,
		    struct c2_db_tx *tx)
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
*/
int ldb_layout_write(bool ifupdate,
		     const uint32_t recsize,
		     struct c2_bufvec_cursor *cur,
		     struct c2_ldb_schema *schema,
		     struct c2_db_tx *tx)
{
   /**
	@code
	struct c2_layout_rec	*rec;
	struct c2_db_pair	 pair;

	Collect data into lid and rec, from the buffer pointed by cur and
	by referring the recsize.

	c2_db_pair_setup(&pair, &schema->ls_layout_entries,
			 lid, sizeof(uint64_t),
			 rec, recsize);

	if (ifupdate) {
		c2_table_insert(tx, &pair);
	} else {
		c2_table_update(tx, &pair);
	}
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
