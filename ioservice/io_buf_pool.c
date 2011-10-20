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
#include "ioservice/io_buf_pool.h"

/**
   @addtogroup io_buf_prealloc Buffer pool
   @{
 */

int c2_buf_pool_init(struct c2_buf_pool *pool, uint32_t buf_nr, uint32_t seg_nr,
		     c2_bcount_t seg_size, uint32_t threshold)
{
	int rc;
	struct c2_buf_pool_item *bpi_item;
	struct c2_net_buffer *nb;

	C2_PRE(pool != NULL);
	C2_PRE((seg_nr * seg_size) <=
		c2_net_domain_get_max_buffer_size(pool->bp_ndom));
	C2_PRE(seg_size <=
	       c2_net_domain_get_max_buffer_segment_size(pool->bp_ndom));
	pool->bp_threshold = threshold;
	pool->bp_free = 0;
	c2_tlist_init(&buf_pool_descr, &pool->bp_head);
	while (++pool->bp_free <= buf_nr){
		C2_ALLOC_PTR(bpi_item);
		if (bpi_item == NULL)
			break;
		C2_ALLOC_PTR(bpi_item->bpi_nb);
		if (bpi_item->bpi_nb == NULL)
			break;
		nb = bpi_item->bpi_nb;
		rc = c2_bufvec_alloc(&nb->nb_buffer, seg_nr, seg_size);
		if (rc != 0)
			break;
		rc = c2_net_buffer_register(nb, pool->bp_ndom);
		if (rc != 0)
			break;
		c2_tlink_init(&buf_pool_descr, bpi_item);
		C2_ASSERT(!c2_tlink_is_in(&buf_pool_descr, bpi_item));
		c2_tlist_add_tail(&buf_pool_descr, &pool->bp_head,
				   bpi_item);
	}
	if(rc != 0)
		c2_buf_pool_fini(pool);
	return rc;
}
C2_EXPORTED(c2_buf_pool_init);

void c2_buf_pool_fini(struct c2_buf_pool *pool)
{
	struct c2_net_buffer *nb = NULL;
	struct c2_buf_pool_item *bpi_item;
	C2_PRE(pool != NULL);
	c2_tlist_for(&buf_pool_descr, &pool->bp_head, bpi_item) {
		nb = bpi_item->bpi_nb;
		c2_net_buffer_deregister(nb, pool->bp_ndom);
		c2_bufvec_free(&nb->nb_buffer);
		c2_tlist_del(&buf_pool_descr, bpi_item);
		c2_tlink_fini(&buf_pool_descr, bpi_item);
		c2_free(nb);
		c2_free(bpi_item);
		pool->bp_free--;
	}c2_tlist_endfor;
	c2_tlist_fini(&buf_pool_descr, &pool->bp_head);
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
	struct c2_net_buffer *nb = NULL;
	struct c2_buf_pool_item *bpi_item;

	C2_PRE(pool != NULL);
	if(pool->bp_free <= 0)
		return NULL;

	bpi_item = c2_tlist_head(&buf_pool_descr, &pool->bp_head);
	C2_ASSERT(bpi_item != NULL);
	c2_tlist_del(&buf_pool_descr, bpi_item);
	c2_tlink_fini(&buf_pool_descr, bpi_item);
	nb = bpi_item->bpi_nb;
	c2_free(bpi_item);
	pool->bp_free--;
	if(pool->bp_free < pool->bp_threshold) {
		pool->bp_ops->bpo_below_threshold(pool);
	}
	return nb;
}
C2_EXPORTED(c2_buf_pool_get);

void c2_buf_pool_put(struct c2_buf_pool *pool, struct c2_net_buffer *buf)
{
	struct c2_buf_pool_item *bpi_item;
	C2_PRE(pool != NULL);
	C2_PRE(buf != NULL);
	C2_ALLOC_PTR(bpi_item);
	C2_ASSERT(bpi_item != NULL);
	bpi_item->bpi_nb = buf;
	c2_tlink_init(&buf_pool_descr, bpi_item);
	C2_ASSERT(!c2_tlink_is_in(&buf_pool_descr, bpi_item));
	c2_tlist_add_tail(&buf_pool_descr, &pool->bp_head, bpi_item);
	pool->bp_free++;
	if(pool->bp_free > 0)
		pool->bp_ops->bpo_not_empty(pool);
	c2_tlist_for(&buf_pool_descr, &pool->bp_head, bpi_item) {
	}c2_tlist_endfor;
}
C2_EXPORTED(c2_buf_pool_put);

void c2_buf_pool_add(struct c2_buf_pool *pool,struct c2_net_buffer *buf)
{
	int rc;
	C2_PRE(buf != NULL);
	rc = c2_net_buffer_register(buf, pool->bp_ndom);
	C2_ASSERT(rc == 0);
	c2_buf_pool_put(pool, buf);
}
C2_EXPORTED(c2_buf_pool_add);

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
