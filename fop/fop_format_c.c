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

enum {
	KXDR_ENC,
	KXDR_DEC,
	KXDR_NR
};

struct c_fop_decor {
	char       *d_type;
	char       *d_uxdrproc;
	char       *d_prefix;
	size_t      d_size;
	bool        d_varsize;
	bool        d_kanbelast;
	bool        d_kreply_prepare;
	char       *d_kxdr[KXDR_NR];
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

static const size_t atom_size[] = {
	[FPF_VOID] = 0,
	[FPF_BYTE] = 1,
	[FPF_U32]  = 4,
	[FPF_U64]  = 8,
};

static const char *atom_uxdr[] = {
	[FPF_VOID] = "xdr_void",
	[FPF_BYTE] = "xdr_char",
	[FPF_U32]  = "xdr_u_int",
	[FPF_U64]  = "xdr_u_hyper",
};

static const char *atom_kxdr[KXDR_NR][FPF_NR] = {
	[KXDR_ENC] = {
		[FPF_VOID] = "c2_kvoid_encode",
		[FPF_BYTE] = "<UNDEFINED>",
		[FPF_U32]  = "c2_ku32_encode",
		[FPF_U64]  = "c2_ku64_encode"
	},
	[KXDR_DEC] = {
		[FPF_VOID] = "c2_kvoid_decode",
		[FPF_BYTE] = "<UNDEFINED>",
		[FPF_U32]  = "c2_ku32_decode",
		[FPF_U64]  = "c2_ku64_decode"
	}
};

void type_prefix(const char *name, char *buf)
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
	const char         *uxdr_fmt;
	const char         *kxdr[KXDR_NR];
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
	uxdr_fmt = "xdr_%s";
	kxdr[KXDR_ENC] = "%s_encode";
	kxdr[KXDR_DEC] = "%s_decode";

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
		uxdr_fmt = atom_uxdr[atype];
		kxdr[KXDR_ENC] = atom_kxdr[KXDR_ENC][atype];
		kxdr[KXDR_DEC] = atom_kxdr[KXDR_DEC][atype];
		dec->d_size = atom_size[atype];
		dec->d_kanbelast = true;
		break;
	}

	ASPRINTF(&dec->d_type, fmt, arg);
	ASPRINTF(&dec->d_uxdrproc, uxdr_fmt, ftype->fft_name);
	ASPRINTF(&dec->d_kxdr[KXDR_ENC], kxdr[KXDR_ENC], ftype->fft_name);
	ASPRINTF(&dec->d_kxdr[KXDR_DEC], kxdr[KXDR_DEC], ftype->fft_name);

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

static void uxdr_head(struct c2_fop_field_type *ftype)
{
	printf("bool_t %s(XDR *xdrs, %s *%s)", TD(ftype)->d_uxdrproc,
	       TD(ftype)->d_type, TD(ftype)->d_prefix);
}

static void uxdr_h(struct c2_fop_field_type *ftype)
{
	uxdr_head(ftype);
	printf(";\n");
}

static void union_uxdr_c(struct c2_fop_field_type *ftype)
{
	size_t i;

	uxdr_head(ftype);
	printf("\n{\n\tstatic struct xdr_discrim %s_discrim[] = {\n",
	       TD(ftype)->d_prefix);

	for (i = 1; i < ftype->fft_nr; ++i) {
		printf("\t\t{ .value = %i,\t\t.proc = (xdrproc_t)%s },\n",
		       ftype->fft_child[i]->ff_tag,
		       TD(ftype->fft_child[i]->ff_type)->d_uxdrproc);
	}
	printf("\t\t{ .proc = NULL_xdrproc_t }\n\t};\n\n\treturn "
	       "xdr_union(xdrs, (enum_t *)&%s->%s,\n\t\t"
	       "(char *)&%s->u, %s_discrim, NULL);\n}\n\n",
	       TD(ftype)->d_prefix, ftype->fft_child[0]->ff_name,
	       TD(ftype)->d_prefix, TD(ftype)->d_prefix);
}

