#ifndef _RPC_PCACHE_H_

#define _RPC_PCACHE_H_

/**
 @page rpc-pcache persistent cache definitions
*/

/*
* Used to describe an unique FOP for reply cache at server side.
*/
struct c2_rcid {
	struct c2_node_id      cr_clid;  /* client ID of this request */
	uint64_t               cr_xid;   /* FOP ID */
};

/**
 Persistent cache object.
 cache have created via call c2_pcache_init while rpc service is started.
 cache a live all time until rpc service is live, and destroyed via call
 c2_pcache_fini.

 all modifications in cache need to be synchronized with transaction in
 service database environment.
*/
struct c2_pcache {
	/**
	 db to store reply
	 */
	DB	*db;
};

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
typedef int (*c2_pc_enode_t) (struct c2_crid *id, void *record, int reclen);

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
typedef int (*c2_pc_decode_t)(void *record, int reclen, void **reply);

/**
 persistent cache constructor

 initialize a pcache database(s)
 */
int c2_pcache_init(struct c2_rpc_server *srv);

/**
 persistent cache destructor

 close database(s) and release resources
 */
void c2_pcache_fini(struct c2_pcache *cache);


/**
 This function is used by server process to store the reply of FOP @reqid
 into persistence cache table.

 @param cache is the cache to store reply contents
 @param db_txn is the transaction which is used to execute the FOP
 @param reqid is the unique ID of FOP
 @param reply is the reply contents of FOP request @reqid, its contents
              must have been converted host independent byte order

 @retval 0           success
 @retval -EEXIST     There've already been a reply in the database
 @retval < 0         error occurs
 */
int c2_pcache_insert(struct c2_pcache *cache, DB_TXN *db_txn,
		     struct c2_rcid *reqid, void *reply)

/**
 This function is used to delete a record from the database.

 @param cache is the cache to store reply contents
 @param db_txn is the transaction which is used to execute the FOP
 @param slotid the slotid

 @retval 0          success
 @retval -ENOENT    no such key exists in the database
 @reval < 0 error   other error
*/
int c2_pcache_delete(struct c2_pcache *cache, DB_TXN *db_txn,
		     struct c2_rcid *reqid);

/*
 c2_pcache_replace is just a simple combination of pcache_insert and
 pcache_delete.
 This interface is useful when a new request comes meaning the recept of
 previous request.

 @see c2_pcache_insert for the interpretation of parameters.
*/
static inline
int c2_pcache_replace(struct c2_pcache *cache, DB_TXN *db_txn,
		      struct c2_rcid *reqid, void *reply)
{
	(void)c2_pcache_delete(cache, db_txn, reqid);
	c2_pcache_insert(cache, db_txn, reqid, reply);
}

/*
 c2_pcache_search: check if there exists a persistent reply cache in the database.
 If exists, return the record.

 @param reqid, the fop ID the caller wants to get
 @param db_txn is the transaction which is used to execute the FOP
 @param reply, this is a return parameter, if the slotid exists, it will be filled
               by reply pointer which is returned by @decode

 @retval >0       the bytes of reply buffer filled
 @retval -ENOSPC  the reply buffer is too short to store the record
 @retval -ENOSRC  no such entry in database
 @retval < 0      error occurs.
 */
int c2_pcache_search(struct c2_pcache *cache, struct c2_rcid *reqid,
		     void **reply);


#endif

