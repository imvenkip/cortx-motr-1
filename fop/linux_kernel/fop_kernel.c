/* -*- C -*- */

#include <linux/module.h>

#include "fop/fop.h"

/**
   @addtogroup fop
   @{
 */

int init_module(void)
{
        return c2_fops_init();
}

void cleanup_module(void)
{
        c2_fops_fini();
}


/** @} end of fop group */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
