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
	char       *d_xdrproc;
	char       *d_prefix;
	size_t      d_size;
	bool        d_varsize;
	union {
		struct {
			char *d_count;
		} d_sequence;
	} u;
};

struct c_fop_field_decor {
	char *fd_fmt;
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

static const size_t atom_size[] = {
	[FPF_VOID] = 0,
	[FPF_BYTE] = 1,
	[FPF_U32]  = 4,
	[FPF_U64]  = 8,
};

static const char *atom_xdr[] = {
	[FPF_VOID] = "xdr_void",
	[FPF_BYTE] = "xdr_char",
	[FPF_U32]  = "xdr_u_int",
	[FPF_U64]  = "xdr_u_hyper",
};

void type_prefix(const char *name, char *buf)
{
	*(buf++) = *(name++);
	for (; *name != 0 && *name != 0; name++) {
		if (*name == '_')
			*(buf++) = name[1];
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
	const char         *xdr_fmt;
	const char         *arg;
	size_t              i;
	char                prefix[32];

	enum c2_fop_field_primitive_type atype;

	C2_ALLOC_PTR(dec);
	C2_ASSERT(dec != NULL);

	ftype->fft_decor = dec;
	arg = ftype->fft_name;
	fmt = "%s";
	xdr_fmt = "xdr_%s";

	switch (ftype->fft_aggr) {
	case FFA_UNION:
	case FFA_SEQUENCE:
		dec->d_varsize = true;
	case FFA_RECORD:
		fmt = "struct %s";
	case FFA_TYPEDEF:
		break;
	case FFA_ATOM:
		atype = ftype->fft_u.u_atom.a_type;
		arg = atom_name[atype];
		xdr_fmt = atom_xdr[atype];
		dec->d_size = atom_size[atype];
		break;
	}

	ASPRINTF(&dec->d_type, fmt, arg);
	ASPRINTF(&dec->d_xdrproc, xdr_fmt, ftype->fft_name);

	type_prefix(ftype->fft_name, prefix);
	ASPRINTF(&dec->d_prefix, "%s", prefix);

	if (ftype->fft_aggr == FFA_SEQUENCE)
		ASPRINTF(&dec->u.d_sequence.d_count, "%s_count", prefix);

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
			ASPRINTF(&fd->fd_fmt, "%s %%s%s", 
				 fdec->d_type, f->ff_name);
		} else {
			ASPRINTF(&fd->fd_fmt, "/* void %s */", f->ff_name);
			fd->fd_void = true;
		}

		dec->d_varsize |= fdec->d_varsize;

		switch (ftype->fft_aggr) {
		case FFA_TYPEDEF:
			dec->d_size = fdec->d_size;
		case FFA_UNION:
		case FFA_SEQUENCE:
		case FFA_ATOM:
			break;
		case FFA_RECORD:
			dec->d_size += fdec->d_size;
			break;
		}

	}
}

static void body_cdef(struct c2_fop_field_type *ftype, 
		      int start, int indent, bool tags)
{
	static const char         ruler[] = "\t\t\t\t\t\t\t\t\t\t\t\t\t";
	size_t                    i;
	struct c_fop_field_decor *fd;

	for (i = start; i < ftype->fft_nr; ++i) {
		fd = FD(ftype->fft_child[i]);
		printf("%*.*s", indent, indent, ruler);
		printf(fd->fd_fmt, "");
		printf("%s", fd->fd_void ? "" : ";");
		if (tags)
			printf("\t/* case %i */", ftype->fft_child[i]->ff_tag);
		printf("\n");
	}
}

static void union_cdef(struct c2_fop_field_type *ftype)
{
	printf("%s {\n\t", TD(ftype)->d_type);
	printf(FD(ftype->fft_child[0])->fd_fmt, "");
	printf(";\n\tunion {\n");
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
	printf("%s {\n\tuint32_t %s;\n\t", TD(ftype)->d_type, 
	       TD(ftype)->u.d_sequence.d_count);
	printf(FD(ftype->fft_child[0])->fd_fmt, "*");
	printf(";\n};\n\n");
}

static void typedef_cdef(struct c2_fop_field_type *ftype)
{
	printf("typedef %s %s;\n\n", TD(ftype->fft_child[0]->ff_type)->d_type,
	       ftype->fft_name);
}

static void xdr_head(struct c2_fop_field_type *ftype)
{
	printf("bool_t %s(XDR *xdrs, %s *%s)", TD(ftype)->d_xdrproc,
	       TD(ftype)->d_type, TD(ftype)->d_prefix);
}