static void record_uxdr_c(struct c2_fop_field_type *ftype)
{
	size_t i;

	uxdr_head(ftype);
	printf("\n{\n\treturn\n");

	for (i = 0; i < ftype->fft_nr; ++i) {
		printf("\t\t%s(xdrs, &%s->%s) &&\n", 
		       TD(ftype->fft_child[i]->ff_type)->d_uxdrproc,
		       TD(ftype)->d_prefix, ftype->fft_child[i]->ff_name);
	}
	printf("\t\t1;\n}\n\n");
}

static void sequence_uxdr_c(struct c2_fop_field_type *ftype)
{
	const struct c2_fop_field_type *ctype;
	struct c_fop_decor             *cdec;
	struct c_fop_decor             *dec;

	uxdr_head(ftype);
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
		       (int)cdec->d_size, cdec->d_uxdrproc);
	}
	printf(";\n}\n\n");
}

static void typedef_uxdr_c(struct c2_fop_field_type *ftype)
{
	uxdr_head(ftype);
	printf("\n{\n\treturn %s(xdrs, %s);\n}\n\n",
	       TD(ftype->fft_child[0]->ff_type)->d_uxdrproc,
	       TD(ftype)->d_prefix);
}

/*
 * Kernel part.
 *
 * Only very simple data layouts are supported at the moment: a fop type must
 * have a (possibly empty) fixed size part optionally followed by a variable
 * size byte array (a buffer).
 */

static void union_kdef(struct c2_fop_field_type *ftype)
{
	printf("%s {\n\t%s;\n\tunion {\n", TD(ftype)->d_type, 
	       FD(ftype->fft_child[0])->fd_fmt);
	body_cdef(ftype, 1, 2, true);
	printf("\t} u;\n};\n\n");
}

static void record_kdef(struct c2_fop_field_type *ftype)
{
	printf("%s {\n", TD(ftype)->d_type);
	body_cdef(ftype, 0, 1, false);
	printf("};\n\n");
}

static void sequence_kdef(struct c2_fop_field_type *ftype)
{
	struct c_fop_decor       *td;
	struct c_fop_field_decor *fd;

	td = TD(ftype);
	fd = FD(ftype->fft_child[0]);
	printf("%s {\n\tuint32_t %s;\n\tuint32_t %s;\n\t",
	       td->d_type, td->u.d_sequence.d_count, td->u.d_sequence.d_pgoff);
	if (td->d_kanbelast)
		printf("struct page *%s;", ftype->fft_child[0]->ff_name);
	else
		printf("%s%s", fd->fd_fmt_ptr, fd->fd_void ? "" : ";");
	printf("\n};\n\n");
}

static void kxdr_head(struct c2_fop_field_type *ftype, int encdec)
{
	printf("int %s(struct rpc_rqst *req, uint32_t *p, void *datum)", 
	       TD(ftype)->d_kxdr[encdec]);
}

static void kxdr_reply_prepare_head(struct c2_fop_field_type *ftype)
{
	printf("bool %s_reply_prepare(struct rpc_rqst *req, %s *%s)",
	       ftype->fft_name, TD(ftype)->d_type, TD(ftype)->d_prefix);
}

static void kxdr_body(struct c2_fop_field_type *ftype)
{
	size_t i;
	struct c_fop_decor *td;

	td = TD(ftype);
	if (td->d_kanbelast) {
		for (i = 0; i < KXDR_NR; ++i) {
			kxdr_head(ftype, i);
			printf("\n{\n\t%s *%s = datum;\n"
			       "\tstruct xdr_stream xdr;\n\n"
			       "\txdr_init_%s(&xdr, &req->rq_%s_buf, p);\n"
			       "\treturn %s0(xdr, %s) ? 0 : -EIO;\n}\n\n",
			       td->d_type, td->d_prefix,
			       i == KXDR_DEC ? "decode" : "encode",
			       i == KXDR_DEC ? "rcv" : "snd",
			       td->d_kxdr[i], td->d_prefix);
		}
	}
}

