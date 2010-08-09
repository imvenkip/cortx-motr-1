/* -*- C -*- */

#include "lib/memory.h"
#include "lib/list.h"
#include "lib/mutex.h"
#include "lib/vec.h"
#include "fop/fop.h"

/**
   @addtogroup fop
   @{
 */

int  c2_fop_field_type_prepare  (struct c2_fop_field_type *ftype);
void c2_fop_field_type_unprepare(struct c2_fop_field_type *ftype);

static const struct c2_addb_ctx_type c2_fop_addb_ctx = {
	.act_name = "fop"
};

static const struct c2_addb_ctx_type c2_fop_type_addb_ctx = {
	.act_name = "fop-type"
};

static const struct c2_addb_loc c2_fop_addb_loc = {
	.al_name = "fop"
};

static struct c2_mutex fop_types_lock;
static struct c2_list  fop_types_list;

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
EXPORT_SYMBOL(c2_fop_field_type_fini);

struct c2_fop *c2_fop_alloc(struct c2_fop_type *fopt, void *data)
{
	struct c2_fop *fop;

	C2_ALLOC_PTR(fop);
	if (fop != NULL) {
		c2_bcount_t nob;

		fop->f_type = fopt;
		nob = fopt->ft_top->fft_layout->fm_sizeof;
		if (data == NULL)
			data = c2_alloc(nob);
		if (data != NULL) {
			fop->f_data.fd_data = data;
			c2_addb_ctx_init(&fop->f_addb, &c2_fop_addb_ctx,
					 &fopt->ft_addb);
		} else {
			c2_free(fop);
			fop = NULL;
		}
	}
	return fop;
}
EXPORT_SYMBOL(c2_fop_alloc);

void c2_fop_free(struct c2_fop *fop)
{
	c2_addb_ctx_fini(&fop->f_addb);
	if (fop != NULL) {
		c2_free(fop->f_data.fd_data);
		c2_free(fop);
	}
}
EXPORT_SYMBOL(c2_fop_free);

void *c2_fop_data(struct c2_fop *fop)
{
	return fop->f_data.fd_data;

}
EXPORT_SYMBOL(c2_fop_data);

void c2_fop_type_fini(struct c2_fop_type *fopt)
{
	if (fopt->ft_fmt != NULL) {
		c2_fop_type_format_fini(fopt->ft_fmt);
		fopt->ft_fmt = NULL;
	}
	if (fopt->ft_top != NULL) {
		c2_mutex_lock(&fop_types_lock);
		c2_list_del(&fopt->ft_linkage);
		c2_mutex_unlock(&fop_types_lock);
	}
	c2_addb_ctx_fini(&fopt->ft_addb);
}
EXPORT_SYMBOL(c2_fop_type_fini);

int c2_fop_type_build(struct c2_fop_type *fopt)
{
	int                        result;
	struct c2_fop_type_format *fmt;

	C2_PRE(fopt->ft_top == NULL);

	fmt    = fopt->ft_fmt;
	result = c2_fop_type_format_parse(fmt);
	if (result == 0) {
		fopt->ft_top = fmt->ftf_out;
		c2_addb_ctx_init(&fopt->ft_addb, &c2_fop_type_addb_ctx,
				 &c2_addb_global_ctx);
		c2_mutex_lock(&fop_types_lock);
		c2_list_add(&fop_types_list, &fopt->ft_linkage);
		c2_mutex_unlock(&fop_types_lock);
	}
	return result;
}
EXPORT_SYMBOL(c2_fop_type_build);

int  c2_fop_type_build_nr(struct c2_fop_type **fopt, int nr)
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
EXPORT_SYMBOL(c2_fop_type_build_nr);

void c2_fop_type_fini_nr(struct c2_fop_type **fopt, int nr)
{
	int i;

	for (i = 0; i < nr; ++i)
		c2_fop_type_fini(fopt[i]);
}
EXPORT_SYMBOL(c2_fop_type_fini_nr);

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
EXPORT_SYMBOL(C2_FOP_TYPE_VOID);

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
EXPORT_SYMBOL(C2_FOP_TYPE_BYTE);

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
EXPORT_SYMBOL(C2_FOP_TYPE_U32);

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
EXPORT_SYMBOL(C2_FOP_TYPE_U64);

int c2_fops_init(void)
{
	c2_list_init(&fop_types_list);
	c2_mutex_init(&fop_types_lock);
	c2_fop_field_type_prepare(&C2_FOP_TYPE_VOID);
	c2_fop_field_type_prepare(&C2_FOP_TYPE_BYTE);
	c2_fop_field_type_prepare(&C2_FOP_TYPE_U32);
	c2_fop_field_type_prepare(&C2_FOP_TYPE_U64);
	return 0;
}
EXPORT_SYMBOL(c2_fops_init);

void c2_fops_fini(void)
{
	c2_fop_field_type_unprepare(&C2_FOP_TYPE_U64);
	c2_fop_field_type_unprepare(&C2_FOP_TYPE_U32);
	c2_fop_field_type_unprepare(&C2_FOP_TYPE_BYTE);
	c2_fop_field_type_unprepare(&C2_FOP_TYPE_VOID);
	c2_mutex_fini(&fop_types_lock);
	c2_list_fini(&fop_types_list);
}
EXPORT_SYMBOL(c2_fops_fini);

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
