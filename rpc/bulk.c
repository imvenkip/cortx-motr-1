/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 *		    Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 06/27/2012
 */

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/tlist.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"                      /* C2_IN */
#include "colibri/magic.h"
#include "net/net.h"
#include "rpc/bulk.h"
#include "rpc/session.h"
#include "rpc/rpc2.h"

/**
   @addtogroup rpc_layer_core

   @{
 */

C2_TL_DESCR_DEFINE(rpcbulk, "rpc bulk buffer list", ,
		   struct c2_rpc_bulk_buf, bb_link, bb_magic,
		   C2_RPC_BULK_BUF_MAGIC, C2_RPC_BULK_MAGIC);

C2_EXPORTED(rpcbulk_tl);

C2_TL_DEFINE(rpcbulk, , struct c2_rpc_bulk_buf);

static bool rpc_bulk_buf_invariant(const struct c2_rpc_bulk_buf *rbuf)
{
	return rbuf != NULL &&
		rbuf->bb_magic == C2_RPC_BULK_BUF_MAGIC &&
		rbuf->bb_rbulk != NULL &&
		rpcbulk_tlink_is_in(rbuf);
}

static bool rpc_bulk_invariant(const struct c2_rpc_bulk *rbulk)
{
	return
		rbulk != NULL && rbulk->rb_magic == C2_RPC_BULK_MAGIC &&
		c2_mutex_is_locked(&rbulk->rb_mutex) &&
		c2_tl_forall(rpcbulk, buf, &rbulk->rb_buflist,
			     rpc_bulk_buf_invariant(buf) &&
			     buf->bb_rbulk == rbulk);
}

static void rpc_bulk_buf_deregister(struct c2_rpc_bulk_buf *buf)
{
	if (buf->bb_flags & C2_RPC_BULK_NETBUF_REGISTERED) {
		c2_net_buffer_deregister(buf->bb_nbuf, buf->bb_nbuf->nb_dom);
		buf->bb_flags &= ~C2_RPC_BULK_NETBUF_REGISTERED;
	}
}
static void rpc_bulk_buf_fini(struct c2_rpc_bulk_buf *rbuf)
{
	struct c2_net_buffer *nbuf = rbuf->bb_nbuf;

	C2_ENTRY("bulk_buf: %p", rbuf);
	C2_PRE(rbuf != NULL);
	C2_PRE(!(nbuf->nb_flags & C2_NET_BUF_QUEUED));

	c2_net_desc_free(&nbuf->nb_desc);
	c2_0vec_fini(&rbuf->bb_zerovec);
	rpc_bulk_buf_deregister(rbuf);
	if (rbuf->bb_flags & C2_RPC_BULK_NETBUF_ALLOCATED)
		c2_free(nbuf);
	c2_free(rbuf);
	C2_LEAVE();
}

static int rpc_bulk_buf_init(struct c2_rpc_bulk_buf *rbuf, uint32_t segs_nr,
			     struct c2_net_buffer *nb)
{
	int		rc;
	uint32_t	i;
	struct c2_buf	cbuf;
	c2_bindex_t	index = 0;

	C2_ENTRY("bulk_buf: %p, net_buf: %p", rbuf, nb);
	C2_PRE(rbuf != NULL);
	C2_PRE(segs_nr > 0);

	rc = c2_0vec_init(&rbuf->bb_zerovec, segs_nr);
	if (rc != 0)
		C2_RETERR(rc, "bulk_buf: Zero vector initialization");

