#ifndef _FOP_TYPES_H_
#define _FOP_TYPES_H_
#include "fop/fop.h"
#include "fop/fop_format.h"
#include "ioservice/io_foms.h"

/** Needed for fop type definitions. */
extern struct c2_fop_type_format c2_fop_cob_writev_rep_tfmt;
extern struct c2_fop_type_format c2_fop_cob_readv_rep_tfmt;

int c2_fop_cob_io_rep_fom_init(struct c2_fop *fop, struct c2_fom **m);

struct c2_fop_type_ops io_rep_ops = {
        .fto_fom_init = c2_fop_cob_io_rep_fom_init,
};

C2_FOP_TYPE_DECLARE(c2_fop_cob_writev_rep, "Write reply", (c2_io_service_fom_start_opcode+2), &io_rep_ops);
C2_FOP_TYPE_DECLARE(c2_fop_cob_readv_rep, "Read reply", (c2_io_service_fom_start_opcode+3), &io_rep_ops);

#endif

