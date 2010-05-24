#ifndef __COLIBRI_RPC_PCACHE_H__

#define __COLIBRI_RPC_PCACHE_H__

#include "lib/cdefs.h"

#include "lib/cache.h"
#include "net/net.h"

/**
 @page rpc-pcache persistent cache definitions
*/

/**
 @section overview

  Persistence cache is used to meet the EOS (Exactly Once Semantics) requirement
in C2 service, this is because some update operations are non-idempotent, so
that the server must run the RPC once and exactly once. This attribute can be
always met in a normally running system, but problems arise if
network(connection loss, route path changed, etc) or node fails.

 The purpose of persistence cache is to store the replied requests into a
database, which have a stable storage attached. By storing the request replies
in the database, if the server fails and restarts, it can consult the
persistence cache to know if the corresponding requests had been executed. In
this case, server just replies the client with the reply contents in the
persistence cache - we call this reply reconstruction. Otherwise, if the server
knows that the replies has been received by clients, it will delete the
corresponding entries from persistence cache.

 @section definition

 Persistence Table: Persistence Table is a table used to store the persistence
cache in stable storage. Currently, we define a table in the DB4 database as
persistence table. The key value of persistence table is slot id.

 Reply reconstruction: To meet EOS, if a retrying request had already executed by
server, the server will just compose a reply to send to the client, w/o actually
executing the request once again. The process of reconstructing the reply RPC
request is called `reply reconstruction'.

 FOP: File Operation Packet. A FOP represents a single, atomic operation to
access file. One important thing has to be stated: RPC is just a container, a
unit of network transfer, to transfer FOPs over network. FOP is actually the
representation of file access. By this means, recovery, transaction management,
etc must be based on FOP; they have nothing to do with RPC.

@section functional-specification Functional Specification

In C2, the persistence cache only exists on the server side where stable storage
equips. We don't implement it on the client side, so that client must not
support any non-idempotent operations. For example, client must be able to
handle multiple lock revocation callback against the same lock; and server must
be able to handle the case that clients complain about non-existence of locks.

First of all, to meet the EOS requirements, we need to label each FOP uniquely,
this is done by tuple <clientID, FOPID>. Then after the request has been
executed by the server, it must cache the result of execution, until it is known
that the client has received the reply. We refer to this cache as `persistence
reply cache' or `persistence cache', or `reply cache' - those three
terminologies are exchangeable. Executing a FOP and inserting the FOP result
into the persistence cache must be atomic.

To make the retrying of requests to be sane, we may need the server to execute
the requests in strictly order, so that if one requests tries failed (because
replier didn't execute it yet), it implicitly indicates the following
outstanding requests from the same client weren't executed as well. This is
because a request B may be relying on the previous request A to be successful,
for example, request A creates a directory, and request B creates a file under
that directory, and if request A is not executed, retrying request B doesn't
make any sense. But how this requirements can be met is out of the scope of this
document. In fact, in C2, we're going to use version based recovery plus DTM
undo-only and redo to recover the nodes from crash, so reply cache is just used
to solve some special cases where version based recovery can't be used - for
example, we may cache directory entries in client side with a stale inode
version, and then operate the inode based on this stale version.


@section logic-specification Logic Specification

We use a database to implement persistence cache in C2. The key value of
persistence cache is structure c2_crid, and the record is the contents of reply
which is provide by server.

In case the server failure or connection loss occurs, the server will enter into
a so-called grace period, during this period, only retrying requests are
accepted. We then need to consult persistence cache entries from the database
and use them to reconstruct the reply. After all entries have been served, or
grace period times out, it will purge the persistence cache, and then can serve
new requests.

@ref rpc-server
*/


/*
* Used to describe an unique FOP for reply cache at server side.
*/
struct c2_rcid {
	struct c2_service_id	cr_clid;  /* client ID of this request */
	uint64_t		cr_xid;   /* FOP ID */
};

/**
 Persistent cache object.
 cache have created by calling c2_pcache_init from c2_rpc_server constructor.
 cache a live all time until rpc service is live, and destroyed via call
 c2_pcache_fini from a c2_rpc_server destructor.

 all modifications in cache need to be synchronized with transaction which
 used to execute folling FOP.
*/

struct c2_rpc_server;

/**
 persistent cache constructor

 initialize a pcache database(s)
 */
int c2_pcache_init(struct c2_rpc_server *srv);

/**
 persistent cache destructor

 close database(s) and release resources
 */
void c2_pcache_fini(struct c2_rpc_server *srv);


/**
 This function is used by server process to store the reply of FOP @reqid
 into persistence cache table.
 If reply with same key exist, it will overwrited.

 @param cache is the cache to store reply contents
 @param db_txn is the transaction which is used to execute the FOP
 @param reqid is the unique ID of FOP
 @param reply is the reply contents of FOP request @reqid, its contents
              must have been converted host independent byte order

 @retval 0           success
 @retval -EEXIST     There've already been a reply in the database
 @retval < 0         error occurs
 */
static inline
int c2_pcache_insert(struct c2_cache *cache, DB_TXN *db_txn,
		     struct c2_rcid *reqid, c2_cache_encode_t enc,
		     void *reply, size_t size)
{
	return c2_cache_insert(cache, db_txn, reqid, enc, reply, size);
}

/**
 This function is used to delete a record from the database.

 @param cache is the cache to store reply contents
 @param db_txn is the transaction which is used to execute the FOP
 @param slotid the slotid

 @retval 0          success
 @retval -ENOENT    no such key exists in the database
 @reval < 0 error   other error
*/
static inline
int c2_pcache_delete(struct c2_cache *cache, DB_TXN *db_txn,
		     struct c2_rcid *reqid)
{
	return c2_cache_delete(cache, db_txn, reqid);
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
static inline
int c2_pcache_search(struct c2_cache *cache, struct c2_rcid *reqid,
		     c2_cache_decode_t dec_fn, void **reply, uint32_t *size)
{
	return c2_cache_search(cache, reqid, dec_fn, reply, size);
}
#endif

