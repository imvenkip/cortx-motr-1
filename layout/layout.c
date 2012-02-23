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

/**
 * @addtogroup layout
 * @{
 */

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/vec.h"
#include "lib/misc.h"
#include "lib/trace.h"

#include "layout/layout_db.h"
#include "layout/layout_internal.h"
#include "layout/layout.h"

/**
 * Initializes layout schema, creates generic table to store layout records.
 * Registers all the layout types amd enum types by creating layout type and
 * enum type specific tables, if they do not exist already.
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

void c2_layout_init(struct c2_layout *l,
		    uint64_t lid,
		    const struct c2_layout_type *type,
		    const struct c2_layout_ops *ops)
{
	C2_PRE(l != NULL);
	C2_PRE(lid != LID_NONE);
	C2_PRE(type != NULL);
	C2_PRE(ops != NULL);

	C2_SET0(l);

	c2_mutex_init(&l->l_lock);

	l->l_id     = lid;
	l->l_type   = type;
	l->l_ops    = ops;
}

void c2_layout_fini(struct c2_layout *l)
{
	C2_PRE(layout_invariant(l));

	c2_mutex_fini(&l->l_lock);
}

void c2_layout_striped_init(struct c2_layout_striped *str_l,
			    struct c2_layout_enum *e,
			    uint64_t lid,
			    const struct c2_layout_type *type,
			    const struct c2_layout_ops *ops)
{
	C2_PRE(str_l != NULL);
	C2_PRE(e != NULL);
	C2_PRE(lid != LID_NONE);
	C2_PRE(type != NULL);
	C2_PRE(ops != NULL);

	C2_SET0(str_l);

	c2_layout_init(&str_l->ls_base, lid, type, ops);

	str_l->ls_enum = e;
}

void c2_layout_striped_fini(struct c2_layout_striped *str_l)
{
	C2_PRE(str_l != NULL);

	c2_layout_fini(&str_l->ls_base);
}

void c2_layout_enum_init(struct c2_layout_enum *le,
			 const struct c2_layout_enum_type *et,
			 const struct c2_layout_enum_ops *ops)
{
	C2_PRE(le != NULL);
	C2_PRE(et != NULL);
	C2_PRE(ops != NULL);

	le->le_type = et;
	le->le_ops  = ops;
}

void c2_layout_enum_fini(struct c2_layout_enum *le)
{
}

/** Adds a reference to the layout. */
void c2_layout_get(struct c2_layout *l)
{
	C2_PRE(l != NULL);

	c2_mutex_lock(&l->l_lock);
	l->l_ref++;
	c2_mutex_unlock(&l->l_lock);
}

/** Releases a reference on the layout. */
void c2_layout_put(struct c2_layout *l)
{
	C2_PRE(l != NULL);

	c2_mutex_lock(&l->l_lock);
	l->l_ref--;
	c2_mutex_unlock(&l->l_lock);
}

/**
 * This method
 * @li Either continues to builds an in-memory layout object from its
 *     representation 'stored in the Layout DB'
 * @li Or builds an in-memory layout object from its representation 'received
 *     over the network'.
 *
 * Two use cases of c2_layout_decode()
 * - Server decodes an on-disk layout record by reading it from the Layout
 *   DB, into an in-memory layout structure, using c2_ldb_rec_lookup() which
 *   internally calls c2_layout_decode().
 * - Client decodes a buffer received over the network, into an in-memory
 *   layout structure, using c2_layout_decode().
 *
 * @param op - This enum parameter indicates what is the DB operation to be
 * performed on the layout record. It could be LOOKUP if at all. If it is NONE,
 * then the layout is decoded from its representation received over the
 * network.
 *
 * @pre
 * - In case c2_layout_decode() is called through c2_ldb_add(), then the
 *   buffer should be containing all the data that is read from the layouts
 *   table. It means it will be with size less than or equal to the one
 *   returned by c2_ldb_rec_max_size().
 * - In case c2_layout_decode() is called by some other caller, then the
 *   buffer should be containing all the data belonging to the specific layout.
 */
int c2_layout_decode(struct c2_ldb_schema *schema, uint64_t lid,
		     struct c2_bufvec_cursor *cur,
		     enum c2_layout_xcode_op op,
		     struct c2_db_tx *tx,
		     struct c2_layout **out)
{
	struct c2_layout_type *lt;
	struct c2_ldb_rec     *rec;
	int                    rc;

	C2_PRE(schema != NULL);
	C2_PRE(lid != LID_NONE);
	C2_PRE(cur != NULL);
	C2_PRE(!c2_bufvec_cursor_move(cur, 0));
	C2_PRE(op == C2_LXO_DB_LOOKUP || op == C2_LXO_DB_NONE);
	C2_PRE(ergo(op == C2_LXO_DB_LOOKUP, tx != NULL));

	C2_LOG("c2_layout_decode(): lid %llu\n", (unsigned long long)lid);

	rec = c2_bufvec_cursor_addr(cur);
	C2_ASSERT(rec != NULL);
	if (rec == NULL)
		return -EPROTO;

