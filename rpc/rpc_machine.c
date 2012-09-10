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
 * Original creation date: 06/28/2012
 */

/**
   @addtogroup rpc_layer_core

   @{
 */

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/misc.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/finject.h"       /* C2_FI_ENABLED */
#include "addb/addb.h"
#include "net/net.h"
#include "net/buffer_pool.h"   /* c2_net_buffer_pool_[lock|unlock] */
#include "rpc/rpc_machine.h"
#include "rpc/formation2.h"    /* c2_rpc_frm_run_formation */
#include "rpc/rpc_onwire.h"    /* c2_rpc_decode */
#include "rpc/service.h"       /* c2_rpc_services_tlist_.* */
#include "rpc/packet.h"        /* c2_rpc */
#include "rpc/rpc2.h"          /* c2_rpc_max_msg_size, c2_rpc_max_recv_msgs */

/* Forward declarations. */
static void rpc_tm_cleanup(struct c2_rpc_machine *machine);
static int rpc_tm_setup(struct c2_net_transfer_mc *tm,
			struct c2_net_domain      *net_dom,
			const char                *ep_addr,
			struct c2_net_buffer_pool *pool,
			uint32_t                   colour,
			c2_bcount_t                msg_size,
			uint32_t                   qlen);
static void __rpc_machine_init(struct c2_rpc_machine *machine);
static void __rpc_machine_fini(struct c2_rpc_machine *machine);
static int root_session_cob_create(struct c2_cob_domain *dom);
static void conn_list_fini(struct c2_list *list);
static void frm_worker_fn(struct c2_rpc_machine *machine);
static struct c2_rpc_chan *rpc_chan_locate(struct c2_rpc_machine *machine,
					   struct c2_net_end_point *dest_ep);
static int rpc_chan_create(struct c2_rpc_chan **chan,
			   struct c2_rpc_machine *machine,
			   struct c2_net_end_point *dest_ep,
			   uint64_t max_packets_in_flight);
static void rpc_chan_ref_release(struct c2_ref *ref);
static void rpc_recv_pool_buffer_put(struct c2_net_buffer *nb);
static void net_buf_event_handler(const struct c2_net_buffer_event *ev);
static void net_buf_received(struct c2_net_buffer    *nb,
			     c2_bindex_t              offset,
			     c2_bcount_t              length,
			     struct c2_net_end_point *from_ep);
static void packet_received(struct c2_rpc_packet    *p,
			    struct c2_rpc_machine   *machine,
			    struct c2_net_end_point *from_ep);
static void item_received(struct c2_rpc_item      *item,
			  struct c2_rpc_machine   *machine,
			  struct c2_net_end_point *from_ep);
static void net_buf_err(struct c2_net_buffer *nb, int32_t status);

/* ADDB Instrumentation for rpccore. */
static const struct c2_addb_ctx_type rpc_machine_addb_ctx_type = {
        .act_name = "rpc-machine"
};

const struct c2_addb_loc c2_rpc_machine_addb_loc = {
        .al_name = "rpc-machine"
};

