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

#include <linux/slab.h>
#include <linux/module.h>

#include "lib/cdefs.h"  /* M0_EXPORTED */
#include "lib/assert.h"  /* M0_PRE */
#include "lib/memory.h"
#include "lib/finject.h" /* M0_FI_ENABLED */

/**
   @addtogroup memory

   <b>Linux kernel kmalloc based allocator.</b>

   @{
*/

M0_INTERNAL void *m0_alloc(size_t size)
{
	if (M0_FI_ENABLED("fail_allocation"))
		return NULL;

	return kzalloc(size, GFP_KERNEL);
}
M0_EXPORTED(m0_alloc);

M0_INTERNAL void *m0_alloc_aligned(size_t size, unsigned shift)
{
	/*
	 * Currently it supports alignment of PAGE_SHIFT only.
	 */
	M0_PRE(shift == PAGE_SHIFT);
	if (size == 0)
		return NULL;
	else
		return alloc_pages_exact(size, GFP_KERNEL | __GFP_ZERO);
}
M0_EXPORTED(m0_alloc_aligned);

M0_INTERNAL void m0_free(void *data)
{
	kfree(data);
}
M0_EXPORTED(m0_free);

M0_INTERNAL void m0_free_aligned(void *addr, size_t size, unsigned shift)
{
	M0_PRE(shift == PAGE_SHIFT);
	M0_PRE(m0_addr_is_aligned(addr, shift));
	free_pages_exact(addr, size);
}
M0_EXPORTED(m0_free_aligned);

M0_INTERNAL size_t m0_allocated(void)
{
	return 0;
}
M0_EXPORTED(m0_allocated);

M0_INTERNAL int m0_pagesize_get(void)
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
