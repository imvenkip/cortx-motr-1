/* -*- C -*- */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "io_fops.h"
#include "lib/errno.h"

int c2_io_fop_cob_rwv_fom_init(struct c2_fop *fop, struct c2_fom **m);

/**
 * readv FOP operation vector.
 */
struct c2_fop_type_ops c2_io_cob_readv_ops = {
	.fto_fom_init = c2_io_fop_cob_rwv_fom_init,
};

/**
 * writev FOP operation vector.
 */
struct c2_fop_type_ops c2_io_cob_writev_ops = {
	.fto_fom_init = c2_io_fop_cob_rwv_fom_init,
};

/**
 * Init function to initialize readv and writev reply FOMs.
 * Since there is no client side FOMs as of now, this is empty.
 */
static int c2_io_fop_cob_rwv_rep_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

/**
 * readv and writev reply FOP operation vector.
 */
struct c2_fop_type_ops c2_io_rwv_rep_ops = {
	.fto_fom_init = c2_io_fop_cob_rwv_rep_fom_init,
};

/**
 * FOP definitions for readv and writev operations.
 */
C2_FOP_TYPE_DECLARE(c2_fop_cob_readv, "read request", 
		    c2_io_service_readv_opcode, &c2_io_cob_readv_ops);
C2_FOP_TYPE_DECLARE(c2_fop_cob_writev, "write request", 
		    c2_io_service_writev_opcode, &c2_io_cob_writev_ops);
/**
 * FOP definitions of readv and writev reply FOPs.
 */
C2_FOP_TYPE_DECLARE(c2_fop_cob_writev_rep, "Write reply", 
		    c2_io_service_writev_rep_opcode, &c2_io_rwv_rep_ops);
C2_FOP_TYPE_DECLARE(c2_fop_cob_readv_rep, "Read reply", 
		    c2_io_service_readv_rep_opcode, &c2_io_rwv_rep_ops);

#ifdef __KERNEL__

/** Placeholder API for c2t1fs build. */
int c2_io_fop_cob_rwv_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

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
