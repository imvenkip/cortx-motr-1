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

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/vec.h"

#include "layout/layout_db.h"
#include "layout/layout_internal.h"
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
	Register linear enumeration type, using c2_ldb_enum_register().
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
	Unregister linear enumeration type, using c2_ldb_enum_unregister().
	@endcode
   */
}

void c2_layout_init(struct c2_layout *lay,
		    uint64_t id,
		    const struct c2_layout_type *type,
		    const struct c2_layout_ops *ops)
{
   /**
	@code
	C2_SET0(lay);

	c2_mutex_init(&lay->l_lock);

	lay->l_id       = id;
	lay->l_type     = type;
	lay->l_ops      = ops;

	@endcode
   */
}

void c2_layout_fini(struct c2_layout *lay)
{
   /**
	@code
	Perform whatever is required to be cleaned up while the object is
	about to be deleted.

	c2_mutex_fini(&lay->l_lock);

	@endcode
   */
}

/** Adds a reference to the layout. */
void c2_layout_get(struct c2_layout *lay)
{
   /**
	@code
	c2_mutex_lock(lay->l_lock);

	Increases reference on layout by incrementing c2_layout::l_ref.

	c2_mutex_unlock(lay->l_lock);
	@endcode
   */
}

/** Releases a reference on the layout. */
void c2_layout_put(struct c2_layout *lay)
{
   /**
	@code
	c2_mutex_lock(lay->l_lock);

	Decreases reference on layout by decrementing c2_layout::l_ref.

	c2_mutex_unlock(lay->l_lock);
	@endcode
   */
}

/**
   This method
   @li Either continues to builds an in-memory layout object from its
       representation 'stored in the Layout DB'
   @li Or builds an in-memory layout object from its representation 'received
       over the network'.

   Two use cases of c2_layout_decode()
   - Client decodes a buffer received over the network, into an in-memory
     layout structure.
   - Server decodes an on-disk layout record by reading it from the Layout
     DB, into an in-memory layout structure. This is done by calling
     c2_ldb_rec_lookup().

   @param op - This enum parameter indicates what is the DB operation to be
   performed on the layout record. It could be LOOKUP if at all.
   If it is NONE, then the layout is decoded from its representation received
   over the network.
*/
int c2_layout_decode(struct c2_ldb_schema *schema, uint64_t lid,
		     struct c2_bufvec_cursor *cur,
		     enum c2_layout_xcode_op op,
		     struct c2_db_tx *tx,
		     struct c2_layout **out)
{
	struct c2_layout_type *lt;
	struct c2_ldb_rec     *rec;

	C2_PRE(schema != NULL);
	C2_PRE(lid != LID_NONE);
	C2_PRE(cur != NULL);
	C2_PRE(op == C2_LXO_DB_LOOKUP || op == C2_LXO_DB_NONE);
	if (op == C2_LXO_DB_LOOKUP)
		C2_PRE(tx != NULL);

	rec = (struct c2_ldb_rec *)c2_bufvec_cursor_addr(cur);

	if (!IS_IN_ARRAY(rec->lr_lt_id, schema->ls_type))
		return -EPROTO;

	lt = schema->ls_type[rec->lr_lt_id];
	if (lt == NULL)
		return -ENOENT;

	/** Move the cursor to point to rec->lr_data. */
	c2_bufvec_cursor_move(cur, sizeof(struct c2_ldb_rec));

	lt->lt_ops->lto_decode(schema, lid, cur, op, tx, out);

	c2_mutex_lock(&(*out)->l_lock);

	(*out)->l_id = lid;
	(*out)->l_type = lt;
	(*out)->l_ref = rec->lr_ref_count;

	c2_mutex_unlock(&(*out)->l_lock);

	return 0;
}

