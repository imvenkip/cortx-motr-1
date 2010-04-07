#include <stdlib.h>
#include "memory.h"


void *c2_alloc(size_t size)
{
	void *ret;

	ret = malloc(size);
	if (ret)
		memset(ret, 0, size);

	return ret;
}

void c2_free(void *data, size_t size)
{
	free(data);
}
