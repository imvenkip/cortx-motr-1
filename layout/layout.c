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
#include "lib/vec.h"   /* c2_bufvec_cursor_step(), c2_bufvec_cursor_addr() */

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_LAYOUT
#include "lib/trace.h"

#include "pool/pool.h"         /* c2_pool_id_is_valid() */
#include "layout/layout_db.h"
#include "layout/layout.h"

extern int layout_type_verify(const struct c2_ldb_schema *schema,
			      uint32_t lt_id);

enum layout_internal {
	ENUM_LID_NONE = 0
};
int LID_NONE = ENUM_LID_NONE;

/** ADDB instrumentation for layout. */
static const struct c2_addb_ctx_type layout_addb_ctx_type = {
	.act_name = "layout"
};

const struct c2_addb_loc layout_addb_loc = {
	.al_name = "layout"
};

struct c2_addb_ctx layout_global_ctx = {
	.ac_type   = &layout_addb_ctx_type,
	/* todo What should this parent be set to? Something coming from FOP? */
	.ac_parent = NULL
};

C2_ADDB_EV_DEFINE(layout_decode_success, "layout_decode_success",
		  C2_ADDB_EVENT_LAYOUT_DECODE_SUCCESS, C2_ADDB_FLAG);
C2_ADDB_EV_DEFINE(layout_decode_fail, "layout_decode_fail",
		  C2_ADDB_EVENT_LAYOUT_DECODE_FAIL, C2_ADDB_FUNC_CALL);
C2_ADDB_EV_DEFINE(layout_encode_success, "layout_encode_success",
		  C2_ADDB_EVENT_LAYOUT_ENCODE_SUCCESS, C2_ADDB_FLAG);
C2_ADDB_EV_DEFINE(layout_encode_fail, "layout_encode_fail",
		  C2_ADDB_EVENT_LAYOUT_ENCODE_FAIL, C2_ADDB_FUNC_CALL);

bool layout_invariant(const struct c2_layout *l)
{
	return l != NULL && l->l_id != LID_NONE && l->l_type != NULL &&
		l->l_ops != NULL;
}

bool striped_layout_invariant(const struct c2_layout_striped *stl)
{
	return stl != NULL && stl->ls_enum != NULL &&
		layout_invariant(&stl->ls_base);
}

bool enum_invariant(const struct c2_layout_enum *le, uint64_t lid)
{
	return le != NULL && le->le_lid == lid && le->le_ops != NULL;
}

int c2_layouts_init(void)
{
	/* todo Should this fn c2_layouts_init() be used to perform the
	 * following:
	 * c2_ldb_schema_init(schema, dbenv);
	 * Register pdclust and composite layout types.
	 * Register list and linear enum types.
	 *
	 * If yes, how to obtain values of schema and dbenv parameters?
	 * And if yes, c2_layouts_fini() should do the opposite of
	 * c2_layouts_init().
	 */
	return 0;
}

void c2_layouts_fini(void)
{
}

int c2_layout_init(struct c2_layout *l,
		   uint64_t lid,
		   uint64_t pool_id,
		   const struct c2_layout_type *type,
		   const struct c2_layout_ops *ops)
{
	C2_PRE(l != NULL);
	C2_PRE(lid != LID_NONE);
	C2_PRE(c2_pool_id_is_valid(pool_id));
	C2_PRE(type != NULL);
	C2_PRE(ops != NULL);

	C2_ENTRY("lid %llu, Layout_type_id %lu", (unsigned long long)lid,
		 (unsigned long)type->lt_id);

	l->l_id      = lid;
	l->l_type    = type;
	l->l_ref     = 0;
	l->l_pool_id = pool_id;
	l->l_ops     = ops;

	c2_mutex_init(&l->l_lock);
	c2_addb_ctx_init(&l->l_addb, &layout_addb_ctx_type,
			 &layout_global_ctx);

	C2_POST(layout_invariant(l));
	C2_LEAVE("lid %llu", (unsigned long long)lid);
	return 0;
}

void c2_layout_fini(struct c2_layout *l)
{
	C2_PRE(layout_invariant(l));

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);

	c2_addb_ctx_fini(&l->l_addb);
	c2_mutex_fini(&l->l_lock);

	C2_LEAVE("lid %llu", (unsigned long long)l->l_id);
}

int c2_layout_striped_init(struct c2_layout_striped *str_l,
			   struct c2_layout_enum *e,
			   uint64_t lid, uint64_t pool_id,
			   const struct c2_layout_type *type,
			   const struct c2_layout_ops *ops)
{
	C2_PRE(str_l != NULL);
	C2_PRE(e != NULL);
	C2_PRE(c2_pool_id_is_valid(pool_id));
	C2_PRE(lid != LID_NONE);
	C2_PRE(type != NULL);
	C2_PRE(ops != NULL);

	C2_ENTRY("lid %llu, Enum_type %s", (unsigned long long)lid,
		 e->le_type->let_name);

	c2_layout_init(&str_l->ls_base, lid, pool_id, type, ops);

	str_l->ls_enum = e;

	C2_POST(striped_layout_invariant(str_l));

	C2_LEAVE("lid %llu", (unsigned long long)lid);
	return 0;
}

