/* -*- C -*- */

#define _GNU_SOURCE /* asprintf */
#include <stdio.h>
#include <stdlib.h> /* free(3) */

#include "fop/fop.h"
#include "lib/memory.h"
#include "lib/assert.h"

/**
   @addtogroup fop
   @{
 */

struct c_fop_decor {
	char       *d_type;
	char       *d_prefix;
	size_t      d_size;
	bool        d_varsize;
	bool        d_kanbelast;
	bool        d_kreply_prepare;
	union {
		struct {
			char *d_count;
			char *d_pgoff;
		} d_sequence;
	} u;
};

struct c_fop_field_decor {
	char *fd_fmt;
	char *fd_fmt_ptr;
};

static void type_decor_free(void *val)
{
	struct c_fop_decor *dec = val;

	free(dec->d_type);
	free(dec->d_prefix);
	free(dec->u.d_sequence.d_count);
	free(dec->u.d_sequence.d_pgoff);
	c2_free(dec);
}

static void field_decor_free(void *val)
{
	struct c_fop_field_decor *fdec = val;

	free(fdec->fd_fmt);
	free(fdec->fd_fmt_ptr);
	c2_free(fdec);
}

static struct c2_fop_decorator comp_dec = {
	.dec_name       = "comp",
	.dec_type_fini  = type_decor_free,
	.dec_field_fini = field_decor_free
};

static inline struct c_fop_decor *TD(const struct c2_fop_field_type *ftype)
{
	return c2_fop_type_decoration_get(ftype, &comp_dec);
}

static inline struct c_fop_field_decor *FD(const struct c2_fop_field *field)
{
	return c2_fop_field_decoration_get(field, &comp_dec);
}

static const char *atom_name[] = {
	[FPF_VOID] = "c2_fop_void_t",
	[FPF_BYTE] = "char",
	[FPF_U32]  = "uint32_t",
	[FPF_U64]  = "uint64_t",
};

static void type_prefix(const char *name, char *buf)
{
	*(buf++) = *(name++);
	while (*name != 0) {
		if (*(name++) == '_')
			*(buf++) = *name;
	}
	*buf = 0;
}

#define ASPRINTF(...)				\
({						\
	int rc;					\
						\
	rc = asprintf(__VA_ARGS__);		\
	C2_ASSERT(rc > 0);			\
})

