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
#include "lib/misc.h"

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
	C2_SET0(lay);

	c2_mutex_init(&lay->l_lock);

	lay->l_id       = id;
	lay->l_type     = type;
	lay->l_ops      = ops;
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

void c2_layout_striped_init(struct c2_layout_striped *str_lay,
			    struct c2_layout_enum *e,
			    uint64_t lid,
			    const struct c2_layout_type *type,
			    const struct c2_layout_ops *ops)
{
	C2_SET0(str_lay);

	c2_layout_init(&str_lay->ls_base, lid, type, ops);

	str_lay->ls_enum = e;
}

void c2_layout_striped_fini(struct c2_layout_striped *str_lay)
{
	c2_layout_fini(&str_lay->ls_base);
}

void c2_layout_enum_init(struct c2_layout_enum *le,
			 struct c2_layout *l,
			 struct c2_layout_enum_type *lt,
			 struct c2_layout_enum_ops *ops)
{
   /**
	@code
	le->le_l    = l;
	le_le_type  = lt;
	le->le_ops  = ops;
	@endcode
    */
}

void c2_layout_enum_fini(struct c2_layout_enum *le)
{
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
   - Server decodes an on-disk layout record by reading it from the Layout
     DB, into an in-memory layout structure, using c2_ldb_rec_lookup() which
     internally calls c2_layout_decode().
   - Client decodes a buffer received over the network, into an in-memory
     layout structure, using c2_layout_decode().

   @param op - This enum parameter indicates what is the DB operation to be
   performed on the layout record. It could be LOOKUP if at all. If it is NONE,
   then the layout is decoded from its representation received over the
   network.

   @pre
   - In case c2_layout_decode() is called through c2_ldb_add(), then the
   buffer should be containing all the data that is read from the layouts table.
   It means it will be with size less than or equal to the one returned by
   c2_ldb_rec_max_size().
   - In case c2_layout_decode() is called by some other caller, then the
   buffer should be containing all the data belonging to the specific layout.
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
	C2_PRE(ergo(op == C2_LXO_DB_LOOKUP, tx != NULL));

	rec = c2_bufvec_cursor_addr(cur);

	if (!IS_IN_ARRAY(rec->lr_lt_id, schema->ls_type))
		return -EPROTO;

	lt = schema->ls_type[rec->lr_lt_id];
	if (lt == NULL)
		return -ENOENT;

	/** Move the cursor to point to rec->lr_data[] */
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

   @pre
   - In case c2_layout_decode() is called through c2_ldb_add(), then the
   buffer should be capable of ontaining all the data that is to be read from
   the layouts table.
   - In case c2_layout_decode() is called by some other caller, then the
   buffer size should be capable of incorporating all data belonging to the
   specific layout.

   @post If the buffer is found to be insufficient, then the error ENOBUFS is
   returned.
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
	C2_PRE(layout_invariant(l));
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
	c2_bufvec_cursor_copyto(out, rec, sizeof(struct c2_ldb_rec));

	lt = schema->ls_type[rec->lr_lt_id];
	if (lt == NULL)
		return -ENOENT;

	lt->lt_ops->lto_encode(schema, l, op, tx, out);

	c2_mutex_unlock(&l->l_lock);

	return 0;
}

bool layout_invariant(const struct c2_layout *l)
{
	/* Verify the members are sane. */
	return true;
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