C2_ADDB_EV_DEFINE_PUBLIC(c2_rpc_machine_func_fail, "rpc_machine_func_fail",
			 C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

static const struct c2_bob_type rpc_machine_bob_type = {
	.bt_name         = "rpc_machine",
	.bt_magix_offset = C2_MAGIX_OFFSET(struct c2_rpc_machine, rm_magix),
	.bt_magix        = C2_RPC_MACHINE_MAGIX,
	.bt_check        = NULL
};

C2_BOB_DEFINE(/* global */, &rpc_machine_bob_type, c2_rpc_machine);

/**
   Buffer callback for buffers added by rpc layer for receiving messages.
 */
const struct c2_net_buffer_callbacks c2_rpc_rcv_buf_callbacks = {
	.nbc_cb = {
		[C2_NET_QT_MSG_RECV] = net_buf_event_handler,
	}
};

static void rpc_tm_event_cb(const struct c2_net_tm_event *ev)
{
	/* Do nothing */
}

/**
    Transfer machine callback vector for transfer machines created by
    rpc layer.
 */
static struct c2_net_tm_callbacks c2_rpc_tm_callbacks = {
	.ntc_event_cb = rpc_tm_event_cb
};

static void rmachine_addb_failure(struct c2_rpc_machine *machine,
				  const char            *msg,
				  int                    rc)
{
	C2_ADDB_ADD(&machine->rm_addb, &c2_rpc_machine_addb_loc,
		    c2_rpc_machine_func_fail, msg, rc);
}

int c2_rpc_machine_init(struct c2_rpc_machine     *machine,
			struct c2_cob_domain      *dom,
			struct c2_net_domain      *net_dom,
			const char                *ep_addr,
			struct c2_reqh            *reqh,
			struct c2_net_buffer_pool *receive_pool,
			uint32_t		   colour,
			c2_bcount_t		   msg_size,
			uint32_t		   queue_len)
{
	int		rc;

	C2_PRE(dom	    != NULL);
	C2_PRE(machine	    != NULL);
	C2_PRE(ep_addr	    != NULL);
	C2_PRE(net_dom	    != NULL);
	C2_PRE(receive_pool != NULL);

	if (C2_FI_ENABLED("fake_error"))
		return -EINVAL;

	C2_SET0(machine);
	machine->rm_dom		  = dom;
	machine->rm_reqh	  = reqh;
	machine->rm_min_recv_size = c2_rpc_max_msg_size(net_dom, msg_size);

	__rpc_machine_init(machine);

	machine->rm_stopping = false;
	rc = C2_THREAD_INIT(&machine->rm_frm_worker, struct c2_rpc_machine *,
			    NULL, &frm_worker_fn, machine, "frm_worker");
	if (rc != 0)
		goto out_fini;

	rc = rpc_tm_setup(&machine->rm_tm, net_dom, ep_addr, receive_pool,
			  colour, msg_size, queue_len);
	if (rc != 0)
		goto out_stop_frm_worker;

	rc = root_session_cob_create(dom);
	if (rc != 0)
		goto out_tm_cleanup;

	C2_ASSERT(rc == 0);
	return rc;

out_tm_cleanup:
	rpc_tm_cleanup(machine);

out_stop_frm_worker:
	machine->rm_stopping = true;
	c2_thread_join(&machine->rm_frm_worker);

out_fini:
	__rpc_machine_fini(machine);
	C2_ASSERT(rc != 0);
	return rc;
}
C2_EXPORTED(c2_rpc_machine_init);

static void __rpc_machine_init(struct c2_rpc_machine *machine)
{
	c2_list_init(&machine->rm_chans);
	c2_list_init(&machine->rm_incoming_conns);
	c2_list_init(&machine->rm_outgoing_conns);
	c2_rpc_services_tlist_init(&machine->rm_services);
	c2_addb_ctx_init(&machine->rm_addb, &rpc_machine_addb_ctx_type,
			 &c2_addb_global_ctx);
	c2_sm_group_init(&machine->rm_sm_grp);
	c2_rpc_machine_bob_init(machine);
}

static void __rpc_machine_fini(struct c2_rpc_machine *machine)
{
	c2_sm_group_fini(&machine->rm_sm_grp);
	c2_addb_ctx_fini(&machine->rm_addb);
	c2_rpc_services_tlist_fini(&machine->rm_services);
	c2_list_fini(&machine->rm_outgoing_conns);
	c2_list_fini(&machine->rm_incoming_conns);
	c2_list_fini(&machine->rm_chans);
	c2_rpc_machine_bob_fini(machine);
}

static int root_session_cob_create(struct c2_cob_domain *dom)
{
	int rc = 0;
#ifndef __KERNEL__
	struct c2_db_tx tx;

	rc = c2_db_tx_init(&tx, dom->cd_dbenv, 0);
	if (rc == 0) {
		rc = c2_rpc_root_session_cob_create(dom, &tx);
		if (rc == 0)
			c2_db_tx_commit(&tx);
		else
			c2_db_tx_abort(&tx);
	}
#endif
	return rc;
}

void c2_rpc_machine_fini(struct c2_rpc_machine *machine)
{
	C2_PRE(machine != NULL);

	c2_rpc_machine_lock(machine);
	machine->rm_stopping = true;
	c2_rpc_machine_unlock(machine);

	c2_thread_join(&machine->rm_frm_worker);

	c2_rpc_machine_lock(machine);
	C2_PRE(c2_list_is_empty(&machine->rm_outgoing_conns));
	conn_list_fini(&machine->rm_incoming_conns);
	c2_rpc_machine_unlock(machine);

	rpc_tm_cleanup(machine);
	__rpc_machine_fini(machine);
}
C2_EXPORTED(c2_rpc_machine_fini);

/**
   Worker thread that runs formation periodically on all formation machines,
   in an attempt to send timedout items.

   XXX This entire routine is temporary. The item deadline timeout mechanism
       should be based on generic sm framework.
 */
static void frm_worker_fn(struct c2_rpc_machine *machine)
{
	struct c2_rpc_chan *chan;
	enum { MILLI_SEC = 1000 * 1000 };

	C2_PRE(machine != NULL);

	while (true) {
		c2_rpc_machine_lock(machine);
		if (machine->rm_stopping) {
			c2_rpc_machine_unlock(machine);
			return;
		}
		c2_list_for_each_entry(&machine->rm_chans, chan,
				       struct c2_rpc_chan, rc_linkage) {
			c2_rpc_frm_run_formation(&chan->rc_frm);
		}
		c2_rpc_machine_unlock(machine);
		c2_nanosleep(c2_time(0, 100 * MILLI_SEC), NULL);
	}
}

static int rpc_tm_setup(struct c2_net_transfer_mc *tm,
			struct c2_net_domain      *net_dom,
			const char                *ep_addr,
			struct c2_net_buffer_pool *pool,
			uint32_t                   colour,
			c2_bcount_t                msg_size,
			uint32_t                   qlen)
{
	struct c2_clink tmwait;
	int             rc;

	C2_PRE(tm != NULL && net_dom != NULL && ep_addr != NULL);
	C2_PRE(pool != NULL);

	tm->ntm_state     = C2_NET_TM_UNDEFINED;
	tm->ntm_callbacks = &c2_rpc_tm_callbacks;

	rc = c2_net_tm_init(tm, net_dom);
	if (rc < 0)
		return rc;

	rc = c2_net_tm_pool_attach(tm, pool,
				   &c2_rpc_rcv_buf_callbacks,
				   c2_rpc_max_msg_size(net_dom, msg_size),
				   c2_rpc_max_recv_msgs(net_dom, msg_size),
				   qlen);
	if (rc < 0) {
		c2_net_tm_fini(tm);
		return rc;
	}

	c2_net_tm_colour_set(tm, colour);

	/* Start the transfer machine so that users of this rpc_machine
	   can send/receive messages. */
	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&tm->ntm_chan, &tmwait);

	rc = c2_net_tm_start(tm, ep_addr);
	if (rc == 0) {
		while (tm->ntm_state != C2_NET_TM_STARTED &&
		       tm->ntm_state != C2_NET_TM_FAILED)
			c2_chan_wait(&tmwait);
	}
	c2_clink_del(&tmwait);
	c2_clink_fini(&tmwait);

	if (tm->ntm_state == C2_NET_TM_FAILED) {
		/** @todo Find more appropriate err code.
		    tm does not report cause of failure.
		 */
		rc = -ENETUNREACH;
		c2_net_tm_fini(tm);
	}
	return rc;
}