	rbuf->bb_flags = 0;
	if (nb == NULL) {
		C2_ALLOC_PTR(rbuf->bb_nbuf);
		if (rbuf->bb_nbuf == NULL) {
			c2_0vec_fini(&rbuf->bb_zerovec);
			C2_RETURN(-ENOMEM);
		}
		rbuf->bb_flags |= C2_RPC_BULK_NETBUF_ALLOCATED;
		rbuf->bb_nbuf->nb_buffer = rbuf->bb_zerovec.z_bvec;
	} else {
		rbuf->bb_nbuf = nb;
		/*
		 * Incoming buffer can be bigger while the bulk transfer
		 * request could refer to smaller size. Hence initialize
		 * the zero vector to get correct size of bulk transfer.
		 */
		for (i = 0; i < segs_nr; ++i) {
			cbuf.b_addr = nb->nb_buffer.ov_buf[i];
			cbuf.b_nob = nb->nb_buffer.ov_vec.v_count[i];
			rc = c2_0vec_cbuf_add(&rbuf->bb_zerovec, &cbuf, &index);
			if (rc != 0) {
				c2_0vec_fini(&rbuf->bb_zerovec);
				C2_RETERR(rc, "Addition of cbuf");
			}
		}
	}

	rpcbulk_tlink_init(rbuf);
	rbuf->bb_magic = C2_RPC_BULK_BUF_MAGIC;
	C2_RETURN(rc);
}

static void rpc_bulk_buf_cb(const struct c2_net_buffer_event *evt)
{
	struct c2_rpc_bulk	*rbulk;
	struct c2_rpc_bulk_buf	*buf;
	struct c2_net_buffer	*nb;
	bool			 receiver;

	C2_ENTRY("net_buf_evt: %p", evt);
	C2_PRE(evt != NULL);
	C2_PRE(evt->nbe_buffer != NULL);

	nb = evt->nbe_buffer;
	buf = (struct c2_rpc_bulk_buf *)nb->nb_app_private;
	rbulk = buf->bb_rbulk;

	C2_ASSERT(rpc_bulk_buf_invariant(buf));
	C2_ASSERT(rpcbulk_tlink_is_in(buf));

	if (C2_IN(nb->nb_qtype, (C2_NET_QT_PASSIVE_BULK_RECV,
				 C2_NET_QT_ACTIVE_BULK_RECV)))
		nb->nb_length = evt->nbe_length;

	receiver = C2_IN(nb->nb_qtype, (C2_NET_QT_ACTIVE_BULK_RECV,
					C2_NET_QT_ACTIVE_BULK_SEND));

	c2_mutex_lock(&rbulk->rb_mutex);
	C2_ASSERT(rpc_bulk_invariant(rbulk));
	/*
	 * Change the status code of struct c2_rpc_bulk only if it is
	 * zero so far. This will ensure that return code of first failure
	 * from list of net buffers in struct c2_rpc_bulk will be maintained.
	 * Buffers are canceled by io coalescing code which in turn sends
	 * a coalesced buffer and cancels member buffers. Hence -ECANCELED
	 * is not treated as an error here.
	 */
	if (rbulk->rb_rc == 0 && evt->nbe_status != -ECANCELED)
		rbulk->rb_rc = evt->nbe_status;

	rpcbulk_tlist_del(buf);
	if (receiver) {
		C2_ASSERT(c2_chan_has_waiters(&rbulk->rb_chan));
		if (rpcbulk_tlist_is_empty(&rbulk->rb_buflist))
			c2_chan_signal(&rbulk->rb_chan);
	}
	rpc_bulk_buf_deregister(buf);

	rpc_bulk_buf_fini(buf);
	c2_mutex_unlock(&rbulk->rb_mutex);
	C2_LEAVE();
}

const struct c2_net_buffer_callbacks rpc_bulk_cb  = {
	.nbc_cb = {
		[C2_NET_QT_PASSIVE_BULK_SEND] = rpc_bulk_buf_cb,
		[C2_NET_QT_PASSIVE_BULK_RECV] = rpc_bulk_buf_cb,
		[C2_NET_QT_ACTIVE_BULK_RECV]  = rpc_bulk_buf_cb,
		[C2_NET_QT_ACTIVE_BULK_SEND]  = rpc_bulk_buf_cb,
	}
};