static void kxdr_h(struct c2_fop_field_type *ftype)
{
	if (TD(ftype)->d_kanbelast) {
		kxdr_head(ftype, 0);
		printf(";\n");
		kxdr_head(ftype, 1);
		printf(";\n");
	} else
		printf("/* fop %s cannot be represented by kernel XDR. */\n",
		       ftype->fft_name);
	if (TD(ftype)->d_kreply_prepare) {
		kxdr_reply_prepare_head(ftype);
		printf(";\n");
	}
}

static void kxdr_head0(struct c2_fop_field_type *ftype, int encdec)
{
	struct c_fop_decor *td;

	td = TD(ftype);
	printf("static bool %s0(struct xdr_stream *xdr, %s *%s)\n{\n",
	       td->d_kxdr[encdec], ftype->fft_name, td->d_prefix);
}

static void record_kxdr_c(struct c2_fop_field_type *ftype)
{
	int t;
	size_t i;

	for (t = 0; t < KXDR_NR; ++t) {
		kxdr_head0(ftype, t);
		printf("\treturn\n");
		for (i = 0; i < ftype->fft_nr; ++i) {
			struct c2_fop_field *f;

			f = ftype->fft_child[i];
			printf("\t\t%s0(xdr, &%s->%s) &&\n",
			       TD(f->ff_type)->d_kxdr[t], 
			       TD(ftype)->d_prefix, f->ff_name);
		}
		printf("\t\t1;\n}\n\n");
	}
	kxdr_body(ftype);
	if (TD(ftype)->d_kreply_prepare) {
		kxdr_reply_prepare_head(ftype);
		printf("\n{\n\treturn %s_reply_prepare(req, &%s->%s);\n}\n\n",
		       ftype->fft_child[ftype->fft_nr - 1]->ff_type->fft_name,
		       TD(ftype)->d_prefix, 
		       ftype->fft_child[ftype->fft_nr - 1]->ff_name);
	}
}

static void sequence_kxdr_c(struct c2_fop_field_type *ftype)
{
	int t;
	struct c_fop_decor  *td;
	struct c2_fop_field *f;

	td = TD(ftype);
	f  = ftype->fft_child[0];
	for (t = 0; t < KXDR_NR; ++t) {
		kxdr_head0(ftype, t);
		printf("%s"
		       "\tbool result;\n\n"
		       "\tresult = %s0(xdr, &%s->%s)",
		       td->d_kanbelast ? "" : "\tuint32_t i;\n",
		       atom_kxdr[t][FPF_U32],
		       td->d_prefix, td->u.d_sequence.d_count);
		if (td->d_kanbelast) {
			printf(" &&\n");
			if (t == KXDR_DEC) {
				printf("\t\t xdr_inline_pages(xdr, %s->%s,\n"
				       "\t\t\t%s->%s, %s->%s);\n",
				       td->d_prefix, 
				       ftype->fft_child[0]->ff_name,
				       td->d_prefix, td->u.d_sequence.d_pgoff,
				       td->d_prefix, td->u.d_sequence.d_count);
			} else {
				printf("\t\t xdr_write_pages(xdr, %s->%s,\n"
				       "\t\t\t%s->%s, %s->%s);\n",
				       td->d_prefix, 
				       ftype->fft_child[0]->ff_name,
				       td->d_prefix, td->u.d_sequence.d_pgoff,
				       td->d_prefix, td->u.d_sequence.d_count);
			}
		} else {
			printf(";\n\tfor (i = 0; result && i < %s->%s; ++i)\n"
			       "\t\tresult &= %s0(xdr, &%s->%s[i]);\n",
			       td->d_prefix, td->u.d_sequence.d_count,
			       TD(f->ff_type)->d_kxdr[t],
			       td->d_prefix, f->ff_name);
		}
		printf("\treturn result;\n}\n\n");
	}
	kxdr_body(ftype);
	if (TD(ftype)->d_kreply_prepare) {
		kxdr_reply_prepare_head(ftype);
		printf("\n{\n\treturn %s_reply_prepare(req, &%s->%s);\n}\n\n",
		       ftype->fft_child[ftype->fft_nr - 1]->ff_type->fft_name,
		       TD(ftype)->d_prefix, 
		       ftype->fft_child[ftype->fft_nr - 1]->ff_name);
	}
}

