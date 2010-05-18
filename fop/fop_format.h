/* -*- C -*- */

#ifndef __COLIBRI_FOP_FOP_FORMAT_H__
#define __COLIBRI_FOP_FOP_FORMAT_H__

#include "fop.h"

/**
   @addtogroup fop 

   @{
*/

struct c2_fop_field_format {
	const char             *fif_name;
	enum c2_fop_field_type  fif_type;
};

#define C2_FOP_FIELD_FORMAT(name, type) {	\
	.fif_name = (name),		        \
	.fif_type = type			\
}

#define C2_FOP_FIELD_FORMAT_END {		\
        .fif_name = NULL,			\
        .fif_type = FFT_ZERO			\
}

int c2_fop_field_format_parse(const struct c2_fop_field_format *fmt,
			      struct c2_fop_field *top);

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
