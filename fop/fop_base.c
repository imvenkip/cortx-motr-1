/* -*- C -*- */

#include "lib/cdefs.h"  /* C2_EXPORTED */
#include "lib/memory.h"
#include "lib/misc.h"   /* C2_SET0 */
#include "lib/mutex.h"
#include "lib/vec.h"
#include "fop/fop_base.h"
#include "fop/fop_iterator.h"

/**
   @addtogroup fop
   @{
 */

int  c2_fop_field_type_prepare  (struct c2_fop_field_type *ftype);
void c2_fop_field_type_unprepare(struct c2_fop_field_type *ftype);

/*
 * Imported either from fop/fop.c or from fop/rt/stub.c
 */
int  fop_fol_type_init(struct c2_fop_type *fopt);
void fop_fol_type_fini(struct c2_fop_type *fopt);

static const struct c2_fol_rec_type_ops c2_fop_fol_default_ops;

const struct c2_addb_ctx_type c2_fop_addb_ctx = {
	.act_name = "fop"
};

static const struct c2_addb_ctx_type c2_fop_type_addb_ctx = {
	.act_name = "fop-type"
};

static const struct c2_addb_loc c2_fop_addb_loc = {
	.al_name = "fop"
};

static struct c2_mutex fop_types_lock;
static struct c2_tl    fop_types_list;

C2_TL_DESCR_DEFINE(ft, "fop types", static, struct c2_fop_type,
		   ft_linkage,	ft_magix,
		   0xba11ab1ea5111dae /* bailable asilidae */,
		   0xd15ea5e0fed1f1ce /* disease of edifice */);

C2_TL_DEFINE(ft, static, struct c2_fop_type);

/**
   Used to check that no new fop iterator types are registered once a fop type
   has been built.
 */
bool fop_types_built = false;

void c2_fop_field_type_fini(struct c2_fop_field_type *t)
{
	size_t i;

	c2_fop_field_type_unprepare(t);
	if (t->fft_child != NULL) {
		for (i = 0; i < t->fft_nr; ++i) {
			if (t->fft_child[i] != NULL)
				c2_free(t->fft_child[i]);
		}
		c2_free(t->fft_child);
		t->fft_child = NULL;
	}
}
C2_EXPORTED(c2_fop_field_type_fini);

void c2_fop_type_fini(struct c2_fop_type *fopt)
{
	fop_fol_type_fini(fopt);
	if (fopt->ft_top != NULL) {
		c2_mutex_lock(&fop_types_lock);
		C2_ASSERT(&fopt->ft_rpc_item_type != NULL);
		c2_rpc_item_type_deregister(&fopt->ft_rpc_item_type);
		ft_tlink_del_fini(fopt);
		fopt->ft_magix = 0;
		c2_mutex_unlock(&fop_types_lock);
	}
	if (fopt->ft_fmt != NULL) {
		c2_fop_type_format_fini(fopt->ft_fmt);
		fopt->ft_fmt = NULL;
	}
	c2_addb_ctx_fini(&fopt->ft_addb);
}
C2_EXPORTED(c2_fop_type_fini);


int c2_fop_type_build(struct c2_fop_type *fopt)
{
	int                        result;
	struct c2_fop_type_format *fmt;

	C2_PRE(fopt->ft_top == NULL);

	fmt    = fopt->ft_fmt;
	result = c2_fop_type_format_parse(fmt);
	if (result == 0) {
		result = fop_fol_type_init(fopt);
		if (result == 0) {
			fopt->ft_top = fmt->ftf_out;
			result =
			c2_rpc_item_type_register(&fopt->ft_rpc_item_type);
			c2_addb_ctx_init(&fopt->ft_addb,
					 &c2_fop_type_addb_ctx,
					 &c2_addb_global_ctx);
			c2_mutex_lock(&fop_types_lock);
			ft_tlink_init_at(fopt, &fop_types_list);
			c2_mutex_unlock(&fop_types_lock);
		}
		if (result != 0)
			c2_fop_type_fini(fopt);
	}
	fop_types_built = true;
	return result;
}
C2_EXPORTED(c2_fop_type_build);

