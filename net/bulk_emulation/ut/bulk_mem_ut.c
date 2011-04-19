/* -*- C -*- */

#include "lib/arith.h"
#include "lib/assert.h"
#include "lib/cdefs.h"
#include "lib/ut.h"

#include "net/bulk_emulation/mem_xprt_xo.c"

void test_buf_copy(void)
{
}

const struct c2_test_suite net_bulk_mem_ut = {
        .ts_name = "net-bulk-mem",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "net_bulk_mem_buf_copy", test_buf_copy },
                { NULL, NULL }
        }
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
