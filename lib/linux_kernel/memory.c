#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <linux/slab.h>
#include <linux/module.h>

#include "lib/cdefs.h"  /* C2_EXPORTED */
#include "lib/memory.h"

/**
   @addtogroup memory

   <b>Linux kernel kmalloc based allocator.</b>

   @{
*/

void *c2_alloc(size_t size)
{
	return kzalloc(size, GFP_KERNEL);
}
C2_EXPORTED(c2_alloc);

void c2_free(void *data)
{
	kfree(data);
}
C2_EXPORTED(c2_free);

size_t c2_allocated(void)
{
	return 0;
}
C2_EXPORTED(c2_allocated);

/** @} end of memory group */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