static void xdr_h(struct c2_fop_field_type *ftype)
{
	xdr_head(ftype);
	printf(";\n");
}

static void union_xdr_c(struct c2_fop_field_type *ftype)
{
	size_t i;

	xdr_head(ftype);
	printf("\n{\n\tstatic struct xdr_discrim %s_discrim[] = {\n",
	       TD(ftype)->d_prefix);

	for (i = 1; i < ftype->fft_nr; ++i) {
		printf("\t\t{ .value = %i,\t\t.proc = (xdrproc_t)%s },\n",
		       ftype->fft_child[i]->ff_tag,
		       TD(ftype->fft_child[i]->ff_type)->d_xdrproc);
	}
	printf("\t\t{ .proc = NULL_xdrproc_t }\n\t};\n\n\treturn "
	       "xdr_union(xdrs, (enum_t *)&%s->%s,\n\t\t"
	       "(char *)&%s->u, %s_discrim, NULL);\n}\n\n",
	       TD(ftype)->d_prefix, ftype->fft_child[0]->ff_name,
	       TD(ftype)->d_prefix, TD(ftype)->d_prefix);
}

static void record_xdr_c(struct c2_fop_field_type *ftype)
{
	size_t i;

	xdr_head(ftype);
	printf("\n{\n\treturn\n");

	for (i = 0; i < ftype->fft_nr; ++i) {
		printf("\t\t%s(xdrs, &%s->%s) &&\n", 
		       TD(ftype->fft_child[i]->ff_type)->d_xdrproc,
		       TD(ftype)->d_prefix, ftype->fft_child[i]->ff_name);
	}
	printf("\t\t1;\n}\n\n");
}

static void sequence_xdr_c(struct c2_fop_field_type *ftype)
{
	const struct c2_fop_field_type *ctype;
	struct c_fop_decor             *cdec;
	struct c_fop_decor             *dec;

	xdr_head(ftype);
	printf("\n{\n\treturn ");

	ctype = ftype->fft_child[0]->ff_type;
	cdec  = TD(ctype);
	dec   = TD(ftype);

	if (ctype->fft_aggr == FFA_ATOM && 
	    ctype->fft_u.u_atom.a_type == FPF_BYTE) {
		printf("xdr_bytes(xdrs, &%s->%s,\n\t\t&%s->%s, ~0)",
		       dec->d_prefix, ftype->fft_child[0]->ff_name,
		       dec->d_prefix, dec->u.d_sequence.d_count);
	} else if (cdec->d_varsize) {
		printf("0 /* cannot serialise variable length type */");
	} else {
		printf("xdr_array(xdrs, (char **)&%s->%s,\n\t\t"
		       "&%s->%s, ~0, %i, (xdrproc_t)%s)",
		       dec->d_prefix, ftype->fft_child[0]->ff_name,
		       dec->d_prefix, dec->u.d_sequence.d_count,
		       (int)cdec->d_size, cdec->d_xdrproc);
	}
	printf(";\n}\n\n");
}

static void typedef_xdr_c(struct c2_fop_field_type *ftype)
{
	xdr_head(ftype);
	printf("\n{\n\treturn %s(xdrs, %s);\n}\n\n",
	       TD(ftype->fft_child[0]->ff_type)->d_xdrproc,
	       TD(ftype)->d_prefix);
}

struct c_ops {
	void (*cdef)(struct c2_fop_field_type *ftype);
	void (*xdr_c)(struct c2_fop_field_type *ftype);
	void (*xdr_h)(struct c2_fop_field_type *ftype);
};

static struct c_ops ops[] = {
	[FFA_UNION]    = { union_cdef,    union_xdr_c,    xdr_h },
	[FFA_RECORD]   = { record_cdef,   record_xdr_c,   xdr_h },
	[FFA_SEQUENCE] = { sequence_cdef, sequence_xdr_c, xdr_h },
	[FFA_TYPEDEF]  = { typedef_cdef,  typedef_xdr_c,  xdr_h }
};

int c2_fop_type_format_cdef(struct c2_fop_field_type *ftype)
{
	type_decorate(ftype);
	ops[ftype->fft_aggr].cdef(ftype);
	return 0;
}

int c2_fop_type_format_uxdr(struct c2_fop_field_type *ftype)
{
	type_decorate(ftype);
	ops[ftype->fft_aggr].xdr_h(ftype);
	return 0;
}

int c2_fop_type_format_uxdr_c(struct c2_fop_field_type *ftype)
{
	type_decorate(ftype);
	ops[ftype->fft_aggr].xdr_c(ftype);
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