static void typedef_kxdr_c(struct c2_fop_field_type *ftype)
{
	int t;
	struct c_fop_decor  *td;
	struct c2_fop_field *f;

	td = TD(ftype);
	f  = ftype->fft_child[0];
	for (t = 0; t < 2; ++t) {
		kxdr_head0(ftype, t);
		printf("\treturn %s0(xdr, %s);\n}\n\n",
		       f->ff_type->fft_name, td->d_prefix);
	}
	kxdr_body(ftype);
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

static const struct c_ops uxdr_h_ops[] = {
	[FFA_UNION]    = { uxdr_h },
	[FFA_RECORD]   = { uxdr_h },
	[FFA_SEQUENCE] = { uxdr_h },
	[FFA_TYPEDEF]  = { uxdr_h }
};

static const struct c_ops uxdr_c_ops[] = {
	[FFA_UNION]    = { union_uxdr_c },
	[FFA_RECORD]   = { record_uxdr_c },
	[FFA_SEQUENCE] = { sequence_uxdr_c },
	[FFA_TYPEDEF]  = { typedef_uxdr_c }
};

static const struct c_ops kdef_ops[] = {
	[FFA_UNION]    = { union_kdef },
	[FFA_RECORD]   = { record_kdef },
	[FFA_SEQUENCE] = { sequence_kdef },
	[FFA_TYPEDEF]  = { typedef_cdef } /* sic */
};

static const struct c_ops kxdr_h_ops[] = {
	[FFA_UNION]    = { kxdr_h },
	[FFA_RECORD]   = { kxdr_h },
	[FFA_SEQUENCE] = { kxdr_h },
	[FFA_TYPEDEF]  = { kxdr_h }
};

static const struct c_ops kxdr_c_ops[] = {
	[FFA_UNION]    = { kxdr_h },
	[FFA_RECORD]   = { record_kxdr_c },
	[FFA_SEQUENCE] = { sequence_kxdr_c },
	[FFA_TYPEDEF]  = { typedef_kxdr_c }
};

int c2_fop_type_format_cdef(struct c2_fop_field_type *ftype)
{
	type_decorate(ftype);
	cdef_ops[ftype->fft_aggr].op(ftype);
	return 0;
}

int c2_fop_type_format_uxdr_h(struct c2_fop_field_type *ftype)
{
	type_decorate(ftype);
	uxdr_h_ops[ftype->fft_aggr].op(ftype);
	return 0;
}

int c2_fop_type_format_uxdr_c(struct c2_fop_field_type *ftype)
{
	type_decorate(ftype);
	uxdr_c_ops[ftype->fft_aggr].op(ftype);
	return 0;
}

int c2_fop_type_format_kdef(struct c2_fop_field_type *ftype)
{
	type_decorate(ftype);
	kdef_ops[ftype->fft_aggr].op(ftype);
	return 0;
}

int c2_fop_type_format_kxdr_h(struct c2_fop_field_type *ftype)
{
	type_decorate(ftype);
	kxdr_h_ops[ftype->fft_aggr].op(ftype);
	return 0;
}

int c2_fop_type_format_kxdr_c(struct c2_fop_field_type *ftype)
{
	type_decorate(ftype);
	kxdr_c_ops[ftype->fft_aggr].op(ftype);
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
