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

extern const struct c2_addb_loc layout_addb_loc;
extern struct c2_addb_ctx layout_global_ctx;

enum {
	LINEAR_ENUM_MAGIC = 0x4C494E2D454E554DULL /* LIN-ENUM */
};

static const struct c2_bob_type linear_enum_bob = {
	.bt_name         = "linear_enum",
	.bt_magix_offset = offsetof(struct c2_layout_linear_enum, lla_magic),
	.bt_magix        = LINEAR_ENUM_MAGIC,
	.bt_check        = NULL
};

C2_BOB_DEFINE(static, &linear_enum_bob, c2_layout_linear_enum);

/**
 * linear_enum_invariant() can not be invoked until an enumeration object
 * is associated with some layout object. Hence this separation.
 */
static bool linear_enum_invariant_internal(
				const struct c2_layout_linear_enum *le)
{
	return
		le != NULL &&
		le->lle_attr.lla_nr != NR_NONE &&
		le->lle_attr.lla_B != 0 &&
		c2_layout_linear_enum_bob_check(le);
}

static bool linear_enum_invariant(const struct c2_layout_linear_enum *le)
{
	return
		linear_enum_invariant_internal(le) &&
		enum_invariant(&le->lle_base);
}

static const struct c2_layout_enum_ops linear_enum_ops;

/**
 * Build linear enumeration object.
 * @note Enum object need not be finalised explicitly by the user. It is
 * finalised internally through c2_layout__striped_fini().
 */
int c2_linear_enum_build(struct c2_layout_domain *dom,
			 uint32_t nr, uint32_t A, uint32_t B,
			 struct c2_layout_linear_enum **out)
{
	struct c2_layout_linear_enum *lin_enum;
	int                           rc;

	C2_PRE(domain_invariant(dom));
	C2_PRE(nr != NR_NONE);
	C2_PRE(B != 0);
	C2_PRE(out != NULL);

	C2_ENTRY();

	C2_ALLOC_PTR(lin_enum);
	if (lin_enum == NULL) {
		rc = -ENOMEM;
		c2_layout__log("c2_linear_enum_build", "C2_ALLOC_PTR() failed",
			       PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			       &c2_addb_oom, &layout_global_ctx, LID_NONE, rc);
	} else {
		rc = 0;
		c2_layout_linear_enum_bob_init(lin_enum);

		c2_layout__enum_init(dom, &lin_enum->lle_base,
				     &c2_linear_enum_type,
				     &linear_enum_ops);

		lin_enum->lle_attr.lla_nr = nr;
		lin_enum->lle_attr.lla_A  = A;
		lin_enum->lle_attr.lla_B  = B;

		*out = lin_enum;
		C2_POST(linear_enum_invariant_internal(lin_enum));
	}

	C2_LEAVE("rc %d", rc);
	return rc;
}

/**
 * Implementation of leo_fini for LINEAR enumeration type.
 * Invoked internally by c2_layout__striped_fini().
 */
