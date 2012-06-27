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
 * Original author: Nikita_Danilov <Nikita_Danilov@xyratex.com>
 *		    Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 04/28/2011
 */

/**
   @defgroup rpc_layer_core RPC layer core
   @page rpc-layer-core-dld RPC layer core DLD
   @section Overview
   RPC layer core is used to transmitt rpc items and groups of them.

   @section rpc-layer-core-logic Logical specification.
   Typical scenario in terms of current interfaces looks like the following:
   User sends (a) FOPs one by one, or (b)group of FOPs and waits for
   reply from endpoint. Such interactions can be expressed in terms of
   provided interfaces:

   @code
   static void us_callback(struct c2_update_stream *us,
                           struct c2_rpc_item *item)             { ... }
   static const struct c2_update_stream_ops us_ops = {
						.uso_event_cb = us_callback
					    };
   //...
   int ret;
   int i;
   struct c2_rpc_machine mach;
   uint64_t session_id;
   struct c2_update_stream *update_stream;
   struct c2_rpc_item item[] = {DUMMY_INITIALIZER, DUMMY_INITIALIZER, ...};
   struct c2_rpc_group *group;
   struct c2_rpc_item_type_ops item_ops = { .rito_item_cb = item_callback };
   struct c2_time timeout = DUMMY_TIMEOUT;
   // initialising fop operations vectors:
   static struct c2_rpc_item_type_ops fop_item_type_ops;
   static struct c2_rpc_item_type fop_item_type;
   fop_item_type.rit_ops = &fop_item_type_ops;

   // INITIALISATION:
   //
   // initialise rpc layer core internal data structures.
   // c2_rpc_core_init() should be called in core/colibri/init.c
   // and executed as a part of c2_init().

   // create rpc machine.
   ret = c2_rpc_machine_init(&mach, cob_domain, net_dom, ep_addr, recv_pool,
			      colour, rpc_msg_size, tm_que_len);
   // create/get update stream used for interaction between endpoints
   ret = c2_rpc_update_stream_get(&mach, &srvid,
	C2_UPDATE_STREAM_SHARED_SLOT, &us_ops, &update_stream);

   // USAGE (a):
   // sending rpc_items
   item.ri_type = &fop_item_type;
   ret = c2_rpc_submit(&srvid, &update_stream, &item,
	C2_RPC_ITEM_PRIO_MIN, C2_RPC_CACHING_TYPE);
   // waiting for reply:
   ret = c2_rpc_reply_timedwait(&clink, &timeout);

   // USAGE (b):
   // open and generate new group, used in formation.
   ret = c2_rpc_group_open(&mach, &group);

   // send group of items
   for (i = 0; i < ARRAY_SIZE(item); ++i) {
      item[i].ri_type = &fop_item_type;
      ret = c2_rpc_group_submit(&mach, group, &item[i], &srvid, &update_stream,
	C2_RPC_ITEM_PRIO_MIN, C2_RPC_CACHING_TYPE);
   }

   ret = c2_rpc_group_close(&mach, group);

   @endcode

   @section rpc-layer-core-func Functional specification.
   Internally, the RPC layer core should do the following:
   @li put items into sub-caches, associated with specified services;
   @li monitor the occupancy of the sub-caches;
   @li when there is enough pages in a sub-cache to form an optimal rpc,
       form it and send.

   For simple implementation one update stream may be maped onto one slot.
   Several update streams may be mapped onto one slot for more complex cases.

   Update stream state machine:
   @verbatim
      UNINITIALIZED
           | update_stream_init()
           |
           |                     next_item()
           |                  +-----+
           V    next_item()   V     |    timeout
          IDLE------------->SENDING-+------------>TIMEDOUT---+
           | ^                ^next_item()                   |
           | |  revovery done |               retry          |
           | +--------------RECOVERY<------------------------+
           |
           | update_stream_fini()
           V
       FINALIZED
    @endverbatim

   RPC-item state machine:
    @verbatim
      UNINITIALIZED
           | rpc_item_init()
           V
         IN USE
           |
           | c2_rpc_item_submit()
           |
           V   added to RPC    sent over nw      got reply over nw
         SUBMITTED------>ADDED------------->SENT----------------> REPLIED--+
           |               |                                               |
  c2_rpc_item_cancel()     | c2_rpc_item_cancel()                          |
           |               V                           rpc_item_fini()     |
           +----------->FINALIZED<-----------------------------------------+

      @endverbatim

   @see https://docs.google.com/a/xyratex.com/Doc?docid=0AQaCw6YRYSVSZGZmMz
V6NzJfMTljbTZ3anhjbg&hl=en

   @{
*/

