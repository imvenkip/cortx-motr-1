#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "memory.h"

void *c2_alloc(size_t size)
{
	void *ret;

	ret = malloc(size);
	if (ret)
		memset(ret, 0, size);

	return ret;
}

void c2_free(void *data)
{
	free(data);
}

size_t c2_allocated(void)
{
	struct mallinfo mi;

	mi = mallinfo();
	return mi.uordblks;
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
