/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 08/04/2010
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <linux/slab.h>
#include <linux/module.h>

#include "lib/cdefs.h"  /* C2_EXPORTED */
#include "lib/assert.h"  /* C2_PRE */
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

void *c2_alloc_aligned(size_t size, unsigned shift)
{
	/*
	 * Currently it supports alignment of PAGE_SHIFT only.
	 */
	C2_PRE(shift == PAGE_SHIFT);
	if (size == 0)
		return NULL;
	else
		return alloc_pages_exact(size, GFP_KERNEL | __GFP_ZERO);
}
C2_EXPORTED(c2_alloc_aligned);

void c2_free(void *data)
{
	kfree(data);
}
C2_EXPORTED(c2_free);

void c2_free_aligned(void *addr, size_t size, unsigned shift)
{
	C2_PRE(shift == PAGE_SHIFT);
	C2_PRE(c2_addr_is_aligned(addr, shift));
	free_pages_exact(addr, size);
}
C2_EXPORTED(c2_free_aligned);

size_t c2_allocated(void)
{
	return 0;
}
C2_EXPORTED(c2_allocated);

int c2_pagesize_get(void)
{
	return PAGE_SIZE;
}

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
