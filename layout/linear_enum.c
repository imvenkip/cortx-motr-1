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
#include "lib/tlist.h"  /* struct c2_tl */
#include "lib/vec.h"    /* c2_bufvec_cursor_step(), c2_bufvec_cursor_addr() */
#include "lib/memory.h" /* C2_ALLOC_PTR() */
#include "lib/misc.h"   /* C2_IN() */
#include "lib/bob.h"

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_LAYOUT
#include "lib/trace.h"

#include "fid/fid.h"    /* c2_fid_set(), c2_fid_is_valid() */
#include "layout/layout_internal.h"
#include "layout/linear_enum.h"

extern const struct c2_addb_loc layout_addb_loc;
extern struct c2_addb_ctx layout_global_ctx;

enum {
	LINEAR_ENUM_MAGIC = 0x4C494E2D454E554DULL /* LIN-ENUM */
};

static const struct c2_bob_type linear_bob = {
	.bt_name         = "linear_enum",
	.bt_magix_offset = offsetof(struct c2_layout_linear_enum, lla_magic),
	.bt_magix        = LINEAR_ENUM_MAGIC,
	.bt_check        = NULL
};

C2_BOB_DEFINE(static, &linear_bob, c2_layout_linear_enum);

static bool linear_allocated_invariant(const struct c2_layout_linear_enum *le)
{
	return
		c2_layout_linear_enum_bob_check(le) &&
		le->lle_attr.lla_nr == 0 &&
		le->lle_attr.lla_B == 0;
}

/**
 * linear_invariant() can not be invoked until an enumeration object
 * is associated with some layout object. Hence this separation.
 */
static bool linear_invariant_internal(const struct c2_layout_linear_enum *le)
{
	return
		c2_layout_linear_enum_bob_check(le) &&
		le->lle_attr.lla_nr != 0 &&
		le->lle_attr.lla_B != 0;
}

static bool linear_invariant(const struct c2_layout_linear_enum *le)
{
	return
		linear_invariant_internal(le) &&
		c2_layout__enum_invariant(&le->lle_base);
}

static const struct c2_layout_enum_ops linear_enum_ops;

/** Implementation of leto_allocate for LINEAR enumeration type. */
static int linear_allocate(struct c2_layout_domain *dom,
			   struct c2_layout_enum **out)
{
	struct c2_layout_linear_enum *lin_enum;

	C2_PRE(c2_layout__domain_invariant(dom));
	C2_PRE(out != NULL);

	C2_ENTRY();
	C2_ALLOC_PTR(lin_enum);
	if (lin_enum == NULL) {
		c2_layout__log("linear_allocate", "C2_ALLOC_PTR() failed",
			       &c2_addb_oom, &layout_global_ctx, LID_NONE,
			       -ENOMEM);
		return -ENOMEM;
	}
	c2_layout__enum_init(dom, &lin_enum->lle_base,
			     &c2_linear_enum_type, &linear_enum_ops);
	c2_layout_linear_enum_bob_init(lin_enum);
	C2_POST(linear_allocated_invariant(lin_enum));
	*out = &lin_enum->lle_base;
	C2_LEAVE("linear enum pointer %p", lin_enum);
	return 0;
}

static void linear_delete(struct c2_layout_enum *e)
{
	struct c2_layout_linear_enum *lin_enum;

	lin_enum = bob_of(e, struct c2_layout_linear_enum,
		          lle_base, &linear_bob);
	C2_PRE(linear_allocated_invariant(lin_enum));

	C2_ENTRY("lid %llu, enum_pointer %p",
		 (unsigned long long)e->le_sl->sl_base.l_id, e);
	c2_layout_linear_enum_bob_fini(lin_enum);
	c2_layout__enum_fini(e);
	c2_free(lin_enum);
	C2_LEAVE();
}

static int linear_populate(struct c2_layout_linear_enum *lin_enum,
			   const struct c2_layout_linear_attr *attr)
{
	C2_PRE(linear_allocated_invariant(lin_enum));
	C2_PRE(attr != NULL);

	if (attr->lla_nr == 0 || attr->lla_B == 0) {
		C2_LOG("lin_enum %p, attr %p,  Invalid attributes, rc %d",
		       lin_enum, attr, -EINVAL);
		return -EINVAL;
	}
	lin_enum->lle_attr = *attr;
	C2_POST(linear_invariant_internal(lin_enum));
	return 0;
}

