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

#include "config.h"      /* ENABLE_FREE_POISON */
#include "lib/arith.h"   /* min_type, m0_is_po2 */
#include "lib/assert.h"
#include "lib/atomic.h"
#include "lib/memory.h"
#include "lib/finject.h"

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_MEMORY
#include "lib/trace.h"

/**
   @addtogroup memory

   <b>User level malloc(3) based implementation.</b>

   The only interesting detail is implementation of m0_allocated(). No standard
   function returns the amount of memory allocated in the arena.

   GNU Libc defines mallinfo() function, returning the amount of allocated
   memory among other things. In OS X (of all places) there is malloc_size()
   function that, given a pointer to an allocated block of memory, returns its
   size. On other platforms m0_allocates() is always 0.

   @{
*/

static struct m0_atomic64 allocated;
static struct m0_atomic64 cumulative_alloc;
static struct m0_atomic64 cumulative_free;

enum { U_POISON_BYTE = 0x5f };

#ifdef HAVE_MALLINFO

#include <malloc.h>
static size_t __allocated(void)
{
	return mallinfo().uordblks;
}

#define __malloc malloc

static void __free(void *ptr)
{
#ifdef ENABLE_FREE_POISON
	memset(ptr, U_POISON_BYTE, malloc_usable_size(ptr));
#endif
	free(ptr);
}

/* HAVE_MALLINFO */
#elif HAVE_MALLOC_SIZE

#include <malloc/malloc.h>

static void __free(void *ptr)
{
	size_t size = malloc_size(ptr);

	m0_atomic64_sub(&allocated, size);
#ifdef ENABLE_DEV_MODE
	m0_atomic64_add(&cumulative_free, size);
#endif
#ifdef ENABLE_FREE_POISON
	memset(ptr, U_POISON_BYTE, size);
#endif
	free(ptr);
}

M0_INTERNAL void *__malloc(size_t size)
{
	void *area;

	area = malloc(size);
#ifdef ENABLE_DEV_MODE
	m0_atomic64_add(&allocated, malloc_size(area));
#endif
	return area;
}

static size_t __allocated(void)
{
	return m0_atomic64_get(&allocated);
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

void *m0_alloc(size_t size)
{
	void *ret;

	if (M0_FI_ENABLED("fail_allocation"))
		return NULL;

	M0_ENTRY("size=%lu", size);
	ret = __malloc(size);
	if (ret != NULL) {
		memset(ret, 0, size);
#ifdef ENABLE_DEV_MODE
		m0_atomic64_add(&cumulative_alloc, size);
#endif
	}
	M0_LEAVE("ptr=%p size=%lu", ret, size);
	return ret;
}

void m0_free(void *data)
{
	M0_ENTRY("ptr=%p", data);
	__free(data);
	M0_LEAVE();
}

M0_INTERNAL void m0_free_aligned(void *data, size_t size, unsigned shift)
{
	M0_PRE(m0_addr_is_aligned(data, shift));
	m0_free(data);
}

static size_t used0;

M0_INTERNAL size_t m0_allocated(void)
{
	size_t used;

	used = __allocated();
	if (used < used0)
		used = used0;
	return used - used0;
}

M0_INTERNAL size_t m0_allocated_total(void)
{
	return m0_atomic64_get(&cumulative_alloc);
}

M0_INTERNAL size_t m0_freed_total(void)
{
	return m0_atomic64_get(&cumulative_free);
}

M0_INTERNAL void *m0_alloc_aligned(size_t size, unsigned shift)
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
	M0_ASSERT(m0_is_po2(alignment));
	rc = posix_memalign(&result, alignment, size);
	if (rc == 0) {
		memset(result, 0, size);
#ifdef ENABLE_DEV_MODE
		m0_atomic64_add(&cumulative_alloc, size);
#endif
	} else {
		result = NULL;
	}
	return result;
}

M0_INTERNAL int m0_memory_init(void)
{
	void *nothing;

	/*
	 * m0_bitmap_init() relies on non-NULL-ness of m0_alloc(0) result.
	 */
	nothing = m0_alloc(0);
	M0_ASSERT(nothing != NULL);
	m0_free(nothing);
	m0_atomic64_set(&allocated, 0);
	m0_atomic64_set(&cumulative_alloc, 0);
	m0_atomic64_set(&cumulative_free, 0);
	used0 = __allocated();
	return 0;
}

M0_INTERNAL void m0_memory_fini(void)
{
}

M0_INTERNAL int m0_pagesize_get()
{
	return getpagesize();
}

#undef M0_TRACE_SUBSYSTEM

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
