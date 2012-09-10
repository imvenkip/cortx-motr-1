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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 05/17/2010
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/arith.h"   /* min_type, c2_is_po2 */
#include "lib/assert.h"
#include "lib/atomic.h"
#include "lib/memory.h"
#include "lib/finject.h"

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_MEMORY
#include "lib/trace.h"

/**
   @addtogroup memory

   <b>User level malloc(3) based implementation.</b>

   The only interesting detail is implementation of c2_allocated(). No standard
   function returns the amount of memory allocated in the arena.

   GNU Libc defines mallinfo() function, returning the amount of allocated
   memory among other things. In OS X (of all places) there is malloc_size()
   function that, given a pointer to an allocated block of memory, returns its
   size. On other platforms c2_allocates() is always 0.

   @{
*/

static struct c2_atomic64 allocated;

#ifdef HAVE_MALLINFO

#include <malloc.h>
static size_t __allocated(void)
{
	return mallinfo().uordblks;
}
#define __free free
#define __malloc malloc

/* HAVE_MALLINFO */
#elif HAVE_MALLOC_SIZE

#include <malloc/malloc.h>

static void __free(void *ptr)
{
	c2_atomic64_sub(&allocated, malloc_size(ptr));
	free(ptr);
}

void *__malloc(size_t size)
{
	void *area;

	area = malloc(size);
	c2_atomic64_add(&allocated, malloc_size(area));
	return area;
}

static size_t __allocated(void)
{
	return c2_atomic64_get(&allocated);
}

/* HAVE_MALLOC_SIZE */
#else

static size_t __allocated(void)
{
	return 0;
}

#define __free free
#define __malloc malloc

#endif

void *c2_alloc(size_t size)
{
	void *ret;

	if (C2_FI_ENABLED("fail_allocation"))
		return NULL;

	C2_ENTRY("size=%lu", size);
	ret = __malloc(size);
	if (ret)
		memset(ret, 0, size);
	C2_LEAVE("ptr=%p size=%lu", ret, size);
	return ret;
}

void c2_free(void *data)
{
	C2_ENTRY("ptr=%p", data);
	__free(data);
	C2_LEAVE();
}

void c2_free_aligned(void *data, size_t size, unsigned shift)
{
	C2_PRE(c2_addr_is_aligned(data, shift));
	c2_free(data);
}

static size_t used0;

size_t c2_allocated(void)
{
	size_t used;

	used = __allocated();
	if (used < used0)
		used = used0;
	return used - used0;
}

void *c2_alloc_aligned(size_t size, unsigned shift)
{
	void  *result;
	int    rc;
	size_t alignment;

	/*
	 * posix_memalign(3):
	 *
	 *         The requested alignment must be a power of 2 at least as
	 *         large as sizeof(void *).
	 */

	alignment = max_type(size_t, 1 << shift, sizeof result);
	C2_ASSERT(c2_is_po2(alignment));
	rc = posix_memalign(&result, alignment, size);
	if (rc == 0)
		memset(result, 0, size);
	else
		result = NULL;
	return result;
}

int c2_memory_init()
{
	c2_atomic64_set(&allocated, 0);
	used0 = __allocated();
	return 0;
}

void c2_memory_fini()
{
}

int c2_pagesize_get()
{
	return getpagesize();
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