#ifndef __COLIBRI_RPC_RPCCORE_H__
#define __COLIBRI_RPC_RPCCORE_H__

#include "lib/cdefs.h"
#include "lib/mutex.h"
#include "lib/list.h"
#include "lib/time.h"
#include "lib/refs.h"
#include "lib/chan.h"
#include "lib/bob.h"
#include "lib/timer.h"
#include "lib/tlist.h"
#include "net/net.h"
#include "cob/cob.h"
#include "rpc/session_internal.h"
#include "rpc/session.h"
#include "addb/addb.h"
#include "rpc/item.h"
#include "rpc/formation2.h"     /* c2_rpc_frm */
#include "rpc/formation.h"
#include "rpc/bulk.h"
#include "net/buffer_pool.h"

struct c2_rpc_machine;

enum {
	C2_RPC_MACHINE_MAGIX	    = 0x5250434D414348, /* RPCMACH */
	/** Default Maximum RPC message size is taken as 128k */
	C2_RPC_DEF_MAX_RPC_MSG_SIZE = 1 << 17,
};

/** Enum to distinguish if the path is incoming or outgoing */
enum c2_rpc_item_path {
	C2_RPC_PATH_INCOMING = 0,
	C2_RPC_PATH_OUTGOING,
	C2_RPC_PATH_NR
};

/**
  Statistical data maintained for each item in the rpc_machine.
  It is upto the higher level layers to retrieve and process this data
 */
struct c2_rpc_stats {
	/** Number of items processed */
	uint64_t	rs_items_nr;
	/** Number of bytes processed */
	uint64_t	rs_bytes_nr;
	/** Cumulative latency. */
	c2_time_t	rs_cumu_lat;
	/** Min Latency */
	c2_time_t	rs_min_lat;
	/** Max Latency */
	c2_time_t	rs_max_lat;
	/** Number of rpc objects (used to calculate packing density) */
	uint64_t	rs_rpcs_nr;
};

struct c2_rpc_group {
	struct c2_rpc_machine	*rg_mach;
	/** List of rpc items linked through c2_rpc_item:ri_group_linkage. */
	struct c2_list		 rg_items;
	/** expected number of items in the group */
	uint64_t		 rg_expected;
        /** lock protecting fields of the struct */
        struct c2_mutex		 rg_guard;
	/** signalled when a reply is received or an error happens
	     (usually a timeout). */
	struct c2_chan		 rg_chan;
};

/**
   Struct c2_rpc_chan provides information about a target network endpoint.
   An rpc machine (struct c2_rpc_machine) contains list of c2_rpc_chan structures
   targeting different net endpoints.
   Rationale A physical node can have multiple endpoints associated with it.
   And multiple services can share endpoints for transport.
   The rule of thumb is to use one transfer machine per endpoint.
   So to make sure that services using same endpoint,
   use the same transfer machine, this structure has been introduced.
   Struct c2_rpc_conn is used for a particular service and now it
   points to a struct c2_rpc_chan to identify the transfer machine
   it is working with.
 */
struct c2_rpc_chan {
	/** Linkage to the list maintained by c2_rpc_machine.*/
	struct c2_list_link		  rc_linkage;
	/** Number of c2_rpc_conn structures using this transfer machine.*/
	struct c2_ref			  rc_ref;
	/** @deprecated Formation state machine associated with chan. */
	struct c2_rpc_frm_sm		  rc_frmsm;
	/** Formation state machine associated with chan. */
	struct c2_rpc_frm                 rc_frm;
	/** Destination end point to which rpcs will be sent. */
	struct c2_net_end_point		 *rc_destep;
	/** The rpc_machine, this chan structure is associated with.*/
	struct c2_rpc_machine		 *rc_rpc_machine;
};

/**
   RPC machine is an instance of RPC item (FOP/ADDB) processing context.
   Several such contexts might be existing simultaneously.
 */
struct c2_rpc_machine {
	struct c2_mutex                   rm_mutex;

