/* -*- C -*- */
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
 * Original creation date: 11/26/2010
 */

#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/errno.h"               /* ENOMEM */
#include "lib/misc.h"                /* C2_SET0 */
#include "lib/cdefs.h"               /* C2_EXPORTED */
#include "fid/fid.h"
#include "fop/fop.h"
#include "fop/fop_iterator.h"

/**
   @addtogroup fop

   <b>Fop iterator implementation.</b>

   The central data-structure, required to implement efficiently iteration over
   fop fields, is an index, recording for each fop type, a set of its fields
   that must be inspected by the iterator.

   An important observation simplifies the design of that index considerably:
   number of iterator types is relatively small---10 looks like a reasonable
   upper boundary. With this in mind, the following rough approach would work:

   - let FIT_TYPE_MAX be the maximal possible number of iterator types,
     supported by the system;

   - allocate for each fop type T a Boolean array T.present[FIT_TYPE_MAX][N],
     where N is number of fields in the type;

   - T.present[i][j] is true, iff j-th field must be inspected to iterate i-th
     iterator type. This means that either
     - j-th field has a type watched by the iterator (see c2_fit_watch), or
     - j-th field has a type S, such that S.present[i][k] is true for some k
       (that is, while the field itself is not watched, some of its sub-fields
       are).

   Because T.present[][] is expected to be mostly filled with false, the
   iteration can be sped up by storing only indices of non-false elements, see
   ftype_fit::ff_present[].

   T.present[][] array is implemented by means of a "fop decorator" interface,
   see c2_fop_decorator_register().

   @{
 */

enum {
	/**
	   Maximal number of fop iterator types.
	 */
	FIT_TYPE_MAX = 16
};

C2_TL_DESCR_DEFINE(wat, "fop iterator watches", static, struct c2_fit_watch,
		   fif_linkage,	fif_magix,
		   0x10d15edc0a1f1e1d /* iodised coalfield */,
		   0xcea5e1e55debac1e /* ceaseless debacle */);
C2_TL_DEFINE(wat, static, struct c2_fit_watch);

C2_TL_DESCR_DEFINE(mod, "fop iterator modifiers", static, struct c2_fit_mod,
		   fm_linkage,	fm_magix,
		   0xbe111c05ec0d1c11 /* bellicose codicil */,
		   0x5ca1ab1e5caff01d /* scalable scaffold */);
C2_TL_DEFINE(mod, static, struct c2_fit_mod);

/**
   Per-field data kept for each fop iterator type.

   @see ftype_fit
 */
struct field_fit_data {
	/**
	   Used as a sentinel flag.

	   @see ftype_fit::ff_present[]
	 */
	bool     ffd_valid;
	/**
	   True iff the iterator has to recurse into the field.
	 */
	bool     ffd_recurse;
	/**
	   The index of an element in c2_fop_type::ft_top::fft_child[].
	 */
	uint32_t ffd_fieldno;
	/**
	   Cumulative bits for this field and this iterator type.

	   @see c2_fit_watch::fif_bits
	   @see c2_fit_mod::fm_bits_add, c2_fit_mod::fm_bits_sub
	 */
	uint64_t ffd_bits;
};

/**
   fop iterators related data, allocated for each fop type.
 */
struct ftype_fit {
	/** Number of fields in the fop type. */
	size_t ff_nr;
	/**
	   Each element of this array is an array of c2_fop_type::ft_top::fft_nr
	   + 1 elements. ftype_fit::ff_present[i] is an array with information
	   about the fop type field that must be considered when iterating over
	   a fop of this type with an iterator of i-th iterator type.

	   Less than ff_nr elements can be actually
	   used. ftype_fit::ff_present[i][j].ffd_valid is used to tell used
	   element from unused. ffd_valid is also used as a sentinel flag to
	   terminate iteration over the array. The "+ 1" above is to guarantee
	   that the sentinel always exists.
	 */
	struct field_fit_data *ff_present[FIT_TYPE_MAX];
};

/**
   Global array of all fop iterator types registered in this address space.
 */
