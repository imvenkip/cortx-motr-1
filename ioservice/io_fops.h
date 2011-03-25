/* -*- C -*- */
#ifndef __COLIBRI_FOP_IO_OPERATIONS_H__
#define __COLIBRI_FOP_IO_OPERATIONS_H__

#include "fop/fop.h"
#include "fop/fop_format.h"
#include "lib/memory.h"

struct c2_fom;
struct c2_fom_type;

/** 
 * The opcode from which IO service FOPS start.
 */
enum c2_io_service_opcodes {
	c2_io_service_readv_opcode = 14,
	c2_io_service_writev_opcode,
	c2_io_service_writev_rep_opcode,
	c2_io_service_readv_rep_opcode
};

/**
 *  A mapping function that finds out the FOM type (c2_fom_type)
 *  given an opcode.
 */
struct c2_fom_type* c2_fom_type_map(c2_fop_type_code_t code);

/** 
 * Bunch of externs needed for stob/ut/io_fop_init.c code. 
 */
extern struct c2_fop_type_ops cob_readv_ops;
extern struct c2_fop_type_ops cob_writev_ops;
extern struct c2_fop_type_ops io_rep_ops;

/**
 * FOP definitions and corresponding fop type formats 
 * exported by ioservice.
 */
extern struct c2_fop_type_format c2_fop_cob_writev_tfmt;
extern struct c2_fop_type_format c2_fop_cob_readv_tfmt;
extern struct c2_fop_type_format c2_fop_cob_writev_rep_tfmt;
extern struct c2_fop_type_format c2_fop_cob_readv_rep_tfmt;

extern struct c2_fop_type c2_fop_cob_readv_fopt;
extern struct c2_fop_type c2_fop_cob_writev_fopt;
extern struct c2_fop_type c2_fop_cob_readv_rep_fopt;
extern struct c2_fop_type c2_fop_cob_writev_rep_fopt;

int c2_fop_cob_readv_fom_init(struct c2_fop *fop, struct c2_fom **m);
int c2_fop_cob_writev_fom_init(struct c2_fop *fop, struct c2_fom **m);

/** 
 * FOM init methods for readv, writev and io reply FOPs. 
 * Init methods create the c2_fom objects and do the 
 * necessary association with FOM and FOM type.
 */
int c2_fop_cob_readv_fom_init(struct c2_fop *fop, struct c2_fom **fom);
int c2_fop_cob_writev_fom_init(struct c2_fop *fop, struct c2_fom **fom);
int c2_fop_cob_io_rep_fom_init(struct c2_fop *fop, struct c2_fom **fom);

/** 
 * State handler functions for writev and readv FOPs.
 */
int c2_fom_cob_write_state(struct c2_fom *fom);
int c2_fom_cob_read_state(struct c2_fom *fom);

/**
 * Placeholder declarations for c2t1fs code.
 */
#ifndef __KERNEL__
extern struct c2_fom_ops c2_fom_write_ops;
extern struct c2_fom_ops c2_fom_read_ops;
#endif

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

