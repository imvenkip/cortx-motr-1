/* -*- C -*- */
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
 * Original author: Trupti Patil <trupti_patil@xyratex.com>
 * Original creation date: 11/16/2011
 */

/**
 * @addtogroup linear_enum
 *
 * @{
 */

#include "lib/errno.h"
#include "lib/tlist.h"	/* struct c2_tl */
#include "lib/vec.h"    /* c2_bufvec_cursor_step(), c2_bufvec_cursor_addr() */
#include "lib/memory.h" /* C2_ALLOC_PTR() */
#include "lib/bob.h"

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_LAYOUT
#include "lib/trace.h"

#include "fid/fid.h"    /* c2_fid_set(), c2_fid_is_valid() */
#include "layout/layout_internal.h"
#include "layout/linear_enum.h"

extern bool layout_invariant(const struct c2_layout *l);
extern bool enum_invariant(const struct c2_layout_enum *le, uint64_t lid);
extern const struct c2_addb_loc layout_addb_loc;
extern struct c2_addb_ctx layout_global_ctx;

enum {
	LINEAR_ENUM_MAGIC = 0x4C494E2D454E554DULL, /* LIN-ENUM */
	LINEAR_NR_NONE    = 0
};

static const struct c2_bob_type linear_enum_bob = {
	.bt_name         = "linear_enum",
	.bt_magix_offset = offsetof(struct c2_layout_linear_enum, lla_magic),
	.bt_magix        = LINEAR_ENUM_MAGIC,
	.bt_check        = NULL
};

C2_BOB_DEFINE(static, &linear_enum_bob, c2_layout_linear_enum);

bool c2_linear_enum_invariant(const struct c2_layout_linear_enum *lin_enum,
			      uint64_t lid)
{
	return lin_enum != NULL &&
		lin_enum->lle_attr.lla_nr != LINEAR_NR_NONE &&
		lin_enum->lle_attr.lla_B != 0 &&
		c2_layout_linear_enum_bob_check(lin_enum) &&
		enum_invariant(&lin_enum->lle_base, lid);
}

static const struct c2_layout_enum_ops linear_enum_ops;

/**
 * Build linear enumeration object.
 * @note Enum object need not be finalized explicitly by the user. It is
 * finalized internally through the layout finalization routine to be invoked
 * as l->l_ops->lo_fini().
 */
int c2_linear_enum_build(uint64_t lid, uint32_t nr, uint32_t A, uint32_t B,
			 struct c2_layout_linear_enum **out)
{
	struct c2_layout_linear_enum *lin_enum;
	int                           rc = 0;

	C2_PRE(out != NULL && *out == NULL);

	C2_ENTRY("BUILD");

	C2_ALLOC_PTR(lin_enum);
	if (lin_enum == NULL) {
		rc = -ENOMEM;
		C2_ADDB_ADD(&layout_global_ctx, &layout_addb_loc,
		            c2_addb_oom);
		C2_LOG("c2_linear_enum_build(): C2_ALLOC_PTR() failed, rc %d",
		       rc);
		goto out;
	}

	c2_layout_linear_enum_bob_init(lin_enum);

	rc = c2_layout_enum_init(&lin_enum->lle_base, lid,
				 &c2_linear_enum_type, &linear_enum_ops);
	if (rc != 0) {
		C2_LOG("c2_linear_enum_build(): lid %llu, "
		       "c2_layout_enum_init() failed, rc %d",
		       (unsigned long long)lid, rc);
		goto out;
	}

	lin_enum->lle_attr.lla_nr = nr;
	lin_enum->lle_attr.lla_A  = A;
	lin_enum->lle_attr.lla_B  = B;

	*out = lin_enum;
	C2_POST(c2_linear_enum_invariant(lin_enum, lid));

out:
	if (rc != 0 && lin_enum != NULL) {
		c2_layout_enum_fini(&lin_enum->lle_base);
		c2_free(lin_enum);
	}

	C2_LEAVE("rc %d", rc);
	return rc;
}

/**
 * Implementation of leo_fini for LINEAR enumeration type.
 * Invoked internally by l->l_ops->lo_fini().
 */
static void linear_fini(struct c2_layout_enum *e, uint64_t lid)
{
	struct c2_layout_linear_enum *lin_enum;

	C2_PRE(e != NULL);

	C2_ENTRY("DESTROY");

	lin_enum = container_of(e, struct c2_layout_linear_enum, lle_base);

	C2_ASSERT(c2_linear_enum_invariant(lin_enum, lid));

	c2_layout_linear_enum_bob_fini(lin_enum);
	c2_layout_enum_fini(e);

	c2_free(lin_enum);

	C2_LEAVE();
}

