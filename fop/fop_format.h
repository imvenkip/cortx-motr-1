/* -*- C -*- */

#ifndef __COLIBRI_FOP_FOP_FORMAT_H__
#define __COLIBRI_FOP_FOP_FORMAT_H__

#include "fop.h"

/**
   @addtogroup fop 

   @{
*/

struct c2_fop_type_format;

struct c2_fop_type_format {
	struct c2_fop_field_type  *ftf_out;
	enum c2_fop_field_aggr     ftf_aggr;
	const char                *ftf_name;
	uint64_t                   ftf_val;
	const struct c2_fop_field_format {
		const char                      *c_name;
		const struct c2_fop_type_format *c_type;
		uint32_t                         c_tag;
	} ftf_child[];
};

int c2_fop_type_format_parse(struct c2_fop_type_format *fmt);

extern const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_VOID;
extern const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_BYTE;
extern const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_U32;
extern const struct c2_fop_type_format C2_FOP_TYPE_FORMAT_U64;

#define C2_FOP_FIELD_TAG(_tag, _name, _type)	\
{						\
	.c_name = #_name,			\
	.c_type = &(_type),			\
	.c_tag  = (_tag)			\
}

#define C2_FOP_FIELD(_name, _type) C2_FOP_FIELD_TAG(0, _name, _type)

#define C2_FOP_FORMAT(_name, _aggr, ...)	\
struct c2_fop_type_format _name = {		\
	.ftf_aggr = (_aggr),			\
	.ftf_name = #_name,			\
	.ftf_val  = 1,				\
	.ftf_child = {				\
		__VA_ARGS__,			\
		{ .c_name = NULL }		\
	}					\
}

int c2_fop_type_format_cdef     (struct c2_fop_field_type *ftype);
int c2_fop_type_format_kdef     (struct c2_fop_field_type *ftype);
int c2_fop_type_format_memlayout(struct c2_fop_field_type *ftype);

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
