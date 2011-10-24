/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Madhavrao Vemuri <madhav_vemuri@xyratex.com>
 * Original creation date: 12/10/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "lib/memory.h"/* C2_ALLOC_PTR */
#include "lib/errno.h" /* ENOMEM */
#include "lib/arith.h" /* C2_CNT_INC & C2_CNT_DEC*/
#include "net/net_buffer_pool.h"

/**
   @addtogroup io_buf_prealloc Buffer pool
   @{
 */

/** Descriptor for the tlist of buffers. */
//C2_TL_DESCR_DEFINE(buf_pool, "buf_pool", static, struct c2_buf_pool_item,
C2_TL_DESCR_DEFINE(buf_pool, "buf_pool", static, struct c2_net_buffer,
		   bpi_link, bpi_magic, BUF_POOL_LINK_MAGIC,
		   BUF_POOL_HEAD_MAGIC);
//C2_TL_DEFINE(buf_pool, static, struct c2_buf_pool_item);
C2_TL_DEFINE(buf_pool, static, struct c2_net_buffer);

/** Head of list of buffers in the pool. */
static struct c2_tl bp_head;

int c2_buf_pool_init(struct c2_buf_pool *pool, uint32_t buf_nr, uint32_t seg_nr,
		     c2_bcount_t seg_size, uint32_t threshold)
{
	int rc;
	c2_bcount_t buf_size = seg_nr * seg_size;
//	struct c2_buf_pool_item *bpi_item;
	struct c2_net_buffer *nb;

	C2_PRE(pool != NULL);
	C2_PRE(pool->bp_ndom != NULL);
	C2_PRE(buf_size <=
		c2_net_domain_get_max_buffer_size(pool->bp_ndom));
	C2_PRE(seg_size <=
	       c2_net_domain_get_max_buffer_segment_size(pool->bp_ndom));

	pool->bp_threshold = threshold;
	pool->bp_free	   = 0;
	pool->seg_nr	   = seg_nr;
	pool->seg_size	   = seg_size;

	c2_mutex_init(&pool->bp_lock);
	buf_pool_tlist_init(&bp_head);
	while (++pool->bp_free <= buf_nr) {
		/*C2_ALLOC_PTR(bpi_item);
		if (bpi_item == NULL)
			break;
		bpi_item->bpi_magic = BUF_POOL_LINK_MAGIC;
		nb = &bpi_item->bpi_nb;
		*/
		C2_ALLOC_PTR(nb);
		if (nb == NULL)
			break;
		nb->bpi_magic = BUF_POOL_LINK_MAGIC;
		rc = c2_bufvec_alloc(&nb->nb_buffer, seg_nr, seg_size);
		if (rc != 0)
			break;
		rc = c2_net_buffer_register(nb, pool->bp_ndom);
		if (rc != 0)
			break;
		/*buf_pool_tlink_init(bpi_item);
		C2_ASSERT(!buf_pool_tlink_is_in(bpi_item));
		buf_pool_tlist_add_tail(&bp_head, bpi_item);
		*/
		buf_pool_tlink_init(nb);
		C2_ASSERT(!buf_pool_tlink_is_in(nb));
		buf_pool_tlist_add_tail(&bp_head, nb);
	}
	if (rc != 0) {
		c2_bufvec_free(&nb->nb_buffer);
		//c2_free(bpi_item);
		c2_free(nb);
		c2_buf_pool_fini(pool);
	}
	return rc;
}
C2_EXPORTED(c2_buf_pool_init);

void c2_buf_pool_fini(struct c2_buf_pool *pool)
{
	struct c2_net_buffer *nb;
	//struct c2_buf_pool_item *bpi_item;
	C2_PRE(pool != NULL);
	//c2_tlist_for(&buf_pool_tl, &bp_head, bpi_item) {
	c2_tlist_for(&buf_pool_tl, &bp_head, nb) {
		//nb = &bpi_item->bpi_nb;
		c2_net_buffer_deregister(nb, pool->bp_ndom);
		c2_bufvec_free(&nb->nb_buffer);
		//buf_pool_tlist_del(bpi_item);
		//buf_pool_tlink_fini(bpi_item);
		buf_pool_tlist_del(nb);
		buf_pool_tlink_fini(nb);
		//bpi_item->bpi_magic = 0;
		nb->bpi_magic = 0;
		//c2_free(bpi_item);
		c2_free(nb);
		C2_CNT_DEC(pool->bp_free);
	}c2_tlist_endfor;
	buf_pool_tlist_fini(&bp_head);
	c2_mutex_fini(&pool->bp_lock);
}
C2_EXPORTED(c2_buf_pool_fini);

void c2_buf_pool_lock(struct c2_buf_pool *pool)
{
	c2_mutex_lock(&pool->bp_lock);
}
C2_EXPORTED(c2_buf_pool_lock);

bool c2_buf_pool_is_locked(const struct c2_buf_pool *pool)
{
	return c2_mutex_is_locked(&pool->bp_lock);
}
C2_EXPORTED(c2_buf_pool_is_locked);

void c2_buf_pool_unlock(struct c2_buf_pool *pool)
{
	c2_mutex_unlock(&pool->bp_lock);
}
C2_EXPORTED(c2_buf_pool_unlock);