/**
 * Implementation of leto_register for LINEAR enumeration type.
 * No table is required specifically for LINEAR enum type.
 */
static int linear_register(struct c2_ldb_schema *schema,
			   const struct c2_layout_enum_type *et)
{
	return 0;
}

/**
 * Implementation of leto_unregister for LINEAR enumeration type.
 */
static void linear_unregister(struct c2_ldb_schema *schema,
			      const struct c2_layout_enum_type *et)
{
}

/**
 * Implementation of leto_max_recsize() for linear enumeration type.
 *
 * Returns maximum record size for the part of the layouts table record,
 * required to store LINEAR enum details.
 */
static uint32_t linear_max_recsize(void)
{
	return sizeof(struct c2_layout_linear_attr);
}

/**
 * Implementation of leto_recsize() for linear enumeration type.
 *
 * Returns record size for the part of the layouts table record, required to
 * store LINEAR enum details for the specified enumeration object.
 */
static uint32_t linear_recsize(struct c2_layout_enum *e, uint64_t lid)
{
	return sizeof(struct c2_layout_linear_attr);
}

/**
 * Implementation of leto_decode() for linear enumeration type.
 * Reads linear enumeration type specific attributes from the buffer into
 * the c2_layout_linear_enum::c2_layout_linear_attr object.
 */
static int linear_decode(struct c2_ldb_schema *schema, uint64_t lid,
			 struct c2_bufvec_cursor *cur,
			 enum c2_layout_xcode_op op,
			 struct c2_db_tx *tx,
			 struct c2_layout_enum **out)
{
	struct c2_layout_linear_enum *lin_enum = NULL;
	struct c2_layout_linear_attr *lin_attr;
	int                           rc;

	C2_PRE(schema != NULL);
	C2_PRE(lid != LID_NONE);
	C2_PRE(cur != NULL);
	C2_PRE(op == C2_LXO_DB_LOOKUP || op == C2_LXO_DB_NONE);
	C2_PRE(ergo(op == C2_LXO_DB_LOOKUP, tx != NULL));
	C2_PRE(out != NULL && *out == NULL);

	C2_ENTRY("lid %llu", (unsigned long long)lid);

	/* Check if the buffer is with sufficient size. */
	if (c2_bufvec_cursor_step(cur) < sizeof *lin_attr) {
		rc = -ENOBUFS;
		C2_LOG("linear_decode(): lid %llu, buffer with insufficient "
		       "size", (unsigned long long)lid);
		goto out;
	}

	lin_attr = c2_bufvec_cursor_addr(cur);
	C2_ASSERT(lin_attr != NULL);

	if (lin_attr->lla_nr == LINEAR_NR_NONE) {
		rc = -EINVAL;
		C2_LOG("linear_decode(), lid %llu, Invalid value, nr %lu",
		       (unsigned long long)lid,
		       (unsigned long)lin_attr->lla_nr);
		goto out;
	}

	rc = c2_linear_enum_build(lid, lin_attr->lla_nr, lin_attr->lla_A,
				  lin_attr->lla_B, &lin_enum);
	if (rc != 0) {
		C2_LOG("linear_decode(): lid %llu, c2_linear_enum_build() "
		       "failed, rc %d", (unsigned long long)lid, rc);
		goto out;
	}

	*out = &lin_enum->lle_base;
	C2_POST(c2_linear_enum_invariant(lin_enum, lid));

out:
	C2_LEAVE("rc %d", rc);
	return rc;
}

/**
 * Implementation of leto_encode() for linear enumeration type.
 * Reads linear enumeration type specific attributes from the
 * c2_layout_linear_enum object into the buffer.
 */
static int linear_encode(struct c2_ldb_schema *schema,
			 struct c2_layout *l,
			 enum c2_layout_xcode_op op,
			 struct c2_db_tx *tx,
			 struct c2_bufvec_cursor *oldrec_cur,
			 struct c2_bufvec_cursor *out)
{
	struct c2_layout_striped     *stl;
	struct c2_layout_linear_enum *lin_enum;
	struct c2_layout_linear_attr *old_attr;
	c2_bcount_t                   nbytes;
	int                           rc = 0;