	/** List of c2_rpc_chan structures. */
	struct c2_list			  rm_chans;
	/** Transfer machine associated with this endpoint.*/
	struct c2_net_transfer_mc	  rm_tm;
	/** Cob domain in which cobs related to session will be stored */
	struct c2_cob_domain		 *rm_dom;
	/** List of rpc connections
	    conn is in list if conn->c_state is not in {CONN_UNINITIALIZED,
	    CONN_FAILED, CONN_TERMINATED} */
	struct c2_list			  rm_incoming_conns;
	struct c2_list			  rm_outgoing_conns;
	/** @deprecated list of ready slots.
	    Replaced by c2_rpc_session::s_ready_slots
	 */
	struct c2_list			  rm_ready_slots;
	/** ADDB context for this rpc_machine */
	struct c2_addb_ctx		  rm_addb;
	/** Statistics for both incoming and outgoing paths */
	struct c2_rpc_stats		  rm_rpc_stats[C2_RPC_PATH_NR];
	/**
	    Request handler this rpc_machine belongs to.
	    @todo There needs to be  generic mechanism to register a
		request handler (or any other handler for future use)
		with the rpc machine and a ops vector specifying a
		method to be invoked for futher processing,
		e.g. c2_reqh_fop_handle(), in case of reqh.
	*/
	struct c2_reqh                   *rm_reqh;

        /**
	    Linkage into request handler's list of rpc machines.
	    c2_reqh::rh_rpc_machines
	 */
        struct c2_tlink                   rm_rh_linkage;

	/**
	    List of c2_rpc_service instances placed using svc_tlink.
	    tl_descr: c2_rpc_services_tl
	 */
	struct c2_tl                      rm_services;

	/**
	    A worker thread to run formation periodically in order to
	    send timedout items if any.
	 */
	struct c2_thread                  rm_frm_worker;

	/**
	   Flag asking rm_frm_worker thread to stop.
	 */
	bool                              rm_stopping;

	uint64_t                          rm_magix;

	/** Buffer pool from which TM receive buffers are provisioned. */
	struct c2_net_buffer_pool	 *rm_buffer_pool;

	/**
	 *  @see c2_net_transfer_mc:ntm_recv_queue_length
	 *  The default value is C2_NET_TM_RECV_QUEUE_DEF_LEN
	 */
	uint32_t			  rm_tm_recv_queue_min_length;

	/**
	 * @see c2_net_transfer_mc:ntm_recv_queue_min_recv_size
	 * The default value is c2_net_domain_get_max_buffer_size()
	 */
	uint32_t			  rm_min_recv_size;

	/**
	 * @see c2_net_transfer_mc:ntm_recv_queue_max_recv_msgs
	 * The default value is 1.
	 */
	uint32_t			  rm_max_recv_msgs;

	/**
	 * @see c2_net_transfer_mc:ntm_pool_colour
	 * The default value is C2_BUFFER_ANY_COLOUR
	 */
	uint32_t			  rm_tm_colour;

};

/** @todo Add these declarations to some internal header */
extern const struct c2_addb_ctx_type c2_rpc_addb_ctx_type;
extern const struct c2_addb_loc      c2_rpc_addb_loc;
extern       struct c2_addb_ctx      c2_rpc_addb_ctx;

extern const struct c2_addb_loc c2_rpc_machine_addb_loc;
C2_ADDB_EV_DECLARE(c2_rpc_machine_func_fail, C2_ADDB_FUNC_CALL);

/**
   Construct rpc core layer
   @return 0 success
   @return -ENOMEM failure
*/
int  c2_rpc_core_init(void);
/** Destruct rpc core layer */
void c2_rpc_core_fini(void);

/**
   Rpc machine is a running instance of rpc layer. A number of rpc machine
   structures can co-exist in rpc layer. With every rpc machine, a sessions
   module, a formation module, sending/receiving logic and statistics
   components are associated.

   @param machine Input rpc_machine object.
   @param dom cob domain that contains cobs representing slots
   @param net_dom Network domain, this rpc_machine is associated with.
   @param ep_addr Source end point address to associate with the transfer mc.
   @param receive_pool Buffer pool to be attached to TM for provisioning it.
   @param colour Unique colour of each transfer machine.
   		 Locality optimized buffer selection during provisioning is
		 enabled by specifying a colour to be assigned to the internal
		 network transfer machine; the invoker should assign each
		 transfer machine in this network domain a unique colour.
		 Specify the C2_BUFFER_ANY_COLOUR constant if locality
		 optimizations are not required.
   @param msg_size Maximum RPC message size.
   		   The C2_RPC_DEF_MAX_RPC_MSG_SIZE constant provides a
		   suitable default value.
   @param queue_len Minimum TM receive queue length.
   		    The C2_NET_TM_RECV_QUEUE_DEF_LEN constant provides a
		    suitable default value.
   @pre c2_rpc_core_init().
   @see c2_rpc_max_msg_size()
 */
