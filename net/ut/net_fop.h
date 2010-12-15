#ifndef _IO_FOP_H_
#define _IO_FOP_H_

#include "fop/fop.h"

extern struct c2_fop_type c2_io_nettest_fopt;

int io_fop_init(void);
void io_fop_fini(void);

#endif /* !_IO_FOP_H_ */
/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