void c2_rpc_bulk_init(struct c2_rpc_bulk *rbulk)
{
	C2_ENTRY("rbulk: %p", rbulk);
	C2_PRE(rbulk != NULL);

	rpcbulk_tlist_init(&rbulk->rb_buflist);
	c2_chan_init(&rbulk->rb_chan);
	c2_mutex_init(&rbulk->rb_mutex);
	rbulk->rb_magic = C2_RPC_BULK_MAGIC;
	rbulk->rb_bytes = 0;
	rbulk->rb_rc = 0;
	C2_LEAVE();
}
C2_EXPORTED(c2_rpc_bulk_init);

void c2_rpc_bulk_fini(struct c2_rpc_bulk *rbulk)
{
	C2_ENTRY("rbulk: %p", rbulk);
	C2_PRE(rbulk != NULL);
	c2_mutex_lock(&rbulk->rb_mutex);
	C2_PRE(rpc_bulk_invariant(rbulk));
	c2_mutex_unlock(&rbulk->rb_mutex);
	C2_PRE(rpcbulk_tlist_is_empty(&rbulk->rb_buflist));

	c2_chan_fini(&rbulk->rb_chan);
	c2_mutex_fini(&rbulk->rb_mutex);
	rpcbulk_tlist_fini(&rbulk->rb_buflist);
	C2_LEAVE();
}
C2_EXPORTED(c2_rpc_bulk_fini);

void c2_rpc_bulk_buflist_empty(struct c2_rpc_bulk *rbulk)
{
	struct c2_rpc_bulk_buf *buf;

	c2_mutex_lock(&rbulk->rb_mutex);
	C2_ASSERT(rpc_bulk_invariant(rbulk));
	c2_tl_for(rpcbulk, &rbulk->rb_buflist, buf) {
		rpcbulk_tlist_del(buf);
		rpc_bulk_buf_fini(buf);
	} c2_tl_endfor;
	c2_mutex_unlock(&rbulk->rb_mutex);
}

int c2_rpc_bulk_buf_add(struct c2_rpc_bulk *rbulk,
			uint32_t segs_nr,
			struct c2_net_domain *netdom,
			struct c2_net_buffer *nb,
			struct c2_rpc_bulk_buf **out)
{
	int			rc;
	struct c2_rpc_bulk_buf *buf;

	C2_ENTRY("rbulk: %p, net_dom: %p, net_buf: %p", rbulk, netdom,
		 nb);
	C2_PRE(rbulk != NULL);
	C2_PRE(netdom != NULL);
	C2_PRE(out != NULL);

	if (segs_nr > c2_net_domain_get_max_buffer_segments(netdom))
		C2_RETERR(-EMSGSIZE, "Cannot exceed net_max_buf_seg");

	C2_ALLOC_PTR(buf);
	if (buf == NULL)
		C2_RETURN(-ENOMEM);

	rc = rpc_bulk_buf_init(buf, segs_nr, nb);
	if (rc != 0) {
		c2_free(buf);
		C2_RETURN(rc);
	}

	c2_mutex_lock(&rbulk->rb_mutex);
	buf->bb_rbulk = rbulk;
	rpcbulk_tlist_add_tail(&rbulk->rb_buflist, buf);
	C2_POST(rpc_bulk_invariant(rbulk));
	c2_mutex_unlock(&rbulk->rb_mutex);
	*out = buf;
	C2_POST(rpc_bulk_buf_invariant(buf));
	C2_RETURN(0);
}
C2_EXPORTED(c2_rpc_bulk_buf_add);

int c2_rpc_bulk_buf_databuf_add(struct c2_rpc_bulk_buf *rbuf,
			        void *buf,
			        c2_bcount_t count,
			        c2_bindex_t index,
				struct c2_net_domain *netdom)
{
	int			 rc;
	struct c2_buf		 cbuf;
	struct c2_rpc_bulk	*rbulk;

	C2_ENTRY("rbuf: %p, netdom: %p", rbuf, netdom);
	C2_PRE(rbuf != NULL);
	C2_PRE(rpc_bulk_buf_invariant(rbuf));
	C2_PRE(buf != NULL);
	C2_PRE(count != 0);
	C2_PRE(netdom != NULL);

