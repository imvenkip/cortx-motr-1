/* -*- C -*- */
#include "io_fops.h"

extern struct c2_fop_type_format c2_fop_cob_writev_rep_tfmt;
extern struct c2_fop_type_format c2_fop_cob_readv_rep_tfmt;

#ifndef __KERNEL__

struct c2_fom;
//extern c2_io_service_fom_start_opcode;

/** Generic ops object for c2_fop_cob_writev */
struct c2_fom_ops c2_fom_write_ops = {
	.fo_fini = NULL,
	.fo_state = c2_fom_cob_write_state,
};

/** Generic ops object for c2_fop_cob_readv */
struct c2_fom_ops c2_fom_read_ops = {
	.fo_fini = NULL,
	.fo_state = c2_fom_cob_read_state,
};

/** Generic ops object for readv and writev reply FOPs */
struct c2_fom_ops c2_fom_io_rep = {
	.fo_fini = NULL,
	.fo_state = NULL,
};

int c2_fop_cob_io_rep_fom_init(struct c2_fop *fop, struct c2_fom **m);
        
//C2_FOP_TYPE_DECLARE(c2_fop_cob_writev_rep, "Write reply", (c2_io_service_fom_start_opcode+2), &io_rep_ops);
//C2_FOP_TYPE_DECLARE(c2_fop_cob_readv_rep, "Read reply", (c2_io_service_fom_start_opcode+3), &io_rep_ops);

/**
 * Allocate and return generic struct c2_fom for readv fop.
 * Find the corresponding fom_type and associate it with c2_fom.
 */
int c2_fop_cob_readv_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	struct c2_fom		*fom;
	struct c2_fom_type 	*fom_type;

	C2_PRE(fop != NULL);
	C2_PRE(m != NULL);

	*m = c2_alloc(sizeof(struct c2_fom));
	fom_type = c2_fom_type_map(fop->f_type->ft_code);
	C2_ASSERT(fom_type != NULL);
	fop->f_type->ft_fom_type = *fom_type;
	fom = *m;
	fom->fo_type = fom_type;
	(*m)->fo_ops = &c2_fom_read_ops;
	return 0;
}

/**
 * Allocate and return generic struct c2_fom for writev fop.
 * Find the corresponding fom_type and associate it with c2_fom.
 */
int c2_fop_cob_writev_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	struct c2_fom		*fom;
	struct c2_fom_type 	*fom_type = NULL;

	C2_PRE(fop != NULL);
	C2_PRE(m != NULL);

	*m = c2_alloc(sizeof(struct c2_fom));
	fom_type = c2_fom_type_map(fop->f_type->ft_code);
	C2_ASSERT(fom_type != NULL);
	fop->f_type->ft_fom_type = *fom_type;
	fom = *m;
	fom->fo_type = fom_type;
	(*m)->fo_ops = &c2_fom_write_ops;
	return 0;
}
#else
/** Placeholder APIs for c2t1fs build. */
int c2_fop_cob_readv_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

int c2_fop_cob_writev_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}
#endif

int c2_fop_cob_io_rep_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

struct c2_fop_type_ops io_rep_ops = {
	        .fto_fom_init = c2_fop_cob_io_rep_fom_init,
};

C2_FOP_TYPE_DECLARE(c2_fop_cob_writev_rep, "Write reply", (14+2), &io_rep_ops);
C2_FOP_TYPE_DECLARE(c2_fop_cob_readv_rep, "Read reply", (14+3), &io_rep_ops);

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