int  c2_rpc_machine_init(struct c2_rpc_machine	   *machine,
			 struct c2_cob_domain	   *dom,
			 struct c2_net_domain	   *net_dom,
			 const char		   *ep_addr,
			 struct c2_reqh            *reqh,
			 struct c2_net_buffer_pool *receive_pool,
			 uint32_t		    colour,
			 c2_bcount_t		    msg_size,
			 uint32_t		    queue_len);

/**
   Destruct rpc_machine
   @param machine rpc_machine operation applied to.
 */
void c2_rpc_machine_fini(struct c2_rpc_machine *machine);

void c2_rpc_machine_lock(struct c2_rpc_machine *machine);
void c2_rpc_machine_unlock(struct c2_rpc_machine *machine);
bool c2_rpc_machine_is_locked(const struct c2_rpc_machine *machine);

/**
 * Calculates the total number of buffers needed in network domain for
 * receive buffer pool.
 * @param len total Length of the TM's in a network domain
 * @param tms_nr    Number of TM's in the network domain
 */
static inline uint32_t c2_rpc_bufs_nr(uint32_t len, uint32_t tms_nr)
{
	return len +
	       /* It is used so that more than one free buffer is present
		* for each TM when tms_nr > 8.
		*/
	       max32u(tms_nr / 4, 1) +
	       /* It is added so that frequent low_threshold callbacks of
		* buffer pool can be reduced.
		*/
	       C2_NET_BUFFER_POOL_THRESHOLD;
}

/** Returns the maximum segment size of receive pool of network domain. */
static inline c2_bcount_t c2_rpc_max_seg_size(struct c2_net_domain *ndom)
{
	C2_PRE(ndom != NULL);

	return min64u(c2_net_domain_get_max_buffer_segment_size(ndom),
		      C2_SEG_SIZE);
}

/** Returns the maximum number of segments of receive pool of network domain. */
static inline uint32_t c2_rpc_max_segs_nr(struct c2_net_domain *ndom)
{
	C2_PRE(ndom != NULL);

	return c2_net_domain_get_max_buffer_size(ndom) /
	       c2_rpc_max_seg_size(ndom);
}

/** Returns the maximum RPC message size in the network domain. */
static inline c2_bcount_t c2_rpc_max_msg_size(struct c2_net_domain *ndom,
					      c2_bcount_t rpc_size)
{
	c2_bcount_t mbs;

	C2_PRE(ndom != NULL);

	mbs = c2_net_domain_get_max_buffer_size(ndom);
	return rpc_size != 0 ? min64u(mbs, max64u(rpc_size, C2_SEG_SIZE)) : mbs;
}

/**
 * Returns the maximum number of messages that can be received in a buffer
 * of network domain for a specific maximum receive message size.
 */
static inline uint32_t c2_rpc_max_recv_msgs(struct c2_net_domain *ndom,
					    c2_bcount_t rpc_size)
{
	C2_PRE(ndom != NULL);

	return c2_net_domain_get_max_buffer_size(ndom) /
	       c2_rpc_max_msg_size(ndom, rpc_size);
}

/**
  Posts an unbound item to the rpc layer.

  The item will be sent through one of item->ri_session slots.

  The rpc layer will try to send the item out not later than
  item->ri_deadline and with priority of item->ri_priority.

  If this call returns without errors, the item's reply call-back is
  guaranteed to be called eventually.

  After successful call to c2_rpc_post(), user should not free the item.
  Rpc-layer will internally free the item when rpc-layer is sure that the item
  will not take part in recovery.

  Rpc layer does not provide any API, to "wait until reply is received".
  Upon receiving reply to item, item->ri_chan is signaled.
  If item->ri_ops->rio_replied() callback is set, then it will be called.
  Pointer to reply item can be retrieved from item->ri_reply.
  If any error occured, item->ri_error is set to non-zero value.

  Note: setting item->ri_ops and adding clink to item->ri_chan MUST be done
  before calling c2_rpc_post(), because reply to the item can arrive even
  before c2_rpc_post() returns.

  @pre item->ri_session != NULL
  @pre item->ri_priority is sane.
*/
int c2_rpc_post(struct c2_rpc_item *item);