int c2_linear_enum_build(struct c2_layout_domain *dom,
			 const struct c2_layout_linear_attr *attr,
			 struct c2_layout_linear_enum **out)
{
	struct c2_layout_enum        *e;
	struct c2_layout_linear_enum *lin_enum;
	int                           rc;

	C2_PRE(out != NULL);
	C2_ENTRY("domain %p", dom);
	rc = linear_allocate(dom, &e);
	if (rc == 0) {
		lin_enum = bob_of(e, struct c2_layout_linear_enum,
			          lle_base, &linear_bob);
		rc = linear_populate(lin_enum, attr);
		if (rc == 0)
			*out = lin_enum;
		else
			linear_delete(e);
	}
	C2_POST(ergo(rc == 0, linear_invariant_internal(lin_enum)));
	C2_LEAVE("domain %p, rc %d", dom, rc);
	return rc;
}

static struct c2_layout_linear_enum
*enum_to_linear_enum(const struct c2_layout_enum *e)
{
	struct c2_layout_linear_enum *lin_enum;

	lin_enum = bob_of(e, struct c2_layout_linear_enum,
			  lle_base, &linear_bob);
	C2_ASSERT(linear_invariant(lin_enum));
	return lin_enum;
}

/** Implementation of leo_fini for LINEAR enumeration type. */
static void linear_fini(struct c2_layout_enum *e)
{
	struct c2_layout_linear_enum *lin_enum;

	C2_PRE(e != NULL);

	C2_ENTRY("lid %llu, enum_pointer %p",
		 (unsigned long long)e->le_sl->sl_base.l_id, e);
	lin_enum = enum_to_linear_enum(e);
	c2_layout_linear_enum_bob_fini(lin_enum);
	c2_layout__enum_fini(e);
	c2_free(lin_enum);
	C2_LEAVE();
}

/**
 * Implementation of leto_register for LINEAR enumeration type.
 * No table is required specifically for LINEAR enum type.
 */
static int linear_register(struct c2_layout_domain *dom,
			   const struct c2_layout_enum_type *et)
{
	return 0;
}

/** Implementation of leto_unregister for LINEAR enumeration type. */
static void linear_unregister(struct c2_layout_domain *dom,
			      const struct c2_layout_enum_type *et)
{
}

/** Implementation of leto_max_recsize() for linear enumeration type. */
static c2_bcount_t linear_max_recsize(void)
{
	return sizeof(struct c2_layout_linear_attr);
}

/** Implementation of leo_decode() for linear enumeration type. */
static int linear_decode(struct c2_layout_enum *e,
			 struct c2_bufvec_cursor *cur,
			 enum c2_layout_xcode_op op,
			 struct c2_db_tx *tx,
			 struct c2_striped_layout *stl)
{
	uint64_t                      lid;
	struct c2_layout_linear_enum *lin_enum;
	struct c2_layout_linear_attr *lin_attr;
	int                           rc;

	C2_PRE(e != NULL);
	C2_PRE(cur != NULL);
	C2_PRE(c2_bufvec_cursor_step(cur) >= sizeof *lin_attr);
	C2_PRE(C2_IN(op, (C2_LXO_DB_LOOKUP, C2_LXO_BUFFER_OP)));
	C2_PRE(ergo(op == C2_LXO_DB_LOOKUP, tx != NULL));
	C2_PRE(c2_layout__striped_allocated_invariant(stl));

	lid = stl->sl_base.l_id;
	C2_ENTRY("lid %llu", (unsigned long long)lid);
	lin_enum = bob_of(e, struct c2_layout_linear_enum,
			  lle_base, &linear_bob);
	C2_ASSERT(linear_allocated_invariant(lin_enum));

	lin_attr = c2_bufvec_cursor_addr(cur);
	c2_bufvec_cursor_move(cur, sizeof *lin_attr);
	rc = linear_populate(lin_enum, lin_attr);
	if (rc != 0)
		C2_LOG("linear_populate() failed");
	C2_POST(ergo(rc == 0, linear_invariant_internal(lin_enum)));
	C2_POST(ergo(rc != 0, linear_allocated_invariant(lin_enum)));
	C2_LEAVE("lid %llu, rc %d", (unsigned long long)lid, rc);
	return rc;
}