static void linear_fini(struct c2_layout_enum *e)
{
	struct c2_layout_linear_enum *lin_enum;

	C2_PRE(e != NULL);

	C2_ENTRY();

	lin_enum = container_of(e, struct c2_layout_linear_enum, lle_base);
	C2_ASSERT(linear_enum_invariant(lin_enum));

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

/**
 * Implementation of leto_unregister for LINEAR enumeration type.
 */
static void linear_unregister(struct c2_layout_domain *dom,
			      const struct c2_layout_enum_type *et)
{
}

/**
 * Implementation of leto_max_recsize() for linear enumeration type.
 *
 * Returns maximum record size for the part of the layouts table record,
 * required to store LINEAR enum details.
 */
static c2_bcount_t linear_max_recsize(void)
{
	return sizeof(struct c2_layout_linear_attr);
}

/**
 * Implementation of leto_recsize() for linear enumeration type.
 *
 * Returns record size for the part of the layouts table record, required to
 * store LINEAR enum details for the specified enumeration object.
 */
static c2_bcount_t linear_recsize(struct c2_layout_enum *e)
{
	return sizeof(struct c2_layout_linear_attr);
}

/**
 * Implementation of leto_decode() for linear enumeration type.
 * Reads linear enumeration type specific attributes from the buffer into
 * the c2_layout_linear_enum::c2_layout_linear_attr object.
 */
static int linear_decode(struct c2_layout_domain *dom,
			 uint64_t lid,
			 enum c2_layout_xcode_op op,
			 struct c2_db_tx *tx,
			 struct c2_bufvec_cursor *cur,
			 struct c2_layout_enum **out)
{
	struct c2_layout_linear_enum *lin_enum = NULL;
	struct c2_layout_linear_attr *lin_attr;
	int                           rc;

	C2_PRE(domain_invariant(dom));
	C2_PRE(lid != LID_NONE);
	C2_PRE(op == C2_LXO_DB_LOOKUP || op == C2_LXO_BUFFER_OP);
	C2_PRE(ergo(op == C2_LXO_DB_LOOKUP, tx != NULL));
	C2_PRE(cur != NULL);
	C2_PRE(c2_bufvec_cursor_step(cur) >= sizeof *lin_attr);
	C2_PRE(out != NULL);

	C2_ENTRY("lid %llu", (unsigned long long)lid);

	lin_attr = c2_bufvec_cursor_addr(cur);
	C2_ASSERT(lin_attr != NULL);

	rc = c2_linear_enum_build(dom, lin_attr->lla_nr, lin_attr->lla_A,
				  lin_attr->lla_B, &lin_enum);
	if (rc != 0) {
		C2_LOG("linear_decode(): lid %llu, c2_linear_enum_build() "
		       "failed, rc %d", (unsigned long long)lid, rc);
		goto out;
	}

	*out = &lin_enum->lle_base;
	C2_POST(linear_enum_invariant_internal(lin_enum));

out:
	C2_LEAVE("rc %d", rc);
	return rc;
}

/**
 * Implementation of leto_encode() for linear enumeration type.
 * Reads linear enumeration type specific attributes from the
 * c2_layout_linear_enum object into the buffer.
 */
static int linear_encode(const struct c2_layout_enum *le,
			 enum c2_layout_xcode_op op,
			 struct c2_db_tx *tx,
			 struct c2_bufvec_cursor *oldrec_cur,
			 struct c2_bufvec_cursor *out)
{
	struct c2_layout_linear_enum *lin_enum;
	struct c2_layout_linear_attr *old_attr;
	c2_bcount_t                   nbytes;
	uint64_t                      lid;

	C2_PRE(le != NULL);
	C2_PRE(op == C2_LXO_DB_ADD || op == C2_LXO_DB_UPDATE ||
	       op == C2_LXO_DB_DELETE || op == C2_LXO_BUFFER_OP);
	C2_PRE(ergo(op != C2_LXO_BUFFER_OP, tx != NULL));
	C2_PRE(ergo(op == C2_LXO_DB_UPDATE, oldrec_cur != NULL));
	C2_PRE(ergo(op == C2_LXO_DB_UPDATE,
		    c2_bufvec_cursor_step(oldrec_cur) >= sizeof old_attr));
	C2_PRE(out != NULL);
	C2_PRE(c2_bufvec_cursor_step(out) >= sizeof lin_enum->lle_attr);

	lin_enum = container_of(le, struct c2_layout_linear_enum, lle_base);
	C2_ASSERT(linear_enum_invariant(lin_enum));

	lid = lin_enum->lle_base.le_l->l_id;
	C2_ENTRY("lid %llu", (unsigned long long)lid);

	if (op == C2_LXO_DB_UPDATE) {
		old_attr = c2_bufvec_cursor_addr(oldrec_cur);

		C2_ASSERT(old_attr->lla_nr == lin_enum->lle_attr.lla_nr &&
			  old_attr->lla_A == lin_enum->lle_attr.lla_A &&
			  old_attr->lla_B == lin_enum->lle_attr.lla_B);
	}

	nbytes = c2_bufvec_cursor_copyto(out, &lin_enum->lle_attr,
					 sizeof lin_enum->lle_attr);
	C2_ASSERT(nbytes == sizeof lin_enum->lle_attr);

	C2_LEAVE("lid %llu", (unsigned long long)lid);
	return 0;
}

/**
 * Implementation of leo_nr for LINEAR enumeration.
 * Rerurns number of objects in the enumeration.
 */
static uint32_t linear_nr(const struct c2_layout_enum *le)
{
	struct c2_layout_linear_enum *lin_enum;

	C2_PRE(le != NULL);

	lin_enum = container_of(le, struct c2_layout_linear_enum, lle_base);
	C2_ASSERT(linear_enum_invariant(lin_enum));

	return lin_enum->lle_attr.lla_nr;
}

/**
 * Implementation of leo_get for LINEAR enumeration.
 * Rerurns idx-th object from the enumeration.
 */
static void linear_get(const struct c2_layout_enum *le, uint32_t idx,
		       const struct c2_fid *gfid, struct c2_fid *out)
{
	struct c2_layout_linear_enum *lin_enum;

	C2_PRE(le != NULL);
	C2_PRE(gfid != NULL);
	C2_PRE(out != NULL);

	lin_enum = container_of(le, struct c2_layout_linear_enum, lle_base);
	C2_ASSERT(linear_enum_invariant(lin_enum));

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

struct c2_layout_enum_type c2_linear_enum_type = {
	.let_name         = "linear",
	.let_id           = 1,
	.let_ref_count    = 0,
	.let_domain       = NULL,
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
