#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "atomic.h"
#include "memory.h"

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
#elif HAVE_MALLOC_SIZE
#include <malloc/malloc.h>

static struct c2_atomic64 allocated = C2_ATOMIC64_INIT(0);

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
	if (data)
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

void c2_memory_init()
{
	used0 = __allocated();
}

void c2_memory_fini()
{
}

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
