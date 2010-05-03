/* -*- C -*- */

#ifndef __COLIBRI_FOP_FOP_H__
#define __COLIBRI_FOP_FOP_H__

/**
   @defgroup fop File operation packet

   @note "Had I been one of the tragic bums who lurked in the mist of that
          station platform where a brittle young FOP was pacing back and forth,
          I would not have withstood the temptation to destroy him."

   @{
*/

typedef uint32_t c2_foptype_code_t;

struct foptype {
	c2_foptype_code_t  ft_code;
	const char        *ft_name;
	c2_list_link       ft_linkage;

	int              (*ft_incoming)(struct c2_foptype *ftype, 
					struct c2_fop *fop);
};

struct c2_fopdata;
struct c2_fop {
	struct c2_foptype *f_type;
	struct c2_fopdata *f_data;
};

int  c2_foptype_register  (struct c2_foptype *ftype, 
			   struct c2_rpcmachine *rpcm);
void c2_foptype_unregister(struct c2_foptype *ftype);

bool c2_fop_is_update(const struct c2_foptype *type);
bool c2_fop_is_batch (const struct c2_foptype *type);

enum c2_fop_field_kind {
	FFK_BUILTIN,
	FFK_STANDARD,
	FFK_OTHER,

	FFK_NR
};

enum c2_fop_field_type {
	FFT_ZERO,
	FFT_VOID,
	FFT_BOOL,
	FFT_BITMASK,
	FFT_OBJECT,
	FFT_NAME,
	FFT_PATH,
	FFT_PRINCIPAL,
	FFT_TIMESTAMP,
	FFT_EPOCH,
	FFT_VERSION,
	FFT_OFFSET,
	FFT_COUNT,
	FFT_BUFFER,
	FFT_RESOURCE,
	FFT_LOCK,
	FFT_NODE,
	FFT_FOP,

	FFT_NR
};

struct c2_fop_field {
	enum c2_fop_field_kind ff_kind;
	enum c2_fop_field_type ff_type;
	const char            *ff_name;
};

struct c2_fop_field_iterator {
	struct c2_fop       *ffi_fop;
};

struct c2_fop_field_val {
	struct c2_fop       *ffv_fop;
	struct c2_fop_field *ffv_field;
	void                *ffv_val;
};

void c2_fop_iterator_init(struct c2_fop_field_iterator *it, struct c2_fop *fop);
void c2_fop_iterator_fini(struct c2_fop_field_iterator *it);
int  c2_fop_iterator_next(struct c2_fop_field_iterator *it,
			  struct c2_fop_field_val *val);



/** @} end of fop group */

/* __COLIBRI_FOP_FOP_H__ */
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