	if (c2_vec_count(&rbuf->bb_zerovec.z_bvec.ov_vec) + count >
	    c2_net_domain_get_max_buffer_size(netdom) ||
	    count > c2_net_domain_get_max_buffer_segment_size(netdom))
		C2_RETERR(-EMSGSIZE, "Cannot exceed net_dom_max_buf_segs");

	cbuf.b_addr = buf;
	cbuf.b_nob = count;
	rbulk = rbuf->bb_rbulk;
	rc = c2_0vec_cbuf_add(&rbuf->bb_zerovec, &cbuf, &index);
	if (rc != 0)
		C2_RETERR(rc, "Addition of cbuf");

	rbuf->bb_nbuf->nb_buffer = rbuf->bb_zerovec.z_bvec;
	C2_POST(rpc_bulk_buf_invariant(rbuf));
	c2_mutex_lock(&rbulk->rb_mutex);
	C2_POST(rpc_bulk_invariant(rbulk));
	c2_mutex_unlock(&rbulk->rb_mutex);
	C2_RETURN(rc);
}
C2_EXPORTED(c2_rpc_bulk_buf_databuf_add);

void c2_rpc_bulk_qtype(struct c2_rpc_bulk *rbulk, enum c2_net_queue_type q)
{
	struct c2_rpc_bulk_buf *rbuf;

	C2_ENTRY("rpc_bulk: %p, qtype: %d", rbulk, q);
	C2_PRE(rbulk != NULL);
	C2_PRE(c2_mutex_is_locked(&rbulk->rb_mutex));
	C2_PRE(!rpcbulk_tlist_is_empty(&rbulk->rb_buflist));
	C2_PRE(C2_IN(q, (C2_NET_QT_PASSIVE_BULK_RECV,
			 C2_NET_QT_PASSIVE_BULK_SEND,
			 C2_NET_QT_ACTIVE_BULK_RECV,
			 C2_NET_QT_ACTIVE_BULK_SEND)));

	c2_tl_for(rpcbulk, &rbulk->rb_buflist, rbuf) {
		rbuf->bb_nbuf->nb_qtype = q;
	} c2_tl_endfor;
	C2_LEAVE();
}

static int rpc_bulk_op(struct c2_rpc_bulk *rbulk,
		       const struct c2_rpc_conn *conn,
		       struct c2_net_buf_desc *descs,
		       enum c2_rpc_bulk_op_type op)
{
	int				 rc;
	int				 cnt = 0;
	struct c2_rpc_bulk_buf		*rbuf;
	struct c2_net_transfer_mc	*tm;
	struct c2_net_buffer		*nb;
	struct c2_net_domain		*netdom;
	struct c2_rpc_machine		*rpcmach;

	C2_ENTRY("rbulk: %p, rpc_conn: %p, rbulk_op_type: %d", rbulk, conn, op);
	C2_PRE(rbulk != NULL);
	C2_PRE(descs != NULL);
	C2_PRE(C2_IN(op, (C2_RPC_BULK_STORE, C2_RPC_BULK_LOAD)));

	rpcmach = conn->c_rpc_machine;
	tm = &rpcmach->rm_tm;
	netdom = tm->ntm_dom;
	c2_mutex_lock(&rbulk->rb_mutex);
	C2_ASSERT(rpc_bulk_invariant(rbulk));
	C2_ASSERT(!rpcbulk_tlist_is_empty(&rbulk->rb_buflist));

