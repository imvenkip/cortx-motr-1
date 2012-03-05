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
 * Notes:
 * - Layout enumeration type specific register/unregister methods are not
 *   required for "linear" enumeration type, since the layout schema does not
 *   contain any separate tables specifically for "linear" enumeration type.
 *
 * @{
 */

#include "lib/errno.h"
#include "lib/tlist.h"	/* struct c2_tl */
#include "lib/vec.h"
#include "lib/bob.h"
#include "lib/memory.h"

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_LAYOUT
#include "lib/trace.h"

#include "layout/layout_internal.h"
#include "layout/linear_enum.h"

extern const struct c2_addb_loc layout_addb_loc;

enum {
	LINEAR_ENUM_MAGIC = 0xdcbaabcddcbaabcd /* dcba abcd dcba abcd */
};

static const struct c2_bob_type linear_enum_bob = {
	.bt_name         = "linear_enum",
	.bt_magix_offset = offsetof(struct c2_layout_linear_enum, lla_magic),
	.bt_magix        = LINEAR_ENUM_MAGIC,
	.bt_check        = NULL
};

C2_BOB_DEFINE(static, &linear_enum_bob, c2_layout_linear_enum);

static const struct c2_layout_enum_ops linear_enum_ops;

/* todo Make this accept attr struct instead of 3 vars. */
int c2_linear_enum_build(uint32_t nr, uint32_t A, uint32_t B,
			 struct c2_layout_linear_enum **out)
{
	struct c2_layout_linear_enum *lin_enum;
	int                           rc = 0;

	C2_PRE(out != NULL && *out == NULL);

	C2_ENTRY();

	C2_ALLOC_PTR(lin_enum);
	if (lin_enum == NULL) {
		rc = -ENOMEM;
		C2_ADDB_ADD(&c2_addb_global_ctx, &layout_addb_loc,
		            c2_addb_oom);
		C2_LOG("c2_linear_enum_build(): C2_ALLOC_PTR() failed, "
		       "rc %d\n", rc);
		goto out;
	}

	c2_layout_linear_enum_bob_init(lin_enum);
	C2_ASSERT(c2_layout_linear_enum_bob_check(lin_enum));

	c2_layout_enum_init(&lin_enum->lle_base, &c2_linear_enum_type,
			    &linear_enum_ops);

	lin_enum->lle_attr.lla_nr = nr;
	lin_enum->lle_attr.lla_A  = A;
	lin_enum->lle_attr.lla_B  = B;

	*out = lin_enum;

out:
	C2_LEAVE("rc %d", rc);
	return rc;
}

void c2_linear_enum_fini(struct c2_layout_linear_enum *lin_enum)
{
	C2_ENTRY();
	c2_layout_linear_enum_bob_fini(lin_enum);
	C2_ASSERT(!c2_layout_linear_enum_bob_check(lin_enum));

	c2_layout_enum_fini(&lin_enum->lle_base);
	C2_LEAVE();
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
 * store LINEAR enum details for the specified layout.
 */
static uint32_t linear_recsize(struct c2_layout_enum *e)
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
	int                           rc = 0;

	C2_PRE(schema != NULL);
	C2_PRE(lid != LID_NONE);
	C2_PRE(cur != NULL);
	C2_PRE(op == C2_LXO_DB_LOOKUP || op == C2_LXO_DB_NONE);
	C2_PRE(ergo(op == C2_LXO_DB_LOOKUP, tx != NULL));
	C2_PRE(out != NULL && *out == NULL);

	C2_ENTRY("lid %llu\n", (unsigned long long)lid);

	/* Check if the buffer is with sufficient size. */
	if (c2_bufvec_cursor_step(cur) < sizeof *lin_attr) {
		rc = -ENOBUFS;
		C2_LOG("list_decode(): lid %llu, buffer with insufficient "
		       "size", (unsigned long long)lid);
		goto out;
	}

	lin_attr = c2_bufvec_cursor_addr(cur);
	C2_ASSERT(lin_attr != NULL);

	if (lin_attr->lla_nr == 0 || lin_attr->lla_A == 0 ||
	    lin_attr->lla_B == 0) {
		rc = -EINVAL;
		C2_LOG("In linear_decode(), lid %llu, Invalid value, "
		       "nr %lu, A %lu, B %lu\n",
		       (unsigned long long)lid,
		       (unsigned long)lin_attr->lla_nr,
		       (unsigned long)lin_attr->lla_A,
		       (unsigned long)lin_attr->lla_B);
		goto out;
	}

	rc = c2_linear_enum_build(lin_attr->lla_nr, lin_attr->lla_A,
				  lin_attr->lla_B, &lin_enum);
	if (rc != 0) {
		C2_LOG("list_decode(): lid %llu, c2_linear_enum_build() "
		       "failed, rc %d", (unsigned long long)lid, rc);
		goto out;
	}

	*out = &lin_enum->lle_base;

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
	c2_bcount_t                   num_bytes;
	int                           rc = 0;

	C2_PRE(schema != NULL);
	C2_PRE(layout_invariant(l));
	C2_PRE(op == C2_LXO_DB_ADD || op == C2_LXO_DB_UPDATE ||
	       op == C2_LXO_DB_DELETE || op == C2_LXO_DB_NONE);
	C2_PRE(ergo(op != C2_LXO_DB_NONE, tx != NULL));
	C2_PRE(out != NULL);

	C2_ENTRY("lid %llu\n", (unsigned long long)l->l_id);

	/* Check if the buffer is with sufficient size. */
	if (c2_bufvec_cursor_step(out)
	    < sizeof(struct c2_layout_linear_attr)) {
		rc = -ENOBUFS;
		C2_LOG("linear_encode(): lid %llu, buffer with insufficient "
		       "size", (unsigned long long)l->l_id);
		goto out;
	}

	stl = container_of(l, struct c2_layout_striped, ls_base);

	lin_enum = container_of(stl->ls_enum, struct c2_layout_linear_enum,
				lle_base);

	C2_ASSERT(lin_enum->lle_attr.lla_nr != 0);
	C2_ASSERT(lin_enum->lle_attr.lla_A != 0);
	C2_ASSERT(lin_enum->lle_attr.lla_B != 0);

	num_bytes = c2_bufvec_cursor_copyto(out, &lin_enum->lle_attr,
					    sizeof lin_enum->lle_attr);
	C2_ASSERT(num_bytes == sizeof lin_enum->lle_attr);

out:
	C2_LEAVE("rc %d", rc);
	return 0;
}

/**
 * Implementation of leo_nr for LINEAR enumeration.
 * Rerurns number of objects in the enumeration.
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
 * Implementation of leo_get for LINEAR enumeration.
 * Rerurns idx-th object from the enumeration.
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
	.leto_max_recsize    = linear_max_recsize,
	.leto_recsize        = linear_recsize,
	.leto_decode         = linear_decode,
	.leto_encode         = linear_encode
};

const struct c2_layout_enum_type c2_linear_enum_type = {
	.let_name       = "linear",
	.let_id         = 1,
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