static void rpc_tm_cleanup(struct c2_rpc_machine *machine)
{
	int			   rc;
	struct c2_clink		   tmwait;
	struct c2_net_transfer_mc *tm = &machine->rm_tm;

	C2_PRE(machine != NULL);

	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&tm->ntm_chan, &tmwait);

	rc = c2_net_tm_stop(tm, true);

	if (rc < 0) {
		c2_clink_del(&tmwait);
		c2_clink_fini(&tmwait);
		C2_ADDB_ADD(&machine->rm_addb, &c2_rpc_machine_addb_loc,
			    c2_rpc_machine_func_fail, "c2_net_tm_stop", rc);
		return;
	}
	/* Wait for transfer machine to stop. */
	while (tm->ntm_state != C2_NET_TM_STOPPED &&
	       tm->ntm_state != C2_NET_TM_FAILED)
		c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	c2_clink_fini(&tmwait);

	/* Fini the transfer machine here and deallocate the chan. */
	c2_net_tm_fini(tm);
}

/**
   XXX Temporary. This routine will be discarded, once rpc-core starts
   providing c2_rpc_item::ri_ops::rio_sent() callback.

   In-memory state of conn should be cleaned up when reply to CONN_TERMINATE
   has been sent. As of now, rpc-core does not provide this callback. So this
   is a temporary routine, that cleans up all terminated connections from
   rpc connection list maintained in rpc_machine.
 */
