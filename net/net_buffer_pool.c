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
   @addtogroup net_buffer_pool  Network Buffer Pool
   @{
 */

enum {
	/* Hex ASCII value of "nb_link" */
	NET_BUFFER_POOL_LINK_MAGIC = 0x6e625f6c696e6b,
	/* Hex ASCII value of "nb_head" */
	NET_BUFFER_POOL_HEAD_MAGIC = 0x6e625f68656164,
};

/** Descriptor for the tlist of buffers. */
C2_TL_DESCR_DEFINE(net_buffer_pool, "net_buffer_pool", static,
		   struct c2_net_buffer, nb_linkage, nb_magic,
		   NET_BUFFER_POOL_LINK_MAGIC, NET_BUFFER_POOL_HEAD_MAGIC);
C2_TL_DEFINE(net_buffer_pool, static, struct c2_net_buffer);

/** Head of list of buffers in the pool. */
static struct c2_tl nbp_head;

int c2_net_buffer_pool_init(struct c2_net_buffer_pool *pool, uint32_t buf_nr,
			    uint32_t seg_nr, c2_bcount_t seg_size,
			    uint32_t threshold)
{
	int rc;
	c2_bcount_t buf_size = seg_nr * seg_size;
	struct c2_net_buffer *nb;

	C2_PRE(pool != NULL);
	C2_PRE(pool->nbp_ndom != NULL);
	C2_PRE(buf_size <=
		c2_net_domain_get_max_buffer_size(pool->nbp_ndom));
	C2_PRE(seg_size <=
	       c2_net_domain_get_max_buffer_segment_size(pool->nbp_ndom));

	pool->nbp_threshold = threshold;
	pool->nbp_free	    = 0;
	pool->nbp_seg_nr    = seg_nr;
	pool->nbp_seg_size  = seg_size;

	c2_mutex_init(&pool->nbp_lock);
	net_buffer_pool_tlist_init(&nbp_head);
	while (++pool->nbp_free <= buf_nr) {
		C2_ALLOC_PTR(nb);
		if (nb == NULL)
			break;
		nb->nb_magic = NET_BUFFER_POOL_LINK_MAGIC;
		rc = c2_bufvec_alloc(&nb->nb_buffer, seg_nr, seg_size);
		if (rc != 0)
			break;
		rc = c2_net_buffer_register(nb, pool->nbp_ndom);
		if (rc != 0)
			break;
		net_buffer_pool_tlink_init(nb);
		C2_ASSERT(!net_buffer_pool_tlink_is_in(nb));
		net_buffer_pool_tlist_add_tail(&nbp_head, nb);
	}
	if (rc != 0) {
		c2_bufvec_free(&nb->nb_buffer);
		c2_free(nb);
		c2_net_buffer_pool_fini(pool);
	}
	return rc;
}
C2_EXPORTED(c2_net_buffer_pool_init);

void c2_net_buffer_pool_fini(struct c2_net_buffer_pool *pool)
{
	struct c2_net_buffer *nb;
	C2_PRE(pool != NULL);
	c2_tlist_for(&net_buffer_pool_tl, &nbp_head, nb) {
		c2_net_buffer_deregister(nb, pool->nbp_ndom);
		c2_bufvec_free(&nb->nb_buffer);
		net_buffer_pool_tlist_del(nb);
		net_buffer_pool_tlink_fini(nb);
		nb->nb_magic = 0;
		c2_free(nb);
		C2_CNT_DEC(pool->nbp_free);
	}c2_tlist_endfor;
	net_buffer_pool_tlist_fini(&nbp_head);
	c2_mutex_fini(&pool->nbp_lock);
}
C2_EXPORTED(c2_net_buffer_pool_fini);

void c2_net_buffer_pool_lock(struct c2_net_buffer_pool *pool)
{
	c2_mutex_lock(&pool->nbp_lock);
}
C2_EXPORTED(c2_net_buffer_pool_lock);

bool c2_net_buffer_pool_is_locked(const struct c2_net_buffer_pool *pool)
{
	return c2_mutex_is_locked(&pool->nbp_lock);
}
C2_EXPORTED(c2_net_buffer_pool_is_locked);

void c2_net_buffer_pool_unlock(struct c2_net_buffer_pool *pool)
{
	c2_mutex_unlock(&pool->nbp_lock);
}
C2_EXPORTED(c2_net_buffer_pool_unlock);

struct c2_net_buffer *c2_net_buffer_pool_get(struct c2_net_buffer_pool *pool)
{
	struct c2_net_buffer *nb;

	C2_PRE(pool != NULL);
	if (pool->nbp_free <= 0)
		return NULL;
	C2_PRE(c2_net_buffer_pool_is_locked(pool));
	nb = net_buffer_pool_tlist_head(&nbp_head);
	if (nb != NULL) {
		net_buffer_pool_tlist_del(nb);
		net_buffer_pool_tlink_fini(nb);
		C2_CNT_DEC(pool->nbp_free);
	} else
		nb = NULL;
	if (pool->nbp_free < pool->nbp_threshold) {
		pool->nbp_ndom->nd_ops->nbpo_below_threshold(pool);
	}
	return nb;
}
C2_EXPORTED(c2_net_buffer_pool_get);

void c2_net_buffer_pool_put(struct c2_net_buffer_pool *pool,
			    struct c2_net_buffer *buf)
{
	C2_PRE(pool != NULL);
	C2_PRE(buf != NULL);
	C2_PRE(c2_net_buffer_pool_is_locked(pool));
	C2_PRE(!(buf->nb_flags & C2_NET_BUF_IN_USE));
	C2_PRE(buf->nb_flags & C2_NET_BUF_REGISTERED);
	C2_PRE(pool->nbp_ndom == buf->nb_dom);

	C2_ASSERT(buf->nb_magic == NET_BUFFER_POOL_LINK_MAGIC);
	net_buffer_pool_tlink_init(buf);
	C2_ASSERT(!net_buffer_pool_tlink_is_in(buf));
	net_buffer_pool_tlist_add_tail(&nbp_head, buf);
	C2_CNT_INC(pool->nbp_free);
	if (pool->nbp_free == 1)
		pool->nbp_ndom->nd_ops->nbpo_not_empty(pool);
}
C2_EXPORTED(c2_net_buffer_pool_put);

bool c2_net_buffer_pool_grow(struct c2_net_buffer_pool *pool)
{
	struct c2_net_buffer *nb;
	int rc;
	C2_PRE(c2_net_buffer_pool_is_locked(pool));
	C2_ALLOC_PTR(nb);
	if (nb == NULL)
		return -ENOMEM;
	nb->nb_magic = NET_BUFFER_POOL_LINK_MAGIC;
	rc = c2_bufvec_alloc(&nb->nb_buffer, pool->nbp_seg_nr,
			   		     pool->nbp_seg_size);
	if (rc != 0)
		goto clean;
	rc = c2_net_buffer_register(nb, pool->nbp_ndom);
	if (rc != 0)
		goto clean;
	c2_net_buffer_pool_put(pool, nb);
	return true;
clean:
	if (rc != 0) {
		c2_bufvec_free(&nb->nb_buffer);
		c2_free(nb);
	}
	return false;
}
C2_EXPORTED(c2_net_buffer_pool_grow);

bool c2_net_buffer_pool_prune(struct c2_net_buffer_pool *pool)
{
	struct c2_net_buffer *nb;

	C2_PRE(pool != NULL);
	if (pool->nbp_free <= pool->nbp_threshold)
		return false;
	C2_PRE(c2_net_buffer_pool_is_locked(pool));
	nb = net_buffer_pool_tlist_head(&nbp_head);
	if (nb != NULL &&
	  !(nb->nb_flags & C2_NET_BUF_IN_USE)) {
		c2_net_buffer_deregister(nb, pool->nbp_ndom);
		c2_bufvec_free(&nb->nb_buffer);
		net_buffer_pool_tlist_del(nb);
		net_buffer_pool_tlink_fini(nb);
		nb->nb_magic = 0;
		c2_free(nb);
		C2_CNT_DEC(pool->nbp_free);
		return true;
	} else
		return false;
}
C2_EXPORTED(c2_net_buffer_pool_prune);

/** @} end of net_buffer_pool */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
