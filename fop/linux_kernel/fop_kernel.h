/* -*- C -*- */

#ifndef __COLIBRI_FOP_FOP_KERNEL_H__
#define __COLIBRI_FOP_FOP_KERNEL_H__

#include <linux/module.h>

typedef void *xdrproc_t;

#define C2_FOP_TYPE_DECLARE(fopt, name, opcode, ops)	\
	__FOP_TYPE_DECLARE(fopt, name, opcode, ops);    \
	EXPORT_SYMBOL(fopt ## _fopt)

/* __COLIBRI_FOP_FOP_KERNEL_H__ */
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
