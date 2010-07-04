/* -*- C -*- */

#ifndef __COLIBRI_FOP_FOP_FORMAT_H__
#define __COLIBRI_FOP_FOP_FORMAT_H__

/**
   @addtogroup fop 

   @{
*/

#ifndef __KERNEL__
# include <rpc/rpc.h> /* xdrproc_t */
#else
# include "lib/kdef.h"
typedef void *xdrproc_t;
#endif

struct c2_fop_memlayout;

struct c2_fop_type_format {
	struct c2_fop_field_type  *ftf_out;
	enum c2_fop_field_aggr     ftf_aggr;
	const char                *ftf_name;
	uint64_t                   ftf_val;
	struct c2_fop_memlayout   *ftf_layout;
	const struct c2_fop_field_format {
		const char                      *c_name;
		const struct c2_fop_type_format *c_type;
		uint32_t                         c_tag;
	} ftf_child[];
};

int  c2_fop_type_format_parse(struct c2_fop_type_format *fmt);
void c2_fop_type_format_fini(struct c2_fop_type_format *fmt);

int  c2_fop_type_format_parse_nr(struct c2_fop_type_format **fmt, int nr);
void c2_fop_type_format_fini_nr(struct c2_fop_type_format **fmt, int nr);

void *c2_fop_type_field_addr(const struct c2_fop_field_type *ftype, void *obj, 
			     int fileno);

extern const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_VOID_tfmt;
extern const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_BYTE_tfmt;
extern const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_U32_tfmt;
extern const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_U64_tfmt;

#define __paste(x) x ## _tfmt
#ifndef __layout
#define __layout(x) &(x ## _memlayout)
#endif

#define C2_FOP_FIELD_TAG(_tag, _name, _type)	\
{						\
	.c_name = #_name,			\
	.c_type = &__paste(_type),		\
	.c_tag  = (_tag)			\
}

#define C2_FOP_FIELD(_name, _type) C2_FOP_FIELD_TAG(0, _name, _type)

#define C2_FOP_FORMAT(_name, _aggr, ...)	\
struct c2_fop_type_format __paste(_name) = {	\
	.ftf_aggr = (_aggr),			\
	.ftf_name = #_name,			\
	.ftf_val  = 1,				\
	.ftf_layout = __layout(_name),		\
	.ftf_child = {				\
		__VA_ARGS__,			\
		{ .c_name = NULL }		\
	}					\
}

struct c2_fop_decorator {
	const char *dec_name;
	uint32_t    dec_id;
	void      (*dec_type_fini)(void *value);
	void      (*dec_field_fini)(void *value);
};

void c2_fop_decorator_register(struct c2_fop_decorator *dec);

void *c2_fop_type_decoration_get(const struct c2_fop_field_type *ftype,
				 const struct c2_fop_decorator *dec);
void  c2_fop_type_decoration_set(const struct c2_fop_field_type *ftype,
				 const struct c2_fop_decorator *dec, void *val);

void *c2_fop_field_decoration_get(const struct c2_fop_field *field,
				  const struct c2_fop_decorator *dec);
void  c2_fop_field_decoration_set(const struct c2_fop_field *field,
				  const struct c2_fop_decorator *dec, 
				  void *val);

int  c2_fop_comp_init(void);
void c2_fop_comp_fini(void);

int  c2_fop_comp_udef(struct c2_fop_field_type *ftype);
int  c2_fop_comp_kdef(struct c2_fop_field_type *ftype);
int  c2_fop_comp_ulay(struct c2_fop_field_type *ftype);
int  c2_fop_comp_klay(struct c2_fop_field_type *ftype);

typedef char c2_fop_void_t[0];

struct c2_fop_memlayout {
	xdrproc_t fm_uxdr;
	size_t    fm_sizeof;
	struct {
		int ch_offset;
	} fm_child[];
};

#ifndef __KERNEL__
#define C2_FOP_TYPE_DECLARE(fopt, name, opcode, ops)	\
struct c2_fop_type fopt ## _fopt = {			\
	.ft_code = (opcode),				\
	.ft_name = name,				\
	.ft_fmt  = &__paste(fopt),			\
	.ft_ops  = (ops)				\
}

#else /* !__KERNEL__ */
#define C2_FOP_TYPE_DECLARE(fopt, name, opcode, ops)	\
struct c2_fop_type fopt ## _fopt = {			\
	.ft_code = (opcode),				\
	.ft_name = name,				\
	.ft_fmt  = &__paste(fopt),			\
	.ft_ops  = (ops)				\
};                      				\
EXPORT_SYMBOL(fopt ## _fopt)
#endif /* __KERNEL__ */

/** @} end of fop group */

/* __COLIBRI_FOP_FOP_FORMAT_H__ */
#endif

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