/**
  Posts reply item on the same session on which the request item is received.

  After successful call to c2_rpc_reply_post(), user should not free the reply
  item. Rpc-layer will internally free the item when rpc-layer is sure that
  the corresponding request item will not take part in recovery.
 */
int c2_rpc_reply_post(struct c2_rpc_item *request,
		      struct c2_rpc_item *reply);

int c2_rpc_unsolicited_item_post(const struct c2_rpc_conn *conn,
				 struct c2_rpc_item *item);
/**
   Generate group used to treat rpc items as a group.

   @param machine rpc_machine operation applied to.
   @param group returned from the function

   @pre c2_rpc_core_init()
   @pre c2_rpc_machine_init()
   @return 0 success
   @return -ENOMEM failure
 */
int c2_rpc_group_open(struct c2_rpc_machine  *machine,
		      struct c2_rpc_group   **group);

/**
   Tell RPC layer core that group is closed
   and it can be processed by RPC core processing

   @param machine rpc_machine operation applied to.
   @param group return value from the function

   @pre c2_rpc_core_init()
   @pre c2_rpc_machine_init()
   @return 0  success
   @return <0 failure
 */
int c2_rpc_group_close(struct c2_rpc_group *group);

/**
   Submit rpc item group into processing engine.
   or change parameters (priority, caching policy
   and group membership) of an already submitted item.

   @param group used treat rpc items as a group.
   @param item rpc item being sent
   @param prio priority of processing of this item
   @param deadline maximum processing time of this item

   @pre c2_rpc_core_init()
   @pre c2_rpc_machine_init()
   @return 0  success
   @return <0 failure
 */
int c2_rpc_group_submit(struct c2_rpc_group		*group,
			struct c2_rpc_item		*item,
			enum c2_rpc_item_priority	 prio,
			const c2_time_t			*deadline);

/**
   Wait for the reply on item being sent.

   @param The clink on which caller is waiting for item reply.
   @param timeout time to wait for item being sent
   @note c2_rpc_core_init() and c2_rpc_machine_init() have been called before
   invoking this function
   @return 0 success
   @return ETIMEDOUT The wait timed out wihout being sent
 */
int c2_rpc_reply_timedwait(struct c2_clink *clink, const c2_time_t timeout);

/**
   Wait for the reply on group of items being sent.

   @param group used treat rpc items as a group.
   @param timeout time to wait for item being sent
   @pre c2_rpc_core_init()
   @pre c2_rpc_machine_init()
   @return 0 success
   @return ETIMEDOUT The wait timed out wihout being sent
 */
int c2_rpc_group_timedwait(struct c2_rpc_group *group, const c2_time_t *timeout);

/**
   @name stat_ifs STATISTICS IFs
   Iterfaces, returning different properties of rpc_machine.
   @{
 */

/**
   Returns average time spent in the cache for one RPC-item
   @note c2_rpc_core_init() and c2_rpc_machine_init() have been called before
   @param machine rpc_machine operation applied to.
   @param path Incoming or outgoing path of rpc item.
 */
c2_time_t c2_rpc_avg_item_time(struct c2_rpc_machine *machine,
			       const enum c2_rpc_item_path path);

/**
   Returns transmission speed in bytes per second.
   @note c2_rpc_core_init() and c2_rpc_machine_init() have been called before
   @param machine rpc_machine operation applied to.
   @param path Incoming or outgoing path of rpc item.
 */
size_t c2_rpc_bytes_per_sec(struct c2_rpc_machine *machine,
			    const enum c2_rpc_item_path path);

/** @} end name stat_ifs */

/**
   Create a buffer pool per net domain which to be shared by TM's in it.
   @pre ndom != NULL && app_pool != NULL
   @pre bufs_nr != 0
 */
int c2_rpc_net_buffer_pool_setup(struct c2_net_domain *ndom,
				 struct c2_net_buffer_pool *app_pool,
				 uint32_t bufs_nr, uint32_t tm_nr);

void c2_rpc_net_buffer_pool_cleanup(struct c2_net_buffer_pool *app_pool);

/** @} end group rpc_layer_core */

#endif /* __COLIBRI_RPC_RPCCORE_H__  */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