static struct c2_fit_type *fits[FIT_TYPE_MAX] = { NULL, };

/*
 * Some helper functions.
 */

static const struct ftype_fit *data_get(const struct c2_xcode_type *xct);
static struct c2_fit_frame *fit_top(struct c2_fit *it);
static const struct field_fit_data *top_field(struct c2_fit *it);
static void *drill(const struct c2_fit *it, int depth);
static bool tag_find(struct c2_fit *it);

static const struct c2_addb_loc fit_addb_loc = {
	.al_name = "fop-iterator"
};

/**
   An invariant maintained for ftype_fit.
 */
static bool ftype_fit_invariant(const struct ftype_fit *data)
{
	int i;
	int t;

	C2_CASSERT(ARRAY_SIZE(data->ff_present) == ARRAY_SIZE(fits));

	for (t = 0; t < ARRAY_SIZE(data->ff_present); ++t) {
		bool end;

		/* if this iterator type is not registered---there
		   should be no data. */
		if (fits[t] == NULL) {
			if (data->ff_present[t] != NULL)
				return false;
			continue;
		}

		for (end = false, i = 0; i <= data->ff_nr; ++i) {
			struct field_fit_data *fd;

			fd = &data->ff_present[t][i];
			/* found first unused slot... */
			if (!fd->ffd_valid) {
				end = true;
				continue;
			}
			/* all slots after first unused are also unused. */
			if (end)
				return false;
			if (fd->ffd_fieldno >= data->ff_nr)
				return false;
			/* field numbers increase monotonically. */
			if (i > 0 && fd->ffd_fieldno <= fd[-1].ffd_fieldno)
				return false;
			/* "recurse into" slot should have no bits attached. */
			if (fd->ffd_recurse && fd->ffd_bits != 0)
				return false;
		}
		/* there is at least one unused slot (sentinel). */
		if (!end)
			return false;
	}
	return true;
}

extern bool fop_types_built;

void c2_fop_itype_init(struct c2_fit_type *itype)
{
	int i;

	C2_PRE(itype->fit_index == -1);
	C2_PRE(!fop_types_built);

	wat_tlist_init(&itype->fit_watch);

	/*
	 * Search for a free slot in fits[] array and install itype there.
	 */

	for (i = 0; i < ARRAY_SIZE(fits); ++i) {
		if (fits[i] == NULL) {
			fits[i] = itype;
			itype->fit_index = i;
			break;
		}
	}
	C2_ASSERT(IS_IN_ARRAY(i, fits));
}

void c2_fop_itype_fini(struct c2_fit_type *itype)
{
	C2_PRE(IS_IN_ARRAY(itype->fit_index, fits));

	fits[itype->fit_index] = NULL;
	itype->fit_index = -1;
	wat_tlist_fini(&itype->fit_watch);
}

void c2_fop_itype_watch_add(struct c2_fit_type *itype,
			    struct c2_fit_watch *watch)
{
	struct c2_fit_watch *scan;

	c2_tl_for(wat, &itype->fit_watch, scan) {
		C2_PRE(scan->fif_xct != watch->fif_xct);
	} c2_tl_endfor;

	wat_tlink_init_at(watch, &itype->fit_watch);
	mod_tlist_init(&watch->fif_mod);
}

void c2_fop_itype_mod_add(struct c2_fit_watch *watch, struct c2_fit_mod *mod)
{
	mod_tlink_init_at(mod, &watch->fif_mod);
}

int c2_fit_field_add(struct c2_fit_watch *watch,
		     struct c2_fop_type *ftype, const char *fname,
		     uint64_t add_bits, uint64_t sub_bits)
{
	struct c2_fit_mod     *mod;
	struct c2_xcode_field *field;
	int                    result;

	field = c2_fop_type_field_find(*ftype->ft_xc_type, fname);
	if (field != NULL) {
		C2_ALLOC_PTR_ADDB(mod, &ftype->ft_addb, &fit_addb_loc);
		if (mod != NULL) {
			mod->fm_field = field;
			mod->fm_bits_add = add_bits;
			mod->fm_bits_sub = sub_bits;
			c2_fop_itype_mod_add(watch, mod);
			result = 0;
		} else
			result = -ENOMEM;
	} else
		result = -ENOENT;
	return result;
}