/**
 * @post The enum object which is part of striped layout object, is finalized
 * as well.
 */
void c2_layout_striped_fini(struct c2_layout_striped *str_l)
{
	C2_PRE(striped_layout_invariant(str_l));

	C2_ENTRY("lid %llu", (unsigned long long)str_l->ls_base.l_id);

	c2_layout_fini(&str_l->ls_base);

	str_l->ls_enum->le_ops->leo_fini(str_l->ls_enum,
					 str_l->ls_base.l_id);

	C2_LEAVE("lid %llu", (unsigned long long)str_l->ls_base.l_id);
}

int c2_layout_enum_init(struct c2_layout_enum *le, uint64_t lid,
			const struct c2_layout_enum_type *et,
			const struct c2_layout_enum_ops *ops)
{
	C2_PRE(le != NULL);
	C2_PRE(lid != LID_NONE);
	C2_PRE(et != NULL);
	C2_PRE(ops != NULL);

	C2_ENTRY("Enum_type_id %lu", (unsigned long)et->let_id);

	le->le_type = et;
	le->le_lid  = lid;
	le->le_ops  = ops;

	C2_LEAVE("Enum_type_id %lu", (unsigned long)et->let_id);
	return 0;
}

void c2_layout_enum_fini(struct c2_layout_enum *le)
{
	C2_ENTRY("Enum_type %s", le->le_type->let_name);
	C2_LEAVE("Enum_type %s", le->le_type->let_name);
}

/** Adds a reference to the layout. */
void c2_layout_get(struct c2_layout *l)
{
	C2_PRE(layout_invariant(l));

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);

	c2_mutex_lock(&l->l_lock);
	l->l_ref++;
	c2_mutex_unlock(&l->l_lock);

	C2_LEAVE("lid %llu", (unsigned long long)l->l_id);
}

/** Releases a reference on the layout. */
void c2_layout_put(struct c2_layout *l)
{
	C2_PRE(layout_invariant(l));

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);

	c2_mutex_lock(&l->l_lock);
	l->l_ref--;
	c2_mutex_unlock(&l->l_lock);

	C2_LEAVE("lid %llu", (unsigned long long)l->l_id);
}