/**
   This method uses an in-memory layout object and
   @li Either adds/updates/deletes it to/from the Layout DB
   @li Or converts it to a buffer that can be passed on over the network.

   Two use cases of c2_layout_encode()
   - Server encodes an in-memory layout object into a buffer, so as to send
     it to the client.
   - Server encodes an in-memory layout object and stores it into the Layout
     DB.

   @param op - This enum parameter indicates what is the DB operation to be
   performed on the layout record if at all and it could be one of
   ADD/UPDATE/DELETE. If it is NONE, then the layout is stored in the buffer.

*/
int c2_layout_encode(struct c2_ldb_schema *schema,
		     struct c2_layout *l,
		     enum c2_layout_xcode_op op,
		     struct c2_db_tx *tx,
		     struct c2_bufvec_cursor *out)
{
	struct c2_ldb_rec     *rec;
	struct c2_layout_type *lt;

	C2_PRE(schema != NULL);
	C2_PRE(l != NULL); /** @todo This will be replaced by the invariant. */
	C2_PRE(op == C2_LXO_DB_ADD ||
		op == C2_LXO_DB_UPDATE ||
		op == C2_LXO_DB_DELETE ||
		op == C2_LXO_DB_NONE);
	if (op != C2_LXO_DB_NONE)
		C2_PRE(tx != NULL);
	C2_PRE(out != NULL);

	C2_ALLOC_PTR(rec);

	c2_mutex_lock(&l->l_lock);

	rec->lr_lt_id     = l->l_type->lt_id;
	rec->lr_ref_count = l->l_ref;

	/** Copy rec to the buffer pointed by out. */
	data_to_bufvec_copy(out, rec, sizeof(struct c2_ldb_rec));

	lt = schema->ls_type[rec->lr_lt_id];
	if (lt == NULL)
		return -ENOENT;

	lt->lt_ops->lto_encode(schema, l, op, tx, out);

	c2_mutex_unlock(&l->l_lock);

	return 0;
}


int ldb_layout_write(struct c2_ldb_schema *schema,
		     enum c2_layout_xcode_op op,
		     uint32_t recsize,
		     struct c2_bufvec_cursor *cur,
		     struct c2_db_tx *tx)
{
	uint64_t              lid;
	struct c2_layout_rec *rec;
	struct c2_db_pair     pair;

   /**	@todo Collect data into lid and rec, from the buffer pointed by cur and
	by referring the recsize. Till then initializing rec to NULL to avoid
        uninitialization error. */
	rec = NULL;

	c2_db_pair_setup(&pair, &schema->ls_layouts,
			 &lid, sizeof(uint64_t),
			 rec, recsize);

	if (op == C2_LXO_DB_ADD) {
		c2_table_insert(tx, &pair);
	} else if (op == C2_LXO_DB_UPDATE) {
		c2_table_update(tx, &pair);
	} else if (op == C2_LXO_DB_DELETE) {
		c2_table_delete(tx, &pair);
	}
	return 0;
}


/**
 * Copied verbatim from bufvec_xcode.c, need to see how to refactor it.
 * Initializes a c2_bufvec containing a single element of specified size.
 */
void data_to_bufvec(struct c2_bufvec *src_buf, void **data,
			   size_t *len)
{
	C2_PRE(src_buf != NULL);
	C2_PRE(len != 0);
	C2_PRE(data != NULL);

	src_buf->ov_vec.v_nr = 1;
	src_buf->ov_vec.v_count = (c2_bcount_t *)len;
	src_buf->ov_buf = data;
}

/**
 * Copied verbatim from bufvec_xcode.c, need to see how to refactor it.
 * Helper functions to copy opaque data with specified size to and from a
 * c2_bufvec.
 */
int data_to_bufvec_copy(struct c2_bufvec_cursor *cur, void *data,
			       size_t len)
{
	c2_bcount_t		 count;
	struct c2_bufvec_cursor  src_cur;
	struct c2_bufvec	 src_buf;

	C2_PRE(cur != NULL);
	C2_PRE(data != NULL);
	C2_PRE(len != 0);

	data_to_bufvec(&src_buf, &data, &len);
	c2_bufvec_cursor_init(&src_cur, &src_buf);
	count = c2_bufvec_cursor_copy(cur, &src_cur, len);
	if (count != len)
		return -EFAULT;
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