static bool c2_fit_invariant(const struct c2_fit *it)
{
	int i;
	int index;

	index = it->fi_type->fit_index;
	if (!IS_IN_ARRAY(index, fits))
		return false;
	if (fits[index] != it->fi_type)
		return false;
	if (!IS_IN_ARRAY(it->fi_depth, it->fi_stack))
		return false;
	if (it->fi_depth == -1)
		return true;
	if (it->fi_stack[0].ff_xc_type != *it->fi_fop->f_type->ft_xc_type)
		return false;
	for (i = 0; i <= it->fi_depth; ++i) {
		const struct c2_fit_frame      *frame;
		const struct ftype_fit         *data;
		const struct c2_xcode_type     *xct;

		frame = &it->fi_stack[i];
		xct   = frame->ff_xc_type;
		if (xct == NULL)
			return false;
		data = data_get(xct);
		if (frame->ff_pos > xct->xct_nr)
			return false;
		if (xct->xct_nr != data->ff_nr)
			return false;
		if (!ftype_fit_invariant(data))
			return false;
	}
	return true;
}

void c2_fit_init(struct c2_fit *it, struct c2_fit_type *itype,
		 struct c2_fop *fop)
{
	C2_SET0(it);

	it->fi_type                = itype;
	it->fi_fop                 = fop;
	it->fi_stack[0].ff_xc_type = *fop->f_type->ft_xc_type;
}

void c2_fit_fini(struct c2_fit *it)
{
	/* nothing to do even if the cursor is destroyed before iteration loop
	   is over. */
}

int c2_fit_yield(struct c2_fit *it, struct c2_fit_yield *yield)
{
	/*
	 * Cursor state machine.
	 *
	 * The code is ugly. Co-routines would make it simpler.
	 */

	while (1) {
		/* top of cursor stack. This is invalidated when it->fi_depth is
		   modified. */
		struct c2_fit_frame            *top;
		/* field type of the current nesting level. This is invalidated
		   when it->fi_depth is modified. */
		const struct c2_xcode_type     *xct;
		/* a field within the current nesting level, which the cursor is
		   positioned over. This is invalidated when either stack depth
		   or top->ff_pos changes. */
		const struct field_fit_data    *el;

		top   = fit_top(it);
		xct   = top->ff_xc_type;
		el    = top_field(it);

		C2_ASSERT(c2_fit_invariant(it));
		/*
		 * On an entry to the loop body the cursor is positioned "after"
		 * the last yielded position or on the first position.
		 *
		 * First, advance it to the next valid position, if necessary,
		 * or complete the iteration.
		 */
		switch (xct->xct_aggr) {
		case C2_XA_TYPEDEF:
		case C2_XA_ATOM:
		case C2_XA_RECORD:
			/*
			 * For these aggregation types nothing has to be done:
			 * either the position after last returned one is valid,
			 * or the iteration is over.
			 */
			break;
		case C2_XA_UNION:
			/*
			 * only one branch of union is actually present in the
			 * fop. Check whether this branch is among the fields
			 * this cursor has to iterate over.
			 *
			 * Once this check has been executed, there is no need
			 * to repeat it (on the same level of nesting), because
			 * no further matches can be found. This is what
			 * "u_branch" is for.
			 */
			if (top->ff_u.u_branch) {
				/*
				 * If check was already tried, move to the last
				 * position.
				 */
				top->ff_pos = data_get(xct)->ff_nr;
				C2_ASSERT(!el->ffd_valid);
			} else {
				tag_find(it);
				top->ff_u.u_branch = true;
			}
			el = top_field(it);
			break;
		case C2_XA_SEQUENCE: {
			uint32_t seq_length;

			if (el->ffd_valid)
				break;
			/*
			 * If end of the sequence element is reached, go to the
			 * next element, if any.
			 *
			 * Sequence length is stored in the first 4 bytes of an
			 * FFA_SEQUENCE field.
			 *
			 * XXX if necessary, sequence length can be stored in
			 *     the iterator stack (similarly for a union tag) to
			 *     avoid repeated drills.
			 */
			seq_length = *(uint32_t *)drill(it, it->fi_depth - 1);

			if (top->ff_u.u_index + 1 < seq_length) {
				top->ff_u.u_index++;
				top->ff_pos = 0;
				el = top_field(it);
			}
			break;
		}
		default:
			C2_IMPOSSIBLE("Invalid aggregation type.");
		}

		C2_ASSERT(c2_fit_invariant(it));

		/*
		 * The switch above either placed the cursor over a valid
		 * position, or reached the end of the current level.
		 */

		/* If end of the field list is reached pop the stack head. */
		if (!el->ffd_valid) {
			if (it->fi_depth-- == 0)
				/*
				 * The stack is empty, the iteration is over.
				 */
				return 0;
			fit_top(it)->ff_pos++;
			continue;
		}

		/* The cursor is positioned over a valid position. */
		C2_ASSERT(el->ffd_valid);
		if (el->ffd_recurse) {
			const struct c2_xcode_type *xc_type;

			/* push next level onto the stack. */
			xc_type = xct->xct_child[el->ffd_fieldno].xf_type;
			++it->fi_depth;
			top = fit_top(it);
			C2_SET0(top);
			top->ff_xc_type = xc_type;
			C2_ASSERT(top_field(it)->ffd_valid);
			continue;
		}

		C2_ASSERT(c2_fit_invariant(it));
		/* The cursor is over a valid leaf position. */
		yield->fy_val.ffi_fop   = it->fi_fop;
		yield->fy_val.ffi_field = &xct->xct_child[el->ffd_fieldno];
		yield->fy_val.ffi_val   = drill(it, it->fi_depth);
		yield->fy_bits          = el->ffd_bits;
		top->ff_pos++;
		return +1;
	}
}

