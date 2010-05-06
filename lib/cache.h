#ifndef _C2_LIB_CACHE_H_

#define _C2_LIB_CACHE_H_

#include <db.h>

/**
 @page c2-lib-cache generic cache mechanism
*/

/**
 encode the buffer to record in host-independent byte-order.

 @param buffer  application supplied pointer
 @param record where host-independent bytes will be stored to
 @param reclen buffer length of @record

 @retval >0       success, the value is the length of record
 @retval -ENOSPC  No enough space in record
 @retval <0       other error
 */
typedef int (*c2_cache_enode_t)(void *buffer, void **record, int *reclen);

/**
 decode the buffer to record in host byte-order.

 @param record where host-independent bytes will be stored to
 @param buffer  application supplied pointer
 @param bufflen buffer lenght after conversion

 @retval >0       success, the value is the length of record
 @retval -ENOSPC  No enough space in record
 @retval <0       other error
*/
typedef int (*c2_cache_decode_t)(void *record,
				 void **buffer, int *buflen);

/**
 generic cache
*/
struct c2_cache {
	/**
	 database object to store cache
	 */
	DB	*c_db;
        /**
         function to convert primary key from host to database format
	 */
	c2_cache_encode_t	*c_pkey_enc;
	/**
	 function to convert primary key from database to host format
	 */
	c2_cache_decode_t	*c_pkey_dec;
};

/**
  open or create database in given database environment

 @param cache cache object
 @param env database environment if exist, if not exist need set to NULL
 @param dbname unique name for cache
 @param flags cache flags, see db::open flags definition.
*/
int c2_cache_init(stuct c2_cache *cache, DB_ENV *env, const char *dbname,
		  uint32_t flags);

/**
 release cache resources
 @param cache cache object
*/
void c2_cache_fini(struct c2_cache *cache);

/**
 search key in cache

 @param cache cache object
 @param key key to search data
 @param result copy of data from a cache
 @param ressize size of result

*/
int c2_cache_search(struct c2_cache *cache, void *key,
		    void **result, int *ressize);

/**
 insert key in cache

 @param cache cache object
 @param key key to insert in cache
 @param data pointer to supplied data
 @param size size of data region

*/
int c2_cache_insert(struct c2_cache *cache, void *key,
		    void *data, int size);

/**
 delete cache from the cache

 @param cache cache object
 @param key key to delete
 */
int c2_cache_delete(struct c2_cache *cache, void *key);

#endif