/**
 * This method
 * @li Either continues to build an in-memory layout object from its
 *     representation 'stored in the Layout DB'
 * @li Or builds an in-memory layout object from its representation 'received
 *     over the network'.
 *
 * Two use cases of c2_layout_decode()
 * - Server decodes an on-disk layout record by reading it from the Layout
 *   DB, into an in-memory layout structure, using c2_ldb_lookup() which
 *   internally calls c2_layout_decode().
 * - Client decodes a buffer received over the network, into an in-memory
 *   layout structure, using c2_layout_decode().
 *
 * @param op This enum parameter indicates what is the DB operation to be
 * performed on the layout record. It could be LOOKUP if at all. If it is NONE,
 * then the layout is decoded from its representation received over the
 * network.
 *
 * @pre
 * - In case c2_layout_decode() is called through c2_ldb_add(), then the
 *   buffer should be containing all the data that is read specifically from
 *   the layouts table. It means its size will be at the most the size
 *   returned by c2_ldb_rec_max_size().
 * - In case c2_layout_decode() is called by some other caller, then the
 *   buffer should be containing all the data belonging to the specific layout.
 *   It may include data that spans over tables other than layouts as well. It
 *   means its size may even be more than the one returned by
 *   c2_ldb_rec_max_size().
 *
 * @post Layout object is built internally (along with enumeration object being
 * built if applicable). Hence, user needs to finalize the layout object when
 * done with the use. It can be accomplished by performing l->l_ops->lo_fini(l).
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
	C2_PRE(op == C2_LXO_DB_LOOKUP || op == C2_LXO_DB_NONE);
	C2_PRE(ergo(op == C2_LXO_DB_LOOKUP, tx != NULL));
	C2_PRE(out != NULL && *out == NULL);

	C2_ENTRY("lid %llu", (unsigned long long)lid);

	if (op == C2_LXO_DB_NONE)
		c2_mutex_lock(&schema->ls_lock);
	else /* It is locked by c2_ldb_lookup(). */
		C2_ASSERT(c2_mutex_is_locked(&schema->ls_lock));

	/* Check if the buffer is with sufficient size. */
	if (c2_bufvec_cursor_step(cur) < sizeof *rec) {
		rc = -ENOBUFS;
		if (op == C2_LXO_DB_NONE)
			C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
				    layout_decode_fail, "buffer insufficient",
				    -ENOBUFS);

		C2_LOG("c2_layout_decode(): lid %llu, buffer with "
		       "insufficient size", (unsigned long long)lid);
		goto out;
	}

	/* rec can not be NULL since the buffer size is already verified. */
	rec = c2_bufvec_cursor_addr(cur);

	rc = layout_type_verify(schema, rec->lr_lt_id);
	if (rc != 0) {
		if (op == C2_LXO_DB_NONE)
			C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
				    layout_decode_fail,
				    "Unqualified Layout_type_id", rc);

		C2_LOG("c2_layout_decode(): lid %llu, Unqualified "
		       "Layout_type_id %lu, rc %d",
		       (unsigned long long)lid, (unsigned long)rec->lr_lt_id,
		       rc);
		goto out;
	}

	lt = schema->ls_type[rec->lr_lt_id];

	if (!c2_pool_id_is_valid(rec->lr_pid)) {
		rc = -EINVAL;
		if (op == C2_LXO_DB_NONE)
			C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
				    layout_decode_fail, "Invalid pool id",
				    -EINVAL);

		C2_LOG("c2_layout_decode(): lid %llu, Invalid pool id,"
	               " Pool_id %lu", (unsigned long long)lid,
		       (unsigned long)rec->lr_pid);
		goto out;
	}

	/* Move the cursor to point to the layout type specific payload. */
	c2_bufvec_cursor_move(cur, sizeof *rec);

	/*
	 * It is fine if any of the layout does not contain any data in
	 * rec->lr_data[], unless it is required by the specific layout type,
	 * which will be caught by the respective lto_decode() implementation.
	 * Hence, ignoring the return status of c2_bufvec_cursor_move() here.
	 */

	rc = lt->lt_ops->lto_decode(schema, lid, rec->lr_pid, cur, op, tx, out);
	if (rc != 0) {
		if (op == C2_LXO_DB_NONE)
			C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
				    layout_decode_fail, "lto_decode", rc);

		C2_LOG("c2_layout_decode(): lid %llu, lto_decode() failed, "
		       "rc %d", (unsigned long long)lid, rc);
		goto out;
	}

	c2_mutex_lock(&(*out)->l_lock);

	(*out)->l_id      = lid;
	(*out)->l_type    = lt;
	(*out)->l_ref     = rec->lr_ref_count;
	(*out)->l_pool_id = rec->lr_pid;

	c2_mutex_unlock(&(*out)->l_lock);

	C2_POST(layout_invariant(*out));

out:
	if (op == C2_LXO_DB_NONE)
		c2_mutex_unlock(&schema->ls_lock);

	if (op == C2_LXO_DB_NONE && rc == 0)
		C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
			    layout_decode_success, true);

	C2_LEAVE("lid %llu, rc %d", (unsigned long long)lid, rc);
	return rc;
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
 * @param op This enum parameter indicates what is the DB operation to be
 * performed on the layout record if at all and it could be one of
 * ADD/UPDATE/DELETE. If it is NONE, then the layout is stored in the buffer.
 *
 * @param oldrec_cur Cursor pointing to a buffer to be used to read the
 * exisiting layout record from the layouts table. Applicable only in case of
 * layou update operation. In other cases, it is expected to be NULL.
 *
 * @param out Cursor poining to a buffer. Regarding the size of the buufer:
 * - In case c2_layout_decode() is called through c2_ldb_add()|c2_ldb_update()|
 *   c2_ldb_delete(), then the buffer should be capable of containing the data
 *   that is to be written specifically to the layouts table. It means its size
 *   will be at the most the size returned by c2_ldb_rec_max_size().
 * - In case c2_layout_decode() is called by some other caller, then the
 *   buffer size should be capable of incorporating all the data belonging to
 *   the specific layout. It means its size may even be more than the one
 *   returned by c2_ldb_rec_max_size().
 *
 * @post
 * - If op is is either for ADD|UPDATE|DELETE, respective DB operation is
 *   continued.
 * - If op is NONE, the buffer contains the serialized representation of the
 *   whole layout.
 * - If the buffer size is found to be insufficient, then the error ENOBUFS is
 *   returned.
 */
int c2_layout_encode(struct c2_ldb_schema *schema,
		     struct c2_layout *l,
		     enum c2_layout_xcode_op op,
		     struct c2_db_tx *tx,
		     struct c2_bufvec_cursor *oldrec_cur,
		     struct c2_bufvec_cursor *out)
{
	struct c2_ldb_rec      rec;
	struct c2_ldb_rec     *oldrec;
	struct c2_layout_type *lt;
	c2_bcount_t            nbytes;
	int                    rc;