/**
   Destructor for fop iterator decorator.
 */
static void ftype_fit_fini(void *arg)
{
	struct ftype_fit *data = arg;
	int               i;

	C2_PRE(ftype_fit_invariant(data));
	for (i = 0; i < ARRAY_SIZE(data->ff_present); ++i)
		c2_free(data->ff_present[i]);
	c2_free(data);
}

static struct c2_fop_decorator fit_dec = {
	.dec_name      = "fit-dec",
	.dec_type_fini = ftype_fit_fini
};

/**
   Returns true iff given field is "watched" by the given iterator type.

   Additionally, if the field is watched, populate "bits" with the corresponding
   cumulative watch-bits.
 */
static bool has_watches(struct c2_fit_type *itype,
			const struct c2_xcode_field *child, uint64_t *bits)
{
	struct c2_fit_watch *watch;
	struct c2_fit_mod   *mod;

	if (strcmp(itype->fit_name, "fop-all-object") == 0)
                return true;

	*bits = 0;
	c2_tl_for(wat, &itype->fit_watch, watch) {
		if (watch->fif_xct == child->xf_type) {
			*bits = watch->fif_bits;
			c2_tl_for(mod, &watch->fif_mod, mod) {
				if (mod->fm_field == child) {
					*bits |= mod->fm_bits_add;
					*bits &= ~mod->fm_bits_sub;
				}
			} c2_tl_endfor;
			return true;
		}
	} c2_tl_endfor;
	return false;
}

