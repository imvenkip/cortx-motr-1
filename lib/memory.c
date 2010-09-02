#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "lib/atomic.h"
#include "lib/memory.h"

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
	struct mallinfo mi;

	mi = mallinfo();
	return mi.uordblks;
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

	ret = __malloc(size);
	if (ret)
		memset(ret, 0, size);

	return ret;
}

void c2_free(void *data)
{
	__free(data);
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
	void *result;
	int   rc;

	rc = posix_memalign(&result, 1 << shift, size);
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