/** Implementation of leo_encode() for linear enumeration type. */
static int linear_encode(const struct c2_layout_enum *e,
			 enum c2_layout_xcode_op op,
			 struct c2_db_tx *tx,
			 struct c2_bufvec_cursor *out)
{
	struct c2_layout_linear_enum *lin_enum;
	c2_bcount_t                   nbytes;
	uint64_t                      lid;

	C2_PRE(e != NULL);
	C2_PRE(C2_IN(op, (C2_LXO_DB_ADD, C2_LXO_DB_UPDATE,
			  C2_LXO_DB_DELETE, C2_LXO_BUFFER_OP)));
	C2_PRE(ergo(op != C2_LXO_BUFFER_OP, tx != NULL));
	C2_PRE(out != NULL);
	C2_PRE(c2_bufvec_cursor_step(out) >= sizeof lin_enum->lle_attr);

	lin_enum = enum_to_linear_enum(e);
	lid = lin_enum->lle_base.le_sl->sl_base.l_id;
	C2_ENTRY("lid %llu", (unsigned long long)lid);
	nbytes = c2_bufvec_cursor_copyto(out, &lin_enum->lle_attr,
					 sizeof lin_enum->lle_attr);
	C2_ASSERT(nbytes == sizeof lin_enum->lle_attr);
	C2_LEAVE("lid %llu", (unsigned long long)lid);
	return 0;
}

/** Implementation of leo_nr for LINEAR enumeration. */
static uint32_t linear_nr(const struct c2_layout_enum *e)
{
	struct c2_layout_linear_enum *lin_enum;

	C2_PRE(e != NULL);

	C2_ENTRY("lid %llu, enum_pointer %p",
		 (unsigned long long)e->le_sl->sl_base.l_id, e);
	lin_enum = enum_to_linear_enum(e);
	C2_LEAVE("lid %llu, enum_pointer %p, nr %lu",
		 (unsigned long long)e->le_sl->sl_base.l_id, e,
		 (unsigned long)lin_enum->lle_attr.lla_nr);
	return lin_enum->lle_attr.lla_nr;
}

/** Implementation of leo_get for LINEAR enumeration. */
static void linear_get(const struct c2_layout_enum *e, uint32_t idx,
		       const struct c2_fid *gfid, struct c2_fid *out)
{
	struct c2_layout_linear_enum *lin_enum;

	C2_PRE(e != NULL);
	C2_PRE(gfid != NULL);
	C2_PRE(out != NULL);

	C2_ENTRY("lid %llu, enum_pointer %p",
		 (unsigned long long)e->le_sl->sl_base.l_id, e);
	lin_enum = enum_to_linear_enum(e);
	C2_ASSERT(idx < lin_enum->lle_attr.lla_nr);
	c2_fid_set(out,
		   lin_enum->lle_attr.lla_A + idx * lin_enum->lle_attr.lla_B,
		   gfid->f_key);

	C2_LEAVE("lid %llu, enum_pointer %p, fid_pointer %p",
		 (unsigned long long)e->le_sl->sl_base.l_id, e, out);
	C2_ASSERT(c2_fid_is_valid(out));
}

/** Implementation of leo_recsize() for linear enumeration type. */
static c2_bcount_t linear_recsize(struct c2_layout_enum *e)
{
	return sizeof(struct c2_layout_linear_attr);
}

static const struct c2_layout_enum_ops linear_enum_ops = {
	.leo_nr      = linear_nr,
	.leo_get     = linear_get,
	.leo_recsize = linear_recsize,
	.leo_fini    = linear_fini,
	.leo_delete  = linear_delete,
	.leo_decode  = linear_decode,
	.leo_encode  = linear_encode
};

static const struct c2_layout_enum_type_ops linear_type_ops = {
	.leto_register    = linear_register,
	.leto_unregister  = linear_unregister,
	.leto_max_recsize = linear_max_recsize,
	.leto_allocate    = linear_allocate
};

struct c2_layout_enum_type c2_linear_enum_type = {
	.let_name      = "linear",
	.let_id        = 1,
	.let_ref_count = 0,
	.let_domain    = NULL,
	.let_ops       = &linear_type_ops
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
