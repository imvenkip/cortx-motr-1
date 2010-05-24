#include <assert.h>

#include "lib/cdefs.h"
#include "lib/cache.h"

int test_enc(void *buffer, void **rec , uint32_t *size)
{
	*rec = buffer;
	*size = sizeof(int);

	return 0;
}

int test_dec(void *rec, void **buffer, uint32_t *size)
{
	
	*((int *)buffer) = *((int *)rec);
	*size = sizeof(int);

	return 0;
}

struct c2_cache test_cache1 = {
	.c_pkey_enc = test_enc,
};

void test_cache()
{
	int rc;
	int key;
	int data;
	uint32_t data_s;

	rc = c2_cache_init(&test_cache1, NULL, "test_db1", 0);
	assert(!rc);

	key = 5;
	data = 100;
	rc = c2_cache_insert(&test_cache1, NULL, &key, test_enc, &data, sizeof data);
	assert(!rc);

	key = 5;
	rc = c2_cache_search(&test_cache1, &key, test_dec, (void **)&data, &data_s);
	assert(!rc);
	printf("%d\n", data);

	rc = c2_cache_delete(&test_cache1, NULL, &key);
	assert(!rc);

	rc = c2_cache_search(&test_cache1, &key, test_dec, (void **)&data, &data_s);
	assert(rc);


	c2_cache_fini(&test_cache1);
}