int c2_fop_type_build_nr(struct c2_fop_type **fopt, int nr)
{
	int i;
	int result;

	for (result = 0, i = 0; i < nr; ++i) {
		result = c2_fop_type_build(fopt[i]);
		if (result != 0) {
			c2_fop_type_fini_nr(fopt, i);
			break;
		}
	}
	return result;
}
C2_EXPORTED(c2_fop_type_build_nr);

#if 0
struct c2_fop_type *c2_fop_type_search(uint32_t opcode)
{
	struct c2_fop_type *fop_type;

	c2_mutex_lock(&fop_types_lock);
	c2_tlist_for(&ft_tl, &fop_types_list, fop_type) {
		if (fop_type->ft_code == opcode)
			break;
	} c2_tlist_endfor;
	c2_mutex_unlock(&fop_types_lock);
	return fop_type;
}
C2_EXPORTED(c2_fop_type_search);
#endif

void c2_fop_type_fini_nr(struct c2_fop_type **fopt, int nr)
{
	int i;

	for (i = 0; i < nr; ++i)
		c2_fop_type_fini(fopt[i]);
}
C2_EXPORTED(c2_fop_type_fini_nr);

struct c2_fop_memlayout atom_void_memlayout = {
	.fm_uxdr   = (xdrproc_t)xdr_void,
	.fm_sizeof = 0
};

struct c2_fop_field_type C2_FOP_TYPE_VOID = {
	.fft_aggr = FFA_ATOM,
	.fft_name = "void",
	.fft_u = {
		.u_atom = {
			.a_type = FPF_VOID
		}
	},
	.fft_layout = &atom_void_memlayout
};
C2_EXPORTED(C2_FOP_TYPE_VOID);

struct c2_fop_memlayout atom_byte_memlayout = {
	.fm_uxdr   = (xdrproc_t)xdr_char,
	.fm_sizeof = 1
};

struct c2_fop_field_type C2_FOP_TYPE_BYTE = {
	.fft_aggr = FFA_ATOM,
	.fft_name = "byte",
	.fft_u = {
		.u_atom = {
			.a_type = FPF_BYTE
		}
	},
	.fft_layout = &atom_byte_memlayout
};
C2_EXPORTED(C2_FOP_TYPE_BYTE);

struct c2_fop_memlayout atom_u32_memlayout = {
	.fm_uxdr   = (xdrproc_t)xdr_uint32_t,
	.fm_sizeof = 4
};

struct c2_fop_field_type C2_FOP_TYPE_U32 = {
	.fft_aggr = FFA_ATOM,
	.fft_name = "u32",
	.fft_u = {
		.u_atom = {
			.a_type = FPF_U32
		}
	},
	.fft_layout = &atom_u32_memlayout
};
C2_EXPORTED(C2_FOP_TYPE_U32);

struct c2_fop_memlayout atom_u64_memlayout = {
	.fm_uxdr   = (xdrproc_t)xdr_uint64_t,
	.fm_sizeof = 8
};

struct c2_fop_field_type C2_FOP_TYPE_U64 = {
	.fft_aggr = FFA_ATOM,
	.fft_name = "u64",
	.fft_u = {
		.u_atom = {
			.a_type = FPF_U64
		}
	},
	.fft_layout = &atom_u64_memlayout
};
C2_EXPORTED(C2_FOP_TYPE_U64);

int c2_fops_init(void)
{
	ft_tlist_init(&fop_types_list);
	c2_mutex_init(&fop_types_lock);
	c2_fits_init();
        c2_fits_all_init();
	c2_fop_field_type_prepare(&C2_FOP_TYPE_VOID);
	c2_fop_field_type_prepare(&C2_FOP_TYPE_BYTE);
	c2_fop_field_type_prepare(&C2_FOP_TYPE_U32);
	c2_fop_field_type_prepare(&C2_FOP_TYPE_U64);
	return 0;
}

void c2_fops_fini(void)
{
	c2_fop_field_type_unprepare(&C2_FOP_TYPE_U64);
	c2_fop_field_type_unprepare(&C2_FOP_TYPE_U32);
	c2_fop_field_type_unprepare(&C2_FOP_TYPE_BYTE);
	c2_fop_field_type_unprepare(&C2_FOP_TYPE_VOID);
	c2_fits_fini();
        c2_fits_all_fini();
	c2_mutex_fini(&fop_types_lock);
	ft_tlist_fini(&fop_types_list);
}
C2_EXPORTED(c2_fops_fini);

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