static void conn_list_fini(struct c2_list *list)
{
        struct c2_rpc_conn *conn;
        struct c2_rpc_conn *conn_next;

        C2_PRE(list != NULL);

        c2_list_for_each_entry_safe(list, conn, conn_next, struct c2_rpc_conn,
				    c_link) {
                c2_rpc_conn_terminate_reply_sent(conn);
        }
}

struct c2_mutex *c2_rpc_machine_mutex(struct c2_rpc_machine *machine)
{
	return &machine->rm_sm_grp.s_lock;
}

void c2_rpc_machine_lock(struct c2_rpc_machine *machine)
{
	C2_PRE(machine != NULL);

	c2_sm_group_lock(&machine->rm_sm_grp);
}

void c2_rpc_machine_unlock(struct c2_rpc_machine *machine)
{
	C2_PRE(machine != NULL);

	c2_sm_group_unlock(&machine->rm_sm_grp);
}

bool c2_rpc_machine_is_locked(const struct c2_rpc_machine *machine)
{
	C2_PRE(machine != NULL);
	return c2_mutex_is_locked(&machine->rm_sm_grp.s_lock);
}
C2_EXPORTED(c2_rpc_machine_is_locked);

struct c2_rpc_chan *rpc_chan_get(struct c2_rpc_machine *machine,
				 struct c2_net_end_point *dest_ep,
				 uint64_t max_packets_in_flight)
{
	struct c2_rpc_chan	*chan;

	C2_PRE(dest_ep != NULL);
	C2_PRE(c2_rpc_machine_is_locked(machine));

	chan = rpc_chan_locate(machine, dest_ep);
	if (chan == NULL)
		rpc_chan_create(&chan, machine, dest_ep, max_packets_in_flight);

	return chan;
}

static struct c2_rpc_chan *rpc_chan_locate(struct c2_rpc_machine *machine,
					   struct c2_net_end_point *dest_ep)
{
	struct c2_rpc_chan *chan;
	bool                found;

	C2_PRE(dest_ep != NULL);
	C2_PRE(c2_rpc_machine_is_locked(machine));

	found = false;
	/* Locate the chan from rpc_machine->chans list. */
	c2_list_for_each_entry(&machine->rm_chans, chan, struct c2_rpc_chan,
			       rc_linkage) {
		C2_ASSERT(chan->rc_destep->nep_tm->ntm_dom ==
			  dest_ep->nep_tm->ntm_dom);
		if (chan->rc_destep == dest_ep) {
			c2_ref_get(&chan->rc_ref);
			c2_net_end_point_get(chan->rc_destep);
			found = true;
			break;
		}
	}

	return found ? chan : NULL;
}

