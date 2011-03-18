#ifndef _NET_FOP_H_
#define _NET_FOP_H_

#include "fop/fop.h"

extern struct c2_fop_type c2_nettest_fopt;

int nettest_fop_init(void);
void nettest_fop_fini(void);

#endif /* !_NET_FOP_H_ */
/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