	C2_PRE(schema != NULL);
	C2_PRE(layout_invariant(l));
	C2_PRE(op == C2_LXO_DB_ADD || op == C2_LXO_DB_UPDATE ||
	       op == C2_LXO_DB_DELETE || op == C2_LXO_DB_NONE);
	C2_PRE(ergo(op != C2_LXO_DB_NONE, tx != NULL));
	C2_PRE(ergo(op == C2_LXO_DB_UPDATE, oldrec_cur != NULL));
	C2_PRE(out != NULL);

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);

	if (op == C2_LXO_DB_NONE)
		c2_mutex_lock(&schema->ls_lock);
	else /* It is locked by c2_ldb_[add|delete|update](), as applicable. */
		C2_ASSERT(c2_mutex_is_locked(&schema->ls_lock));

	c2_mutex_lock(&l->l_lock);

	/* Check if the buffer is with sufficient size. */
	if (c2_bufvec_cursor_step(out) < sizeof rec) {
		rc = -ENOBUFS;
		if (op == C2_LXO_DB_NONE)
			C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
				    layout_encode_fail, "buffer insufficient",
				    -ENOBUFS);

		C2_LOG("c2_layout_encode(): lid %llu, buffer with insufficient"
		       " size", (unsigned long long)l->l_id);
		goto out;
	}

	/* Check if the buffer for old record is with sufficient size. */
	if (!ergo(op == C2_LXO_DB_UPDATE,
	          c2_bufvec_cursor_step(oldrec_cur) >= sizeof *oldrec)) {
		rc = -ENOBUFS;
		if (op == C2_LXO_DB_NONE)
			C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
				    layout_encode_fail, "buffer insufficient",
				    -ENOBUFS);

		C2_LOG("c2_layout_encode(): lid %llu, buffer for old record "
		       "with insufficient size", (unsigned long long)l->l_id);
		goto out;
	}

	rc = layout_type_verify(schema, l->l_type->lt_id);
	if (rc != 0) {
		if (op == C2_LXO_DB_NONE)
			C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
				    layout_encode_fail,
				    "Unqualified Layout_type_id", rc);

		C2_LOG("c2_layout_encode(): lid %llu, Unqualified "
		       "Layout_type_id %lu, rc %d",
		       (unsigned long long)l->l_id,
		       (unsigned long)l->l_type->lt_id,
		       rc);
		goto out;
	}

	lt = schema->ls_type[l->l_type->lt_id];

	if (!c2_pool_id_is_valid(l->l_pool_id)) {
		rc = -EINVAL;
		if (op == C2_LXO_DB_NONE)
			C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
				    layout_encode_fail, "Invalid pool id",
				    -EINVAL);

		C2_LOG("c2_layout_encode(): lid %llu, Invalid pool id,"
	               " Pool_id %lu", (unsigned long long)l->l_id,
		       (unsigned long)l->l_pool_id);
		goto out;
	}

	if (op == C2_LXO_DB_UPDATE) {
		/*
		 * Processing the oldrec_cur, to verify that the layout
		 * type and pool id have not changed and then to make it
		 * point to the layout type specific payload.
		 */
		oldrec = c2_bufvec_cursor_addr(oldrec_cur);
		C2_ASSERT(oldrec != NULL);
		if (oldrec->lr_lt_id != l->l_type->lt_id ||
		    oldrec->lr_pid != l->l_pool_id) {
			rc = -EINVAL;
			C2_LOG("c2_layout_encode(): lid %llu, New values "
			       "do not match the old ones",
			       (unsigned long long)l->l_id);
			goto out;
		}
		c2_bufvec_cursor_move(oldrec_cur, sizeof *oldrec);
	}

	rec.lr_lt_id     = l->l_type->lt_id;
	rec.lr_ref_count = l->l_ref;
	rec.lr_pid       = l->l_pool_id;

	nbytes = c2_bufvec_cursor_copyto(out, &rec, sizeof rec);
	C2_ASSERT(nbytes == sizeof rec);

	rc = lt->lt_ops->lto_encode(schema, l, op, tx, oldrec_cur, out);
	if (rc != 0) {
		if (op == C2_LXO_DB_NONE)
			C2_ADDB_ADD(&l->l_addb, &layout_addb_loc,
				    layout_encode_fail, "lto_encode", rc);

		C2_LOG("c2_layout_encode(): lid %llu, lto_encode() failed, "
		       "rc %d", (unsigned long long)l->l_id, rc);
	}

out:
	c2_mutex_unlock(&l->l_lock);

	if (op == C2_LXO_DB_NONE)
		c2_mutex_unlock(&schema->ls_lock);

	if (op == C2_LXO_DB_NONE && rc == 0)
		C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
			    layout_encode_success, true);

	C2_LEAVE("lid %llu, rc %d", (unsigned long long)l->l_id, rc);
	return rc;
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
