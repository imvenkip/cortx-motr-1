/* -*- C -*- */
#ifndef __COLIBRI_RM_FOP_H__
#define __COLIBRI_RM_FOP_H__

#include "fop/fop.h"

extern struct c2_fop_type c2_rm_fop_sid_fopt;
extern struct c2_fop_type c2_rm_loan_reply_fopt;
extern struct c2_fop_type c2_rm_send_out_fopt;

extern struct c2_fop_type_format c2_rm_fop_sid_tfmt;
extern struct c2_fop_type_format c2_rm_loan_reply_tfmt;
extern struct c2_fop_type_format c2_rm_send_out_tfmt;

void rm_fop_fini(void);
int rm_fop_init(void);

/* __COLIBRI_RM_FOP_H__ */
#endif

/**
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

