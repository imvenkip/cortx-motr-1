/* -*- C -*- */

#ifndef __COLIBRI_FOP_FOP_FORMAT_H__
#define __COLIBRI_FOP_FOP_FORMAT_H__

#include "fop.h"

/**
   @addtogroup fop 

   @{
*/

struct c2_fop_field_descr;
struct c2_fop_field_format;

struct c2_fop_field_format {
	const char                      *fif_name;
	const struct c2_fop_field_base  *fif_base;
	const struct c2_fop_field_descr *fif_ref;
};

struct c2_fop_field_descr {
	const struct c2_fop_field_format *ffd_fmt;
	const size_t                      ffd_nr;
	struct c2_fop_field              *ffd_field;
};

#define C2_FOP_FIELD_FORMAT(name, type) {	\
	.fif_name = (name),		        \
	.fif_base = &c2_fop_field_base[type]	\
}

#define C2_FOP_FIELD_REF(name, referred) {		\
	.fif_name = (name),				\
	.fif_base = &c2_fop_field_base[FFT_REF],	\
	.fif_ref  = &referred				\
}

#define C2_FOP_FIELD_FORMAT_END {		\
        .fif_name = NULL,			\
        .fif_base = NULL			\
}

int c2_fop_field_format_parse(struct c2_fop_field_descr *descr);

extern const struct c2_fop_field_base c2_fop_field_base[FFT_NR];

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