	c2_tl_for(rpcbulk, &rbulk->rb_buflist, rbuf) {
		nb = rbuf->bb_nbuf;
		nb->nb_length = c2_vec_count(&rbuf->bb_zerovec.z_bvec.ov_vec);
		C2_ASSERT(rpc_bulk_buf_invariant(rbuf));
		C2_ASSERT(ergo(op == C2_RPC_BULK_STORE, C2_IN(nb->nb_qtype,
				            (C2_NET_QT_PASSIVE_BULK_RECV,
				             C2_NET_QT_PASSIVE_BULK_SEND))) &&
			  ergo(op == C2_RPC_BULK_LOAD, C2_IN(nb->nb_qtype,
					    (C2_NET_QT_ACTIVE_BULK_RECV,
					     C2_NET_QT_ACTIVE_BULK_SEND))));
		nb->nb_callbacks = &rpc_bulk_cb;

		/*
		 * Registers the net buffer with net domain if it is not
		 * registered already.
		 */
		if (!(nb->nb_flags & C2_NET_BUF_REGISTERED)) {
			rc = c2_net_buffer_register(nb, netdom);
			if (rc != 0) {
				C2_ADDB_ADD(&rpcmach->rm_addb,
					    &c2_rpc_machine_addb_loc,
					    c2_rpc_machine_func_fail,
					    "Net buf registration failed.", rc);
				goto cleanup;
			}
			rbuf->bb_flags |= C2_RPC_BULK_NETBUF_REGISTERED;
		}

		if (op == C2_RPC_BULK_LOAD) {
			rc = c2_net_desc_copy(&descs[cnt], &nb->nb_desc);
			if (rc != 0) {
				C2_ADDB_ADD(&rpcmach->rm_addb,
					    &c2_rpc_machine_addb_loc,
					    c2_rpc_machine_func_fail,
					    "Load: Net buf desc copy failed.",
					    rc);
				goto cleanup;
			}
		}

		nb->nb_app_private = rbuf;
		rc = c2_net_buffer_add(nb, tm);
		if (rc != 0) {
			C2_ADDB_ADD(&rpcmach->rm_addb,
				    &c2_rpc_machine_addb_loc,
				    c2_rpc_machine_func_fail,
				    "Buffer addition to TM failed.", rc);
			goto cleanup;
		}

		if (op == C2_RPC_BULK_STORE) {
			rc = c2_net_desc_copy(&nb->nb_desc, &descs[cnt]);
                        if (rc != 0) {
                                C2_ADDB_ADD(&rpcmach->rm_addb,
                                            &c2_rpc_machine_addb_loc,
                                            c2_rpc_machine_func_fail,
                                            "Store: Net buf desc copy failed.",
                                            rc);
                                c2_net_buffer_del(nb, tm);
                                goto cleanup;
                        }
		}

		++cnt;
		rbulk->rb_bytes += nb->nb_length;
	} c2_tl_endfor;
	C2_POST(rpc_bulk_invariant(rbulk));
	c2_mutex_unlock(&rbulk->rb_mutex);

	C2_RETURN(rc);
cleanup:
	C2_ASSERT(rc != 0);
	rpcbulk_tlist_del(rbuf);
	c2_tl_for(rpcbulk, &rbulk->rb_buflist, rbuf) {
		if (rbuf->bb_nbuf->nb_flags & C2_NET_BUF_QUEUED)
			c2_net_buffer_del(rbuf->bb_nbuf, tm);
	} c2_tl_endfor;
	c2_mutex_unlock(&rbulk->rb_mutex);
	C2_RETURN(rc);
}

int c2_rpc_bulk_store(struct c2_rpc_bulk *rbulk,
		      const struct c2_rpc_conn *conn,
		      struct c2_net_buf_desc *to_desc)
{
	return rpc_bulk_op(rbulk, conn, to_desc, C2_RPC_BULK_STORE);
}
C2_EXPORTED(c2_rpc_bulk_store);

int c2_rpc_bulk_load(struct c2_rpc_bulk *rbulk,
		     const struct c2_rpc_conn *conn,
		     struct c2_net_buf_desc *from_desc)
{
	return rpc_bulk_op(rbulk, conn, from_desc, C2_RPC_BULK_LOAD);
}
C2_EXPORTED(c2_rpc_bulk_load);

/** @} end of rpc-layer-core group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