	C2_PRE(schema != NULL);
	C2_PRE(layout_invariant(l));
	C2_PRE(op == C2_LXO_DB_ADD || op == C2_LXO_DB_UPDATE ||
	       op == C2_LXO_DB_DELETE || op == C2_LXO_DB_NONE);
	C2_PRE(ergo(op != C2_LXO_DB_NONE, tx != NULL));
	C2_PRE(ergo(op == C2_LXO_DB_UPDATE, oldrec_cur != NULL));
	C2_PRE(out != NULL);

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);

	/* Check if the buffer is with sufficient size. */
	if (c2_bufvec_cursor_step(out) < sizeof lin_enum->lle_attr) {
		rc = -ENOBUFS;
		C2_LOG("linear_encode(): lid %llu, buffer with insufficient "
		       "size", (unsigned long long)l->l_id);
		goto out;
	}

	/* Check if the buffer for old record is with sufficient size. */
	if (!ergo(op == C2_LXO_DB_UPDATE,
	    c2_bufvec_cursor_step(oldrec_cur) >= sizeof old_attr)) {
		rc = -ENOBUFS;
		C2_LOG("linear_encode(): lid %llu, buffer for old record with"
		       " insufficient size", (unsigned long long)l->l_id);
		goto out;
	}

	stl = container_of(l, struct c2_layout_striped, ls_base);
	lin_enum = container_of(stl->ls_enum, struct c2_layout_linear_enum,
				lle_base);

	C2_ASSERT(c2_linear_enum_invariant(lin_enum, l->l_id));

	if (op == C2_LXO_DB_UPDATE) {
		old_attr = c2_bufvec_cursor_addr(oldrec_cur);

		if (old_attr->lla_nr != lin_enum->lle_attr.lla_nr ||
		    old_attr->lla_A != lin_enum->lle_attr.lla_A ||
		    old_attr->lla_B != lin_enum->lle_attr.lla_B) {
			rc = -EINVAL;
			C2_LOG("linear_encode(): lid %llu, New values "
			       "do not match the old ones",
			       (unsigned long long)l->l_id);
			goto out;
		}
	}

	nbytes = c2_bufvec_cursor_copyto(out, &lin_enum->lle_attr,
					 sizeof lin_enum->lle_attr);
	C2_ASSERT(nbytes == sizeof lin_enum->lle_attr);

out:
	C2_LEAVE("lid %llu, rc %d", (unsigned long long)l->l_id, rc);
	return rc;
}

/**
 * Implementation of leo_nr for LINEAR enumeration.
 * Rerurns number of objects in the enumeration.
 */
static uint32_t linear_nr(const struct c2_layout_enum *le, uint64_t lid)
{
	struct c2_layout_linear_enum *lin_enum;

	C2_PRE(le != NULL);
	C2_PRE(lid != LID_NONE);

	lin_enum = container_of(le, struct c2_layout_linear_enum, lle_base);
	C2_ASSERT(c2_linear_enum_invariant(lin_enum, lid));

	return lin_enum->lle_attr.lla_nr;
}

/**
 * Implementation of leo_get for LINEAR enumeration.
 * Rerurns idx-th object from the enumeration.
 */
static void linear_get(const struct c2_layout_enum *le, uint64_t lid,
		       uint32_t idx, const struct c2_fid *gfid,
		       struct c2_fid *out)
{
	struct c2_layout_linear_enum *lin_enum;

	C2_PRE(le != NULL);
	C2_PRE(lid != LID_NONE);
	C2_PRE(gfid != NULL);
	C2_PRE(out != NULL);

	lin_enum = container_of(le, struct c2_layout_linear_enum, lle_base);
	C2_ASSERT(c2_linear_enum_invariant(lin_enum, lid));

	C2_ASSERT(idx < lin_enum->lle_attr.lla_nr);

	c2_fid_set(out,
		   lin_enum->lle_attr.lla_A + idx * lin_enum->lle_attr.lla_B,
		   gfid->f_key);

	C2_ASSERT(c2_fid_is_valid(out));
}


static const struct c2_layout_enum_ops linear_enum_ops = {
	.leo_nr           = linear_nr,
	.leo_get          = linear_get,
	.leo_fini         = linear_fini
};

static const struct c2_layout_enum_type_ops linear_type_ops = {
	.leto_register    = linear_register,
	.leto_unregister  = linear_unregister,
	.leto_max_recsize = linear_max_recsize,
	.leto_recsize     = linear_recsize,
	.leto_decode      = linear_decode,
	.leto_encode      = linear_encode
};

const struct c2_layout_enum_type c2_linear_enum_type = {
	.let_name         = "linear",
	.let_id           = 1,
	.let_ops          = &linear_type_ops
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
