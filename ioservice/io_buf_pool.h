#include "lib/tlist.h"
#include "lib/memory.h"
#include "lib/thread.h"
#include "lib/misc.h" /* C2_SET0 */
#include "net/net.h"  /* C2_NET_BUFFER*/
#include <unistd.h>
#include <stdio.h>
enum {
	BUF_SIZE = 64
};
struct c2_buf_pool;
struct c2_buf_pool_list;
/**
   Initializes a buffer pool.
   Parameters are cap:capacity of the pool
		  buf_size:size of each network buffer
		  seg_size:size of each segment in a network buffer
 */
int c2_buf_pool_init(struct c2_buf_pool *pool, int cap, int buf_size, int seg_size);

/**
   Finalizes a buffer pool.
 */
void c2_buf_pool_fini(struct c2_buf_pool *pool);

void c2_buf_pool_lock(struct c2_buf_pool *pool);
void c2_buf_pool_unlock(struct c2_buf_pool *pool);

/**
   Returns a network buffer the pool.
   Returns NULL when the pool is empty
   @pre pool is locked
 */
struct c2_net_buffer * c2_buf_pool_get(struct c2_buf_pool *pool);

/**
   Returns the back to the pool.
 */
void c2_buf_pool_put(struct c2_buf_pool *pool, struct c2_net_buffer *buf);

/**
   Adds a new buffer to the pool to increase the capacity.
 */
void c2_buf_pool_add(struct c2_buf_pool *pool,struct c2_net_buffer *buf);


struct c2_buf_pool_ops {
	void (*notEmpty)(struct c2_buf_pool *);
	void (*low)(struct c2_buf_pool *);
};

struct c2_buf_list {
struct c2_tlink bl_link;
uint64_t	bl_magic;
struct c2_net_buffer *bl_nb;
};

struct c2_buf_pool {
uint32_t bp_free;
uint32_t bp_threshold;
struct c2_addb_ctx bp_addb;

struct c2_mutex bp_lock;
struct c2_buf_list *bp_list;
struct c2_buf_pool_ops *bp_ops;
struct c2_tl bp_head;
struct c2_net_domain ndom;
};

enum {
	/* Hex ASCII value of "iob_link" */
	BUF_POOL_LINK_MAGIC = 0x696f625f6c696e6b,
	/* Hex ASCII value of "iob_head" */
	BUF_POOL_HEAD_MAGIC = 0x696f625f68656164,
};

static const struct c2_tl_descr buf_pool_descr =
		    C2_TL_DESCR("buf_pool_descr",
		    struct c2_buf_list, bl_link, bl_magic,
		    BUF_POOL_LINK_MAGIC, BUF_POOL_HEAD_MAGIC);

/*
struct c2_buf_pool {
uint32_t bp_free;
uint_32t bp_threshold;
struct c2_mutex list_lock;
static struct c2_tl buf_head;
struct c2_addb_ctx bp_addb;

struct c2_buf_pool_list *bp_list;

};

struct c2_buf_pool_list {
struct c2_tlink bpl_link;
uint64_t        bpl_magic;
struct c2_net_buffer nb;
}
*/

//enum {
	/* Hex ASCII value of "bpl_link" */
//	IO_BUF_LINK_MAGIC = 0x62706c5f6c696e6b,
	/* Hex ASCII value of "bpl_head" */
//	IO_BUF_HEAD_MAGIC = 0x62706c5f68656164,
//};

//static const struct c2_tl_descr bpl_descr =
//		    C2_TL_DESCR("bpl_descr",
//		    struct c2_buf_pool_list, bpl_link, bpl_magic,
//		    IO_BUF_LINK_MAGIC, IO_BUF_HEAD_MAGIC);
//