static int rpc_chan_create(struct c2_rpc_chan **chan,
			   struct c2_rpc_machine *machine,
			   struct c2_net_end_point *dest_ep,
			   uint64_t max_packets_in_flight)
{
	struct c2_rpc_frm_constraints  constraints;
	struct c2_net_domain          *ndom;
	struct c2_rpc_chan            *ch;

	C2_PRE(chan != NULL);
	C2_PRE(dest_ep != NULL);

	C2_PRE(c2_rpc_machine_is_locked(machine));

	C2_ALLOC_PTR_ADDB(ch, &machine->rm_addb, &c2_rpc_machine_addb_loc);
	if (ch == NULL) {
		*chan = NULL;
		return -ENOMEM;
	}

	ch->rc_rpc_machine = machine;
	ch->rc_destep = dest_ep;
	c2_ref_init(&ch->rc_ref, 1, rpc_chan_ref_release);
	c2_net_end_point_get(dest_ep);

	ndom = machine->rm_tm.ntm_dom;

	constraints.fc_max_nr_packets_enqed = max_packets_in_flight;
	constraints.fc_max_packet_size = machine->rm_min_recv_size;
	constraints.fc_max_nr_bytes_accumulated =
				constraints.fc_max_packet_size;
	constraints.fc_max_nr_segments =
				c2_net_domain_get_max_buffer_segments(ndom);

	c2_rpc_frm_init(&ch->rc_frm, &constraints, &c2_rpc_frm_default_ops);
	c2_list_add(&machine->rm_chans, &ch->rc_linkage);
	*chan = ch;
	return 0;
}

void rpc_chan_put(struct c2_rpc_chan *chan)
{
	struct c2_rpc_machine *machine;

	C2_PRE(chan != NULL);

	machine = chan->rc_rpc_machine;
	C2_PRE(c2_rpc_machine_is_locked(machine));

	c2_net_end_point_put(chan->rc_destep);
	c2_ref_put(&chan->rc_ref);
}

static void rpc_chan_ref_release(struct c2_ref *ref)
{
	struct c2_rpc_chan *chan;

	C2_PRE(ref != NULL);

	chan = container_of(ref, struct c2_rpc_chan, rc_ref);
	C2_ASSERT(chan != NULL);
	C2_ASSERT(c2_rpc_machine_is_locked(chan->rc_rpc_machine));

	c2_list_del(&chan->rc_linkage);
	c2_rpc_frm_fini(&chan->rc_frm);
	c2_free(chan);
}

static void net_buf_event_handler(const struct c2_net_buffer_event *ev)
{
	struct c2_net_buffer *nb;
	bool                  buf_is_queued;

	nb = ev->nbe_buffer;
	C2_PRE(nb != NULL);

	if (ev->nbe_status == 0) {
		net_buf_received(nb, ev->nbe_offset, ev->nbe_length,
				 ev->nbe_ep);
	} else {
		if (ev->nbe_status != -ECANCELED)
			net_buf_err(nb, ev->nbe_status);
	}
	buf_is_queued = (nb->nb_flags & C2_NET_BUF_QUEUED);
	if (!buf_is_queued)
		rpc_recv_pool_buffer_put(nb);
}

static struct c2_rpc_machine *
tm_to_rpc_machine(const struct c2_net_transfer_mc *tm)
{
	return container_of(tm, struct c2_rpc_machine, rm_tm);
}

static void net_buf_received(struct c2_net_buffer    *nb,
			     c2_bindex_t              offset,
			     c2_bcount_t              length,
			     struct c2_net_end_point *from_ep)
{
	struct c2_rpc_machine *machine;
	struct c2_rpc_packet   p;
	int                    rc;

	machine = tm_to_rpc_machine(nb->nb_tm);
	c2_rpc_packet_init(&p);
	rc = c2_rpc_packet_decode(&p, &nb->nb_buffer, offset, length);
	if (rc != 0)
		rmachine_addb_failure(machine, "Buffer decode failed", rc);
	/* There might be items in packet p, which were successfully decoded
	   before an error occured. */
	packet_received(&p, machine, from_ep);
	c2_rpc_packet_fini(&p);
}

static void packet_received(struct c2_rpc_packet    *p,
			    struct c2_rpc_machine   *machine,
			    struct c2_net_end_point *from_ep)
{
	struct c2_rpc_item *item;

	machine->rm_rpc_stats[C2_RPC_PATH_INCOMING].rs_rpcs_nr++;
	/* packet p can also be empty */
	for_each_item_in_packet(item, p) {
		c2_rpc_packet_remove_item(p, item);
		item_received(item, machine, from_ep);
	} end_for_each_item_in_packet;
}

