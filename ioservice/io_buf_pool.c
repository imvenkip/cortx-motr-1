#include "net/bulk_sunrpc.h"
#include "io_buf_pool.h"
int c2_buf_pool_init(struct c2_buf_pool *pool, int cap, int buf_size, int seg_size)
{
	int nr = 0, NR = 0, i=0;
	int rc = 0;
	struct c2_net_buffer *nb = NULL;
	pool->bp_threshold = 1; 
	c2_tlist_init(&buf_pool_descr, &pool->bp_head);
	NR = cap / buf_size ;
	do {
		nr = buf_size / seg_size;
		cap -= (nr * seg_size);
		C2_ALLOC_PTR(nb);	
		C2_ALLOC_PTR(pool->bp_list);	
		pool->bp_list->bl_nb = nb;
		rc = c2_bufvec_alloc(&nb->nb_buffer, nr, seg_size);
		printf("%lu %lu \n",(unsigned long)nb,(unsigned long)&nb->nb_buffer);
		rc = c2_net_buffer_register(nb, &pool->ndom);
		printf("nr %d cap %d \n",nr,cap);
		c2_tlink_init(&buf_pool_descr, pool->bp_list);
		C2_ASSERT(!c2_tlink_is_in(&buf_pool_descr, pool->bp_list));
		printf("tlist %d\n",++i);
		c2_tlist_add_tail(&buf_pool_descr, &pool->bp_head, pool->bp_list);
		pool->bp_free++;	
	} while (cap > 0);
	return 0;	
}
/**
   Finalizes a buffer pool.
 */
void c2_buf_pool_fini(struct c2_buf_pool *pool)
{
	struct c2_net_buffer *nb = NULL;
	int i = 0;
	c2_tlist_for(&buf_pool_descr, &pool->bp_head, pool->bp_list) {
		nb = pool->bp_list->bl_nb;
		c2_net_buffer_deregister(nb, &pool->ndom);
		c2_bufvec_free(&nb->nb_buffer);
		c2_tlist_del(&buf_pool_descr, pool->bp_list);
		c2_tlink_fini(&buf_pool_descr, pool->bp_list);
		c2_free(nb);
		c2_free(pool->bp_list);
		printf("%d %lu \n",++i,(unsigned long)nb);
		pool->bp_free--;	
	}
	c2_tlist_endfor;
	c2_tlist_fini(&buf_pool_descr, &pool->bp_head);
}

void c2_buf_pool_lock(struct c2_buf_pool *pool)
{
	c2_mutex_lock(&pool->bp_lock);
}
void c2_buf_pool_unlock(struct c2_buf_pool *pool)
{
	c2_mutex_unlock(&pool->bp_lock);
}
/**
   Returns a network buffer the pool.
   Returns NULL when the pool is empty
   @pre pool is locked
 */
struct c2_net_buffer * c2_buf_pool_get(struct c2_buf_pool *pool)
{
	struct c2_net_buffer *nb = NULL;
	if(pool->bp_free < pool->bp_threshold) {
		pool->bp_ops->low(pool);	
	return NULL;
	}
	pool->bp_list = c2_tlist_head(&buf_pool_descr, &pool->bp_head);
	c2_tlist_del(&buf_pool_descr, pool->bp_list);
	c2_tlink_fini(&buf_pool_descr, pool->bp_list);
	nb = pool->bp_list->bl_nb;	
	c2_free(pool->bp_list);
	printf("GET %lu \n",(unsigned long)nb);
	pool->bp_free--;	

	return nb;
}

/**
   Returns the back to the pool.
 */
void c2_buf_pool_put(struct c2_buf_pool *pool, struct c2_net_buffer *buf)
{
	C2_ALLOC_PTR(pool->bp_list);	
	pool->bp_list->bl_nb = buf;
	printf("PUT %lu \n",(unsigned long)buf);
	c2_tlink_init(&buf_pool_descr, pool->bp_list);
	C2_ASSERT(!c2_tlink_is_in(&buf_pool_descr, pool->bp_list));
	c2_tlist_add_tail(&buf_pool_descr, &pool->bp_head, pool->bp_list);
	pool->bp_free++;
	if(pool->bp_free > pool->bp_threshold) 
		pool->bp_ops->notEmpty(pool);	
}

/**
   Adds a new buffer to the pool to increase the capacity.
 */
void c2_buf_pool_add(struct c2_buf_pool *pool,struct c2_net_buffer *buf)
{
	int rc = 0;
	rc = c2_net_buffer_register(buf, &pool->ndom);
	c2_buf_pool_put(pool, buf);

}

void NotEmpty(struct c2_buf_pool *bp);
void Low(struct c2_buf_pool *bp);
void buffers_get_put(int rc);
struct c2_buf_pool bp;
struct c2_chan buf_chan;
struct c2_buf_pool_ops b_ops = {
	.notEmpty = NotEmpty,
	.low	  = Low,
};
int main()
{

	int rc;
	struct c2_thread        *client_thread;
	int			 nr_client_threads = 512;
	int i;
	struct c2_net_buffer *nb = NULL;	
	struct c2_net_xprt *xprt;
	c2_chan_init(&buf_chan);
	xprt = &c2_net_bulk_sunrpc_xprt;
	c2_net_xprt_init(xprt);
	rc = c2_net_domain_init(&bp.ndom, xprt);
	rc = c2_buf_pool_init(&bp, 65536, 128, 64);
	bp.bp_ops = &b_ops;
	C2_ASSERT(rc == 0);
	nb = c2_buf_pool_get(&bp);
	sleep(1);
	c2_buf_pool_put(&bp, nb);
		C2_ALLOC_PTR(nb);	
		rc = c2_bufvec_alloc(&nb->nb_buffer, 10, 1024);
	//rc = c2_net_buffer_register(nb, &bp.ndom);
	c2_buf_pool_add(&bp, nb);
	
	//c2_net_domain_fini(&bp.ndom);
	//c2_net_xprt_fini(xprt);
	C2_ALLOC_ARR(client_thread, nr_client_threads);
	for (i = 0; i < nr_client_threads; i++) {
		C2_SET0(&client_thread[i]);
		rc = C2_THREAD_INIT(&client_thread[i], int,
				     NULL, &buffers_get_put,
					0, "client_%d", i);
		C2_ASSERT(rc == 0);
	}
	for (i = 0; i < nr_client_threads; i++) {
		c2_thread_join(&client_thread[i]);
	}
	c2_buf_pool_fini(&bp);
	c2_net_domain_fini(&bp.ndom);
	c2_net_xprt_fini(xprt);
	c2_chan_fini(&buf_chan);
	return 0;

};

void buffers_get_put(int rc)
{
	struct c2_net_buffer *nb = NULL;	
	struct c2_clink buf_link;
	c2_clink_init(&buf_link, NULL);
	c2_clink_add(&buf_chan, &buf_link);
	do {
		c2_buf_pool_lock(&bp);
		nb = c2_buf_pool_get(&bp);
		c2_buf_pool_unlock(&bp);
		if(nb == NULL)
			c2_chan_wait(&buf_link);
	} while(nb == NULL);
	sleep(1);
	c2_buf_pool_lock(&bp);
	if(nb != NULL)
		c2_buf_pool_put(&bp, nb);
	c2_buf_pool_unlock(&bp);
	c2_clink_del(&buf_link);
	c2_clink_fini(&buf_link);
}
void NotEmpty(struct c2_buf_pool *bp)
{
	//printf("Buffer pool is Not empty \n");
	c2_chan_signal(&buf_chan);
}
void Low(struct c2_buf_pool *bp)
{
	printf("Buffer pool is LOW \n");
}