struct c2_net_buffer *c2_buf_pool_get(struct c2_buf_pool *pool)
{
	struct c2_net_buffer *nb;
	//struct c2_buf_pool_item *bpi_item;

	C2_PRE(pool != NULL);
	if (pool->bp_free <= 0)
		return NULL;
	C2_PRE(c2_buf_pool_is_locked(pool));
	//bpi_item = buf_pool_tlist_head(&bp_head);
	nb = buf_pool_tlist_head(&bp_head);
	//if (bpi_item != NULL) {
	//	buf_pool_tlist_del(bpi_item);
	//	buf_pool_tlink_fini(bpi_item);
	//	nb = &bpi_item->bpi_nb;
	if (nb != NULL) {
		buf_pool_tlist_del(nb);
		buf_pool_tlink_fini(nb);
		C2_CNT_DEC(pool->bp_free);
	} else
		nb = NULL;
	if (pool->bp_free < pool->bp_threshold) {
		pool->bp_ops->bpo_below_threshold(pool);
	}
	return nb;
}
C2_EXPORTED(c2_buf_pool_get);

void c2_buf_pool_put(struct c2_buf_pool *pool, struct c2_net_buffer *buf)
{
	//struct c2_buf_pool_item *bpi_item;
	C2_PRE(pool != NULL);
	C2_PRE(buf != NULL);
	C2_PRE(c2_buf_pool_is_locked(pool));
	C2_PRE(!(buf->nb_flags & C2_NET_BUF_IN_USE));
	C2_PRE(buf->nb_flags & C2_NET_BUF_REGISTERED);
	C2_PRE(pool->bp_ndom == buf->nb_dom);

	//bpi_item = container_of(buf, struct c2_buf_pool_item, bpi_nb);
	//C2_ASSERT(bpi_item->bpi_magic == BUF_POOL_LINK_MAGIC);
	C2_ASSERT(buf->bpi_magic == BUF_POOL_LINK_MAGIC);
	//buf_pool_tlink_init(bpi_item);
	buf_pool_tlink_init(buf);
	//C2_ASSERT(!buf_pool_tlink_is_in(bpi_item));
	//buf_pool_tlist_add_tail(&bp_head, bpi_item);
	C2_ASSERT(!buf_pool_tlink_is_in(buf));
	buf_pool_tlist_add_tail(&bp_head, buf);
	C2_CNT_INC(pool->bp_free);
	if (pool->bp_free == 1)
		pool->bp_ops->bpo_not_empty(pool);
}
C2_EXPORTED(c2_buf_pool_put);

bool c2_buf_pool_grow(struct c2_buf_pool *pool)
{
	struct c2_net_buffer *nb;
	//struct c2_buf_pool_item *bpi_item;
	int rc;
	C2_PRE(c2_buf_pool_is_locked(pool));
	//C2_ALLOC_PTR(bpi_item);
	//if (bpi_item == NULL)
	C2_ALLOC_PTR(nb);
	if (nb == NULL)
		return -ENOMEM;
	//bpi_item->bpi_magic = BUF_POOL_LINK_MAGIC;
	nb->bpi_magic = BUF_POOL_LINK_MAGIC;
	//nb = &bpi_item->bpi_nb;
	rc = c2_bufvec_alloc(&nb->nb_buffer, pool->seg_nr, pool->seg_size);
	if (rc != 0)
		goto clean;
	rc = c2_net_buffer_register(nb, pool->bp_ndom);
	if (rc != 0)
		goto clean;
	c2_buf_pool_put(pool, nb);
	return true;
clean:
	if (rc != 0) {
		c2_bufvec_free(&nb->nb_buffer);
		//c2_free(bpi_item);
		c2_free(nb);
	}
	return false;
}
C2_EXPORTED(c2_buf_pool_grow);

bool c2_buf_pool_prune(struct c2_buf_pool *pool)
{
	struct c2_net_buffer *nb;
	//struct c2_buf_pool_item *bpi_item;

	C2_PRE(pool != NULL);
	if (pool->bp_free <= pool->bp_threshold)
		return false;
	C2_PRE(c2_buf_pool_is_locked(pool));
	//bpi_item = buf_pool_tlist_head(&bp_head);
	//if (bpi_item != NULL &&
	 // !(bpi_item->bpi_nb.nb_flags & C2_NET_BUF_IN_USE)) {
	nb = buf_pool_tlist_head(&bp_head);
	if (nb != NULL &&
	  !(nb->nb_flags & C2_NET_BUF_IN_USE)) {
		//nb = &bpi_item->bpi_nb;
		c2_net_buffer_deregister(nb, pool->bp_ndom);
		c2_bufvec_free(&nb->nb_buffer);
		//buf_pool_tlist_del(bpi_item);
		//buf_pool_tlink_fini(bpi_item);
		//bpi_item->bpi_magic = 0;
		//c2_free(bpi_item);
		buf_pool_tlist_del(nb);
		buf_pool_tlink_fini(nb);
		nb->bpi_magic = 0;
		c2_free(nb);
		C2_CNT_DEC(pool->bp_free);
		return true;
	} else
		return false;
}
C2_EXPORTED(c2_buf_pool_prune);

/** @} end of io_buf_prealloc */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
