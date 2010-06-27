/* -*- C -*- */

#define _GNU_SOURCE /* asprintf */
#include <stdio.h>

#include "fop.h"
#include "fop_format.h"
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
	bool  fd_void;
};

#define TD(ftype) ((struct c_fop_decor *)((ftype)->fft_decor))
#define FD(field) ((struct c_fop_field_decor *)((field)->ff_decor))

static const char *atom_name[] = {
	[FPF_VOID] = "/* void */",
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

	if (ftype->fft_decor != NULL)
		return;

	C2_ALLOC_PTR(dec);
	C2_ASSERT(dec != NULL);

	ftype->fft_decor = dec;
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

		f->ff_decor = fd;
		if (f->ff_type->fft_decor == NULL)
			type_decorate(f->ff_type);

		fdec = TD(f->ff_type);

		if (f->ff_type != &C2_FOP_TYPE_VOID) {
			ASPRINTF(&fd->fd_fmt, "%-20s %s", 
				 fdec->d_type, f->ff_name);
			ASPRINTF(&fd->fd_fmt_ptr, "%-19s *%s", 
				 fdec->d_type, f->ff_name);
		} else {
			ASPRINTF(&fd->fd_fmt, "/* void %-9s */", f->ff_name);
			fd->fd_fmt_ptr = fd->fd_fmt;
			fd->fd_void = true;
		}

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
		}
	}
	C2_ASSERT(ergo(!dec->d_varsize, dec->d_kanbelast));
}

static void memlayout(struct c2_fop_field_type *ftype)
{
	size_t i;
	const char *prefix;

	prefix = ftype->fft_aggr == FFA_UNION ? "u." : "";

	printf("int %s_memlayout[] = {\n", ftype->fft_name);
	for (i = 0; i < ftype->fft_nr; ++i) {
		struct c2_fop_field *f;
		struct c_fop_field_decor *fd;

		f  = ftype->fft_child[i];
		fd = FD(f);
		if (!fd->fd_void && f->ff_name[0] != 0)
			printf("\toffsetof(%s, %s%s)", TD(ftype)->d_type,
			       i ? prefix : "", f->ff_name);
		else
			printf("\t0 /* %s */", f->ff_name);
		printf(",\n");
	}
	printf("};\n\n");
}

static void body_cdef(struct c2_fop_field_type *ftype, 
		      int start, int indent, bool tags)
{
	static const char         ruler[] = "\t\t\t\t\t\t\t\t\t\t\t\t\t";
	size_t                    i;
	struct c_fop_field_decor *fd;

	for (i = start; i < ftype->fft_nr; ++i) {
		fd = FD(ftype->fft_child[i]);
		printf("%*.*s%s%s", indent, indent, ruler, fd->fd_fmt, 
		       fd->fd_void ? "" : ";");
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
	printf("%s {\n\t%-20s %s;\n\t%s%s\n};\n\n",
	       TD(ftype)->d_type, "uint32_t", TD(ftype)->u.d_sequence.d_count,
	       fd->fd_fmt_ptr, fd->fd_void ? "" : ";");
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
		printf("\tuint32_t %s;\n\tstruct page *%s;", 
		       td->u.d_sequence.d_pgoff, ftype->fft_child[0]->ff_name);
	} else
		printf("\t%s%s", fd->fd_fmt_ptr, fd->fd_void ? "" : ";");
	printf("\n};\n\n");
}

struct c_ops {
	void (*op)(struct c2_fop_field_type *ftype);
};

static const struct c_ops cdef_ops[] = {
	[FFA_UNION]    = { union_cdef },
	[FFA_RECORD]   = { record_cdef },
	[FFA_SEQUENCE] = { sequence_cdef },
	[FFA_TYPEDEF]  = { typedef_cdef }
};

static const struct c_ops kdef_ops[] = {
	[FFA_UNION]    = { union_cdef },
	[FFA_RECORD]   = { record_cdef },
	[FFA_SEQUENCE] = { sequence_kdef },
	[FFA_TYPEDEF]  = { typedef_cdef }
};

int c2_fop_type_format_cdef(struct c2_fop_field_type *ftype)
{
	type_decorate(ftype);
	cdef_ops[ftype->fft_aggr].op(ftype);
	printf("extern int %s_memlayout[];\n\n", ftype->fft_name);
	return 0;
}

int c2_fop_type_format_kdef(struct c2_fop_field_type *ftype)
{
	type_decorate(ftype);
	kdef_ops[ftype->fft_aggr].op(ftype);
	printf("extern int %s_memlayout[];\n\n", ftype->fft_name);
	return 0;
}

int c2_fop_type_format_memlayout(struct c2_fop_field_type *ftype)
{
	type_decorate(ftype);
	memlayout(ftype);
	return 0;
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