int c2_fop_xct_fit(struct c2_xcode_type *xct)
{
	struct ftype_fit *data;
	int               i;
	int               j;
	int               t;

	/*
	 * Allocate "data" and populate it.
	 */
	C2_ALLOC_PTR(data);
	if (data == NULL)
		return -ENOMEM;

	c2_fop_type_decoration_set(xct, &fit_dec, data);
	data->ff_nr = xct->xct_nr;
	for (t = 0; t < ARRAY_SIZE(data->ff_present); ++t) {
		struct c2_fit_type *itype;
                bool   all_watches = false;

		itype = fits[t];
		if (itype == NULL)
			continue;

                if (strcmp(itype->fit_name, "fop-all-object") == 0)
                        all_watches = true;

		C2_ALLOC_ARR(data->ff_present[t], data->ff_nr + 1);
		if (data->ff_present[t] == NULL)
			return -ENOMEM;

		for (i = j = 0; i < data->ff_nr; ++i) {
			const struct c2_xcode_field *child;
			struct field_fit_data       *el;
			const struct ftype_fit      *child_data;

			child = &xct->xct_child[i];
			el    = &data->ff_present[t][j];

			/*
			 * Search for the watches on this field.
			 */
			if (has_watches(itype, child, &el->ffd_bits)) {
				el->ffd_valid   = true;
				el->ffd_fieldno = i;
				++j;
				/*
				 * Do not short-cut the loop: fall through to
				 * check that no sub-fields of a watched field
				 * are watched.
				 */
			}

			child_data = data_get(child->xf_type);
			/*
			 * If any sub-field is watched...
			 */
			if (!all_watches && child_data != NULL &&
			    child_data->ff_present[t][0].ffd_valid) {
				/* ... the field itself isn't. */
				C2_ASSERT(!el->ffd_valid);
				el->ffd_valid   = true;
				el->ffd_recurse = true;
				el->ffd_fieldno = i;
				++j;
			}
		}
	}
	C2_POST(ftype_fit_invariant(data));
	return 0;
}

/*
 * Helpers.
 */

/**
   Returns fop iterators decoration, associated with a fop field type.
 */
static const struct ftype_fit *data_get(const struct c2_xcode_type *xct)
{
	return c2_fop_type_decoration_get(xct, &fit_dec);
}

/**
   Returns a top element of the iterator stack.
 */
static struct c2_fit_frame *fit_top(struct c2_fit *it)
{
	return &it->fi_stack[it->fi_depth];
}

/**
   Returns the descriptor of a field the given frame is positioned over.
 */
static const struct field_fit_data *
frame_field(const struct c2_fit *it, const struct c2_fit_frame *frame)
{
	const struct ftype_fit *data;

	data = data_get(frame->ff_xc_type);
	C2_ASSERT(data != NULL);
	C2_ASSERT(frame->ff_pos <= data->ff_nr);
	return &data->ff_present[it->fi_type->fit_index][frame->ff_pos];
}

/**
   Returns the descriptor of a field the iterator is positioned over.
 */
static const struct field_fit_data *top_field(struct c2_fit *it)
{
	return frame_field(it, fit_top(it));
}

/**
   Returns the pointer to the instance of a field the iterator is over.
 */
#if 0
static void *drill(const struct c2_fit *it, int depth)
{
	int                        i;
	void                      *mark;
	const struct c2_fit_frame *frame;

	C2_PRE(depth <= it->fi_depth);
	C2_PRE(c2_fit_invariant(it));

	frame = &it->fi_stack[0];
	for (mark = c2_fop_data(it->fi_fop), i = 0; i <= depth; ++i, ++frame)
		mark = c2_fop_type_field_addr(frame->ff_type, mark,
					      frame_field(it,
							  frame)->ffd_fieldno,
					      frame->ff_u.u_index);
	return mark;
}
#else
static void *drill(const struct c2_fit *it, int depth)
{
	int                        i;
	void                      *mark;
	const struct c2_fit_frame *frame;

	C2_PRE(depth <= it->fi_depth);
	C2_PRE(c2_fit_invariant(it));

	frame = &it->fi_stack[0];
	for (mark = c2_fop_data(it->fi_fop), i = 0; i <= depth; ++i, ++frame) {
		mark = c2_xcode_addr(&(struct c2_xcode_obj){frame->ff_xc_type,
					                    mark},
			             frame_field(it, frame)->ffd_fieldno,
			             frame->ff_u.u_index);
	}
	return mark;
}
#endif

