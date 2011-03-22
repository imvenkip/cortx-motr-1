/* -*- C -*- */

#include "sns/parity_ops.h"
#include "lib/cdefs.h"

void c2_parity_fini(void)
{
	galois_calc_tables_release();
}
C2_EXPORTED(c2_parity_fini);

void c2_parity_init(void)
{
	int ret = galois_create_mult_tables(C2_PARITY_GALOIS_W);
	C2_ASSERT(ret == 0);
}
C2_EXPORTED(c2_parity_init);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
