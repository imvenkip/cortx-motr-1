#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include "memory.h"

void *c2_alloc(int size)
{
	void *ret;

	ret = malloc(size);
	if (ret)
		memset(ret, 0, size);

	return ret;
}

void c2_free(void *data, int size)
{
	free(data);
}