	if (!IS_IN_ARRAY(rec->lr_lt_id, schema->ls_type))
		return -EPROTO;

	lt = schema->ls_type[rec->lr_lt_id];
	if (lt == NULL)
		return -ENOENT;

	/* Move the cursor to point to the layout type specific payload. */
	c2_bufvec_cursor_move(cur, sizeof *rec);

	/*
	 * It is fine if any of the layout does not contain any data in
	 * rec->lr_data[], unless it is required by the specific layout type,
	 * which will be caught by the respective lto_decode() implementation.
	 * Hence, ignoring the return status of c2_bufvec_cursor_move() here.
	 */
	/* todo Add TC to verify the functioning when no type specific
	 * data is present even when expected.
	 * todo Check if cur is NULL in that case.
	 */

	rc = lt->lt_ops->lto_decode(schema, lid, cur, op, tx, out);
	if (rc != 0)
		return rc;

	c2_mutex_lock(&(*out)->l_lock);

	(*out)->l_id = lid;
	(*out)->l_type = lt;
	(*out)->l_ref = rec->lr_ref_count;

	c2_mutex_unlock(&(*out)->l_lock);

	return 0;
}

/**
 * This method uses an in-memory layout object and
 * @li Either adds/updates/deletes it to/from the Layout DB
 * @li Or converts it to a buffer that can be passed on over the network.
 *
 * Two use cases of c2_layout_encode()
 * - Server encodes an in-memory layout object into a buffer, so as to send
 *   it to the client.
 * - Server encodes an in-memory layout object and stores it into the Layout
 *   DB.
 *
 * @param op - This enum parameter indicates what is the DB operation to be
 * performed on the layout record if at all and it could be one of
 * ADD/UPDATE/DELETE. If it is NONE, then the layout is stored in the buffer.
 *
 * @pre
 * - In case c2_layout_decode() is called through c2_ldb_add(), then the
 * buffer should be capable of ontaining all the data that is to be read from
 * the layouts table.
 * - In case c2_layout_decode() is called by some other caller, then the
 * buffer size should be capable of incorporating all data belonging to the
 * specific layout.
 *
 * @post If the buffer is found to be insufficient, then the error ENOBUFS is
 * returned.
 */
int c2_layout_encode(struct c2_ldb_schema *schema,
		     struct c2_layout *l,
		     enum c2_layout_xcode_op op,
		     struct c2_db_tx *tx,
		     struct c2_bufvec_cursor *oldrec_cur,
		     struct c2_bufvec_cursor *out)
{
	struct c2_ldb_rec     *rec;
	struct c2_ldb_rec     *oldrec;
	struct c2_layout_type *lt;
	int                    rc;
	c2_bcount_t            nbytes_copied;

	C2_PRE(schema != NULL);
	C2_PRE(layout_invariant(l));
	C2_PRE(op == C2_LXO_DB_ADD || op == C2_LXO_DB_UPDATE ||
		op == C2_LXO_DB_DELETE || op == C2_LXO_DB_NONE);
	C2_PRE(ergo(op != C2_LXO_DB_NONE, tx != NULL));
	C2_PRE(ergo(op == C2_LXO_DB_UPDATE, oldrec_cur != NULL));
	C2_PRE(ergo(op == C2_LXO_DB_UPDATE,
	      !c2_bufvec_cursor_move(oldrec_cur, 0)));
	C2_PRE(out != NULL);

	C2_LOG("In c2_layout_encode()\n");

	c2_mutex_lock(&l->l_lock);

	if(!IS_IN_ARRAY(l->l_type->lt_id, schema->ls_type))
		return -ENOENT;

	if(!IS_IN_ARRAY(l->l_type->lt_id, schema->ls_type))
		return -ENOENT;

	lt = schema->ls_type[l->l_type->lt_id];
	if (lt == NULL)
		return -ENOENT;

	if (op == C2_LXO_DB_UPDATE) {
		/*
		 * Processing the oldrec_cur, to verify that the layout
		 * type has not changed and then to make it point it to the
		 * layout type specific payload.
		 */
		oldrec = c2_bufvec_cursor_addr(oldrec_cur);
		C2_ASSERT(oldrec != NULL);
		if (oldrec->lr_lt_id != l->l_type->lt_id)
			return -EINVAL;
		c2_bufvec_cursor_move(oldrec_cur, sizeof *oldrec);
	}

	C2_ALLOC_PTR(rec);
	if (rec == NULL) {
		c2_mutex_unlock(&l->l_lock);
		return -ENOMEM;
	}

	rec->lr_lt_id     = l->l_type->lt_id;
	rec->lr_ref_count = l->l_ref;

	nbytes_copied = c2_bufvec_cursor_copyto(out, rec, sizeof *rec);
	C2_ASSERT(nbytes_copied == sizeof *rec);

	rc = lt->lt_ops->lto_encode(schema, l, op, tx, oldrec_cur, out);

	c2_mutex_unlock(&l->l_lock);

	return rc;
}

bool layout_invariant(const struct c2_layout *l)
{
	if (l == NULL || l->l_id == LID_NONE || l->l_type == NULL ||
			 l->l_ops == NULL)
		return false;

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