static void item_received(struct c2_rpc_item      *item,
			  struct c2_rpc_machine   *machine,
			  struct c2_net_end_point *from_ep)
{
	int rc;

	if (c2_rpc_item_is_conn_establish(item))
		c2_rpc_fop_conn_establish_ctx_init(item, from_ep, machine);

	item->ri_rpc_time = c2_time_now();

	c2_rpc_machine_lock(machine);
	c2_rpc_item_sm_init(item, &machine->rm_sm_grp);
	rc = c2_rpc_item_received(item, machine);
	if (rc == 0)
		c2_rpc_item_change_state(item, C2_RPC_ITEM_ACCEPTED);
	else
		c2_rpc_item_free(item);
	c2_rpc_machine_unlock(machine);
}

static void net_buf_err(struct c2_net_buffer *nb, int32_t status)
{
	struct c2_rpc_machine *machine;

	machine = tm_to_rpc_machine(nb->nb_tm);
	rmachine_addb_failure(machine, "Buffer event reported failure", status);
}

/* Put buffer back into the pool */
static void rpc_recv_pool_buffer_put(struct c2_net_buffer *nb)
{
	struct c2_net_transfer_mc *tm;
	C2_PRE(nb != NULL);
	tm = nb->nb_tm;

	C2_PRE(tm != NULL);
	C2_PRE(tm->ntm_recv_pool != NULL && nb->nb_pool != NULL);
	C2_PRE(tm->ntm_recv_pool == nb->nb_pool);

	nb->nb_ep = NULL;
	c2_net_buffer_pool_lock(tm->ntm_recv_pool);
	c2_net_buffer_pool_put(tm->ntm_recv_pool, nb,
			       tm->ntm_pool_colour);
	c2_net_buffer_pool_unlock(tm->ntm_recv_pool);
}

/**
  Set the stats for outgoing rpc item
  @param item - incoming or outgoing rpc item
  @param path - enum distinguishing whether the item is incoming or outgoing
 */
void item_exit_stats_set(struct c2_rpc_item *item,
			 enum c2_rpc_item_path path)
{
	struct c2_rpc_machine *machine;
	struct c2_rpc_stats   *st;

	C2_PRE(item != NULL && item->ri_session != NULL);

	machine = item->ri_session->s_conn->c_rpc_machine;
	C2_ASSERT(c2_rpc_machine_is_locked(machine));

	C2_PRE(IS_IN_ARRAY(path, machine->rm_rpc_stats));

	item->ri_rpc_time = c2_time_sub(c2_time_now(), item->ri_rpc_time);

	st = &machine->rm_rpc_stats[path];
        st->rs_cumu_lat += item->ri_rpc_time;
	st->rs_min_lat = st->rs_min_lat ? : item->ri_rpc_time;
	st->rs_min_lat = min64u(st->rs_min_lat, item->ri_rpc_time);
	st->rs_max_lat = st->rs_max_lat ? : item->ri_rpc_time;
	st->rs_max_lat = max64u(st->rs_max_lat, item->ri_rpc_time);

        st->rs_items_nr++;
        st->rs_bytes_nr += c2_rpc_item_size(item);
}

size_t c2_rpc_bytes_per_sec(struct c2_rpc_machine *machine,
			    const enum c2_rpc_item_path path)
{
	struct c2_rpc_stats *stats;

	C2_PRE(machine != NULL);
	C2_PRE(IS_IN_ARRAY(path, machine->rm_rpc_stats));

	stats = &machine->rm_rpc_stats[path];
	return stats->rs_bytes_nr / stats->rs_cumu_lat;
}

c2_time_t c2_rpc_avg_item_time(struct c2_rpc_machine *machine,
			       const enum c2_rpc_item_path path)
{
	struct c2_rpc_stats *stats;

	C2_PRE(machine != NULL);
	C2_PRE(IS_IN_ARRAY(path, machine->rm_rpc_stats));

	stats = &machine->rm_rpc_stats[path];
	return stats->rs_cumu_lat / stats->rs_items_nr;
}

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
