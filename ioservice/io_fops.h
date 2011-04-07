/* -*- C -*- */
#ifndef __COLIBRI_IOSERVICE_IO_FOPS_H__
#define __COLIBRI_IOSERVICE_IO_FOPS_H__

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
 * Bunch of externs needed for stob/ut/io_fop_init.c code. 
 */
extern struct c2_fop_type_ops c2_io_cob_readv_ops;
extern struct c2_fop_type_ops c2_io_cob_writev_ops;
extern struct c2_fop_type_ops c2_io_rwv_rep_ops;

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

/* __COLIBRI_IOSERVICE_IO_FOPS_H__ */
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

