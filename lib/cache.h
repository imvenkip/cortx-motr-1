#ifndef _C2_LIB_CACHE_H_

#define _C2_LIB_CACHE_H_

#include <db.h>

/**
 encode the key to record as host-independent byte-order.
 used in c2_compound_srv structure.


 @param reply  application supplied reply structure pointer
 @param record where host-independent bytes will be stored to
 @param reclen buffer length of @record

 @retval >0       success, the value is the length of record
 @retval -ENOSPC  No enough space in record
 @retval <0       other error
 */
typedef int (*c2_cache_enode_t)(void *buffer, void **record, int *reclen);

/**
 decode the reply buffer to record as host byte-order.
 used in c2_compound_srv structure.

 @param reply  application supplied reply structure pointer
 @param record where host-independent bytes will be stored to
 @param reclen buffer length of @record

 @retval >0       success, the value is the length of record
 @retval -ENOSPC  No enough space in record
 @retval <0       other error
*/
typedef int (*c2_cache_decode_t)(void *record,
				 void **reply, int *replen);


struct c2_cache {
	DB 	*c_db;

	c2_cache_encode_t	*c_key_enc;

	c2_cache_decode_t	*c_key_dec;
};

#endif