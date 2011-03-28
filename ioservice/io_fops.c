/* -*- C -*- */
#include "io_fops.h"
#include "lib/errno.h"

int c2_fop_cob_rwv_fom_init(struct c2_fop *fop, struct c2_fom **m);

/**
 * readv FOP operation vector.
 */
struct c2_fop_type_ops cob_readv_ops = {
	.fto_fom_init = c2_fop_cob_rwv_fom_init,
};

/**
 * writev FOP operation vector.
 */
struct c2_fop_type_ops cob_writev_ops = {
	.fto_fom_init = c2_fop_cob_rwv_fom_init,
};

/**
 * Init function to initialize readv and writev reply FOMs.
 * Since there is no client side FOMs as of now, this is empty.
 */
int c2_fop_cob_io_rep_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

/**
 * readv and writev reply FOP operation vector.
 */
struct c2_fop_type_ops io_rep_ops = {
	.fto_fom_init = c2_fop_cob_io_rep_fom_init,
};

/**
 * FOP definitions for readv and writev operations.
 */
C2_FOP_TYPE_DECLARE(c2_fop_cob_readv, "read request", 
		    c2_io_service_readv_opcode, &cob_readv_ops);
C2_FOP_TYPE_DECLARE(c2_fop_cob_writev, "write request", 
		    c2_io_service_writev_opcode, &cob_writev_ops);
/**
 * FOP definitions of readv and writev reply FOPs.
 */
C2_FOP_TYPE_DECLARE(c2_fop_cob_writev_rep, "Write reply", 
		    c2_io_service_writev_rep_opcode, &io_rep_ops);
C2_FOP_TYPE_DECLARE(c2_fop_cob_readv_rep, "Read reply", 
		    c2_io_service_readv_rep_opcode, &io_rep_ops);

#ifndef __KERNEL__

struct c2_fom;

/** Generic ops object for c2_fop_cob_writev */
struct c2_fom_ops c2_fom_write_ops = {
	.fo_fini = NULL,
	.fo_state = c2_fom_cob_rwv_state,
};

/** Generic ops object for c2_fop_cob_readv */
struct c2_fom_ops c2_fom_read_ops = {
	.fo_fini = NULL,
	.fo_state = c2_fom_cob_rwv_state,
};

/** Generic ops object for readv and writev reply FOPs */
struct c2_fom_ops c2_fom_io_rep = {
	.fo_fini = NULL,
	.fo_state = NULL,
};

#else /* #ifdef __KERNEL__ */

/** Placeholder API for c2t1fs build. */
int c2_fop_cob_rwv_fom_init(struct c2_fop *fop, struct c2_fom **m)
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