/**
   Assuming that current nesting level of the iterator is a union field type,
   check whether a union branch actually present in the fop is among watched
   fields.
 */
static bool tag_find(struct c2_fit *it)
{
	uint32_t                    tag;
	struct field_fit_data      *el_array;
	struct field_fit_data      *el;
	const struct c2_xcode_type *xct;
	struct c2_fit_frame        *top;

	top   = fit_top(it);
	xct   = top->ff_xc_type;

	C2_PRE(xct->xct_aggr == C2_XA_UNION);
	C2_PRE(c2_fit_invariant(it));

	/*
	 * Union discriminant tag is stored in the first
	 * 4 bytes of an FFA_UNION field.
	 */
	tag      = *(uint32_t *)drill(it, it->fi_depth - 1);
	el_array = data_get(xct)->ff_present[it->fi_type->fit_index];

	for (el = &el_array[top->ff_pos]; el->ffd_valid; ++el, ++top->ff_pos) {
		struct c2_xcode_field field;

		C2_ASSERT(el->ffd_fieldno < xct->xct_nr);
		field = xct->xct_child[el->ffd_fieldno];
		if (field.xf_tag == tag)
			return true;
	}
	C2_POST(c2_fit_invariant(it));
	return false;
}

/*
 * Standard fop iterator types.
 */

static struct c2_fit_type fop_object_itype = {
	.fit_name  = "fop-object",
	.fit_index = -1
};

static struct c2_fit_watch fop_object_watch = {
	.fif_bits = 0
};

/*
 * Special FOP Iterator
 */
static struct c2_fit_type fop_all_object_itype = {
        .fit_name  = "fop-all-object",
        .fit_index = -1
};

void c2_fop_object_init(const struct c2_xcode_type *fid_xc_type)
{
	const struct c2_xcode_type *xct;

	xct = fid_xc_type;
	C2_PRE(xct != NULL);
	C2_PRE(xct->xct_nr == 2);
	C2_PRE(xct->xct_sizeof == 2 * sizeof(uint64_t));

	fop_object_watch.fif_xct   = xct;
	fop_object_watch.fif_bits  = C2_FOB_LOAD;
	c2_fop_itype_watch_add(&fop_object_itype, &fop_object_watch);
}

void c2_fop_object_fini(void)
{
	wat_tlist_del(&fop_object_watch);
}

void c2_fop_object_it_init(struct c2_fit *it, struct c2_fop *fop)
{
	c2_fit_init(it, &fop_object_itype, fop);
}

void c2_fop_object_it_fini(struct c2_fit *it)
{
	C2_PRE(it->fi_type == &fop_object_itype);
	c2_fit_fini(it);
}

int c2_fop_object_it_yield(struct c2_fit *it,
			   struct c2_fid *fid, uint64_t *bits)
{
	struct c2_fit_yield yield;
	int                 result;

	C2_PRE(it->fi_type == &fop_object_itype);
	result = c2_fit_yield(it, &yield);
	if (result > 0) {
		*fid = *(struct c2_fid *)yield.fy_val.ffi_val;
		*bits = yield.fy_bits;
	}
	return result;
}

void c2_fits_init(void)
{
	c2_fop_decorator_register(&fit_dec);
	c2_fop_itype_init(&fop_object_itype);
        c2_fop_itype_init(&fop_all_object_itype);
}

void c2_fits_fini(void)
{
	c2_fop_itype_fini(&fop_object_itype);
        c2_fop_itype_fini(&fop_all_object_itype);
}

void c2_fop_all_object_it_init(struct c2_fit *it, struct c2_fop *fop)
{
        c2_fit_init(it, &fop_all_object_itype, fop);
}

void c2_fop_all_object_it_fini(struct c2_fit *it)
{
        C2_PRE(it->fi_type == &fop_all_object_itype);
        c2_fit_fini(it);
}

void c2_fop_it_reset(struct c2_fit *it)
{
        struct c2_fit_frame *top;

        it->fi_depth = 0;
        top = fit_top(it);
        top->ff_pos = 0;
}

/** @} end of fop group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