static void type_decorate(struct c2_fop_field_type *ftype)
{
	struct c_fop_decor *dec;
	const char         *fmt;
	const char         *arg;
	size_t              i;
	char                prefix[32];

	enum c2_fop_field_primitive_type atype;

	if (TD(ftype) != NULL)
		return;

	C2_ALLOC_PTR(dec);
	C2_ASSERT(dec != NULL);

	c2_fop_type_decoration_set(ftype, &comp_dec, dec);
	arg = ftype->fft_name;
	fmt = "%s";

	switch (ftype->fft_aggr) {
	case FFA_UNION:
		dec->d_kanbelast = true;
	case FFA_SEQUENCE:
		dec->d_varsize = true;
	case FFA_RECORD:
		fmt = "struct %s";
	case FFA_TYPEDEF:
		break;
	case FFA_ATOM:
		atype = ftype->fft_u.u_atom.a_type;
		arg = atom_name[atype];
		dec->d_kanbelast = true;
		break;
	default:
		C2_IMPOSSIBLE("Wrong fop type aggregation");
	}

	ASPRINTF(&dec->d_type, fmt, arg);

	type_prefix(ftype->fft_name, prefix);
	ASPRINTF(&dec->d_prefix, "%s", prefix);

	if (ftype->fft_aggr == FFA_SEQUENCE) {
		ASPRINTF(&dec->u.d_sequence.d_count, "%s_count", prefix);
		ASPRINTF(&dec->u.d_sequence.d_pgoff, "%s_pgoff", prefix);
	}

	for (i = 0; i < ftype->fft_nr; ++i) {
		struct c2_fop_field *f;
		struct c_fop_field_decor *fd;
		struct c_fop_decor *fdec;

		f = ftype->fft_child[i];
		C2_ALLOC_PTR(fd);
		C2_ASSERT(fd != NULL);


		c2_fop_field_decoration_set(f, &comp_dec, fd);
		if (TD(f->ff_type) == NULL)
			type_decorate(f->ff_type);

		fdec = TD(f->ff_type);

		ASPRINTF(&fd->fd_fmt, "%-20s %s", fdec->d_type, f->ff_name);
		ASPRINTF(&fd->fd_fmt_ptr, "%-19s *%s", 
			 fdec->d_type, f->ff_name);
		dec->d_varsize |= fdec->d_varsize;

		switch (ftype->fft_aggr) {
		case FFA_SEQUENCE:
			dec->d_kanbelast      = f->ff_type == &C2_FOP_TYPE_BYTE;
			dec->d_kreply_prepare = dec->d_kanbelast;
			break;
		case FFA_UNION:
			dec->d_kanbelast     &= fdec->d_kanbelast;
			break;
		case FFA_TYPEDEF:
		case FFA_RECORD:
			dec->d_size          += fdec->d_size;
			dec->d_kanbelast      = fdec->d_kanbelast;
			dec->d_kreply_prepare = fdec->d_kreply_prepare;
			break;
		case FFA_ATOM:
			C2_IMPOSSIBLE("Primitive type has children?");
			break;
		default:
			C2_IMPOSSIBLE("Wrong fop type aggregation");
		}
	}
	C2_ASSERT(ergo(!dec->d_varsize, dec->d_kanbelast));
}

static void memlayout(struct c2_fop_field_type *ftype, const char *where)
{
	size_t i;
	const char *prefix;

	prefix = ftype->fft_aggr == FFA_UNION ? "u." : "";

	printf("struct c2_fop_memlayout %s_memlayout = {\n"
	       "\t.fm_sizeof = sizeof (%s),\n",
	       ftype->fft_name, TD(ftype)->d_type);
	if (where[0] == 'u')
		printf("\t.fm_uxdr = (xdrproc_t)uxdr_%s,\n", ftype->fft_name);
	printf("\t.fm_child = {\n");
	for (i = 0; i < ftype->fft_nr; ++i) {
		struct c2_fop_field *f;
		struct c_fop_field_decor *fd;

		f  = ftype->fft_child[i];
		fd = FD(f);
		printf("\t\t{ ");
		if (f->ff_name[0] != 0)
			printf("offsetof(%s, %s%s)", TD(ftype)->d_type,
			       i ? prefix : "", f->ff_name);
		else
			printf("0 /* %s */", f->ff_name);
		printf(" },\n");
	}
	printf("\t}\n};\n\n");
}

static void body_cdef(struct c2_fop_field_type *ftype, 
		      int start, int indent, bool tags)
{
	static const char         ruler[] = "\t\t\t\t\t\t\t\t\t\t\t\t\t";
	size_t                    i;
	struct c_fop_field_decor *fd;

	for (i = start; i < ftype->fft_nr; ++i) {
		fd = FD(ftype->fft_child[i]);
		printf("%*.*s%s;", indent, indent, ruler, fd->fd_fmt);
		if (tags)
			printf("\t/* case %i */", ftype->fft_child[i]->ff_tag);
		printf("\n");
	}
}

static void union_cdef(struct c2_fop_field_type *ftype)
{
	printf("%s {\n\t%s;\n\tunion {\n", TD(ftype)->d_type, 
	       FD(ftype->fft_child[0])->fd_fmt);
	body_cdef(ftype, 1, 2, true);
	printf("\t} u;\n};\n\n");
}

static void record_cdef(struct c2_fop_field_type *ftype)
{
	printf("%s {\n", TD(ftype)->d_type);
	body_cdef(ftype, 0, 1, false);
	printf("};\n\n");
}

