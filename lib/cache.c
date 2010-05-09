#include <errno.h>
#include <stdlib.h> /* free - we can't use c2_free to avoid confusing mem accounting */
#include <string.h>

#include "lib/memory.h"
#include "lib/cache.h"

int convert_db_error(int db_error)
{
	switch (db_error) {
	case DB_NOTFOUND:
		return -ENOENT;
	case DB_LOCK_DEADLOCK:
		return -EDEADLK;
	default:
		return db_error;
	}
}

int c2_cache_init(struct c2_cache *cache, DB_ENV *env, const char *dbname,
		  uint32_t flags)
{
	int ret;
	int open_flags;

	ret = db_create(&cache->c_db, env, 0);
	if (ret != 0)
		return convert_db_error(ret);

	if (flags != 0) {
		ret = cache->c_db->set_flags(cache->c_db, flags);
		if (ret != 0)
			return convert_db_error(ret);
	}

	open_flags = DB_CREATE              | /* Allow database creation */
		     DB_READ_UNCOMMITTED;     /* Allow dirty reads */

	ret = cache->c_db->open(cache->c_db, NULL, dbname,
				NULL, DB_BTREE, open_flags, 0);

	return convert_db_error(ret);
}

void c2_cache_fini(struct c2_cache *cache)
{
	if (!cache->c_db)
		return;
	
	cache->c_db->sync(cache->c_db, 0);
	cache->c_db->close(cache->c_db, 0);
}

int c2_cache_search(struct c2_cache *cache, void *key,
		    c2_cache_decode_t dec_fn,
		    void **result, uint32_t *ressize)
{
	DBT db_key;
	DBT db_value;
	int ret;

	memset(&db_key, 0, sizeof db_key);
	ret = cache->c_pkey_enc(key, &db_key.data, &db_key.size);
	if (ret < 0)
		return ret;

	memset(&db_value, 0, sizeof db_value);
	/* ask db to make copy */
	db_value.flags = DB_DBT_MALLOC;
	ret = cache->c_db->get(cache->c_db, NULL, &db_key,
			       &db_value, 0);

	if (db_key.data != key)
		c2_free(db_key.data, db_key.size);

	if (ret)
		return convert_db_error(ret);

	ret = dec_fn(db_value.data,  result, ressize);
	free(db_value.data);

	return ret;
}

int c2_cache_insert(struct c2_cache *cache, DB_TXN *txn,
		    void *key,
		    void *data, int size)
{
	DBT db_key;
	DBT db_value;
	int ret;

	memset(&db_key, 0, sizeof(db_key));
	ret = cache->c_pkey_enc(key, &db_key.data, &db_key.size);
	if (ret < 0)
		return ret;

	memset(&db_value, 0, sizeof(db_value));
	db_value.data = data;
	db_value.size = size;
	ret = cache->c_db->put(cache->c_db, txn,
			       &db_key, &db_value, 0);

	if (db_key.data != key)
		c2_free(db_key.data, db_key.size);

	return convert_db_error(ret);
}

int c2_cache_delete(struct c2_cache *cache, DB_TXN *txn, void *key)
{
	DBT db_key;
	int ret;

	memset(&db_key, 0, sizeof(db_key));

	ret = cache->c_pkey_enc(key, &db_key.data, &db_key.size);
	if (ret < 0)
		return ret;

	ret = cache->c_db->del(cache->c_db, txn, &db_key, 0);
	if (db_key.data != key)
		c2_free(db_key.data, db_key.size);

	return convert_db_error(ret);
}