static void sequence_cdef(struct c2_fop_field_type *ftype)
{
	struct c_fop_field_decor *fd;

	fd = FD(ftype->fft_child[0]);
	printf("%s {\n\t%-20s %s;\n\t%s;\n};\n\n",
	       TD(ftype)->d_type, "uint32_t", TD(ftype)->u.d_sequence.d_count,
	       fd->fd_fmt_ptr);
}

static void typedef_cdef(struct c2_fop_field_type *ftype)
{
	printf("typedef %s %s;\n\n", TD(ftype->fft_child[0]->ff_type)->d_type,
	       ftype->fft_name);
}

/*
 * Kernel part.
 *
 * Only very simple data layouts are supported at the moment: a fop type must
 * have a (possibly empty) fixed size part optionally followed by a variable
 * size byte array (a buffer).
 */

static void sequence_kdef(struct c2_fop_field_type *ftype)
{
	struct c_fop_decor       *td;
	struct c_fop_field_decor *fd;

	td = TD(ftype);
	fd = FD(ftype->fft_child[0]);
	printf("%s {\n\tuint32_t %s;\n",
	       td->d_type, td->u.d_sequence.d_count);
	if (td->d_kanbelast) {
		printf("\tuint32_t %s;\n\tstruct page **%s;", 
		       td->u.d_sequence.d_pgoff, ftype->fft_child[0]->ff_name);
	} else
		printf("\t%s;", fd->fd_fmt_ptr);
	printf("\n};\n\n");
}

struct c_ops {
	void (*op)(struct c2_fop_field_type *ftype);
};

static const struct c_ops cdef_ops[FFA_NR] = {
	[FFA_UNION]    = { union_cdef },
	[FFA_RECORD]   = { record_cdef },
	[FFA_SEQUENCE] = { sequence_cdef },
	[FFA_TYPEDEF]  = { typedef_cdef }
};

static const struct c_ops kdef_ops[FFA_NR] = {
	[FFA_UNION]    = { union_cdef },
	[FFA_RECORD]   = { record_cdef },
	[FFA_SEQUENCE] = { sequence_kdef },
	[FFA_TYPEDEF]  = { typedef_cdef }
};

int c2_fop_comp_udef(struct c2_fop_field_type *ftype)
{
	type_decorate(ftype);
	cdef_ops[ftype->fft_aggr].op(ftype);
	printf("extern struct c2_fop_memlayout %s_memlayout;\n",
	       ftype->fft_name);
	return 0;
}

int c2_fop_comp_kdef(struct c2_fop_field_type *ftype)
{
	type_decorate(ftype);
	kdef_ops[ftype->fft_aggr].op(ftype);
	printf("extern struct c2_fop_memlayout %s_memlayout;\n\n", 
	       ftype->fft_name);
	return 0;
}

int c2_fop_comp_ulay(struct c2_fop_field_type *ftype)
{
	type_decorate(ftype);
	printf("static bool_t uxdr_%s(XDR *xdrs, %s *%s)\n{\n"
	       "\textern struct c2_fop_type_format %s_tfmt;\n"
	       "\treturn c2_fop_type_uxdr(%s_tfmt.ftf_out, xdrs, %s);\n}\n\n",
	       ftype->fft_name, TD(ftype)->d_type, TD(ftype)->d_prefix,
	       ftype->fft_name,
	       ftype->fft_name, TD(ftype)->d_prefix);
	memlayout(ftype, "u");
	return 0;
}

int c2_fop_comp_klay(struct c2_fop_field_type *ftype)
{
	type_decorate(ftype);
	memlayout(ftype, "k");
	return 0;
}

int c2_fop_comp_init(void)
{
	c2_fop_decorator_register(&comp_dec);
	return 0;
}

void c2_fop_comp_fini(void)
{
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
