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
 * Original creation date: 06/27/2012
 */

#pragma once

#ifndef __COLIBRI_RPC_MACHINE_H__
#define __COLIBRI_RPC_MACHINE_H__

#include "lib/types.h"
#include "lib/tlist.h"
#include "lib/mutex.h"
#include "lib/time.h"
#include "lib/chan.h"
#include "lib/refs.h"
#include "lib/thread.h"
#include "lib/arith.h"
#include "lib/bob.h"

#include "addb/addb.h"
#include "rpc/formation2.h"  /* c2_rpc_frm         */
#include "net/net.h"         /* c2_net_transfer_mc, c2_net_domain */
#include "sm/sm.h"           /* c2_sm_group */

/**
   @addtogroup rpc_layer_core

   @{
 */

/* Imports */
struct c2_cob_domain;
struct c2_reqh;
struct c2_rpc_conn;

enum {
	/** Default Maximum RPC message size is taken as 128k */
	C2_RPC_DEF_MAX_RPC_MSG_SIZE = 1 << 17,
};

/** Enum to distinguish if the path is incoming or outgoing */
enum c2_rpc_item_path {
	C2_RPC_PATH_INCOMING = 0,
	C2_RPC_PATH_OUTGOING,
	C2_RPC_PATH_NR
};

/** Collection of statistics per rpc machine */
struct c2_rpc_stats {
	uint64_t	rs_nr_rcvd_items;
	uint64_t	rs_nr_sent_items;
	uint64_t	rs_nr_rcvd_packets;
	uint64_t	rs_nr_sent_packets;
	uint64_t	rs_nr_failed_items;
	uint64_t	rs_nr_failed_packets;
	uint64_t	rs_nr_timedout_items;
	uint64_t	rs_nr_dropped_items;
	uint64_t	rs_nr_sent_bytes;
	uint64_t	rs_nr_rcvd_bytes;
};

/**
   RPC machine is an instance of RPC item (FOP/ADDB) processing context.
   Several such contexts might be existing simultaneously.
 */
struct c2_rpc_machine {
	struct c2_sm_group		  rm_sm_grp;

	/** List of c2_rpc_chan objects, linked using rc_linkage.
	    List descriptor: rpc_chan
	 */
	struct c2_tl			  rm_chans;
	/** Transfer machine associated with this endpoint.*/
	struct c2_net_transfer_mc	  rm_tm;
	/** Cob domain in which cobs related to session will be stored */
	struct c2_cob_domain		 *rm_dom;
	/** List of c2_rpc_conn objects, linked using c_link.
	    List descriptor: rpc_conn
	    conn is in list if connection is not in {CONN_UNINITIALISED,
	    CONN_FAILED, CONN_TERMINATED} states.
	 */
	struct c2_tl			  rm_incoming_conns;
	struct c2_tl			  rm_outgoing_conns;
	/** ADDB context for this rpc_machine */
	struct c2_addb_ctx		  rm_addb;
	struct c2_rpc_stats		  rm_stats;
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

	/**
	 * @see c2_net_transfer_mc:ntm_recv_queue_min_recv_size
	 * The default value is c2_net_domain_get_max_buffer_size()
	 */
	uint32_t			  rm_min_recv_size;
};

/**
   Struct c2_rpc_chan provides information about a target network endpoint.
   An rpc machine (struct c2_rpc_machine) contains list of c2_rpc_chan
   structures targeting different net endpoints.
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
	/** Link in c2_rpc_machine::rm_chans list.
	    List descriptor: rpc_chan
	 */
	struct c2_tlink			  rc_linkage;
	/** Number of c2_rpc_conn structures using this transfer machine.*/
	struct c2_ref			  rc_ref;
	/** Formation state machine associated with chan. */
	struct c2_rpc_frm                 rc_frm;
	/** Destination end point to which rpcs will be sent. */
	struct c2_net_end_point		 *rc_destep;
	/** The rpc_machine, this chan structure is associated with.*/
	struct c2_rpc_machine		 *rc_rpc_machine;
	/** C2_RPC_CHAN_MAGIC */
	uint64_t			  rc_magic;
};

extern const struct c2_addb_loc c2_rpc_machine_addb_loc;
C2_ADDB_EV_DECLARE(c2_rpc_machine_func_fail, C2_ADDB_FUNC_CALL);

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
void c2_rpc_machine_get_stats(struct c2_rpc_machine *machine,
			      struct c2_rpc_stats *stats, bool reset);

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

static struct c2_rpc_chan *frm_rchan(const struct c2_rpc_frm *frm)
{
	return container_of(frm, struct c2_rpc_chan, rc_frm);
}

static struct c2_rpc_machine *frm_rmachine(const struct c2_rpc_frm *frm)
{
	return frm_rchan(frm)->rc_rpc_machine;
}

static bool frm_rmachine_is_locked(const struct c2_rpc_frm *frm)
						__attribute__((unused));

static bool frm_rmachine_is_locked(const struct c2_rpc_frm *frm)
{
	return c2_rpc_machine_is_locked(frm_rmachine(frm));
}

C2_BOB_DECLARE(extern, c2_rpc_machine);

C2_TL_DESCR_DECLARE(rpc_conn, extern);
C2_TL_DECLARE(rpc_conn, extern, struct c2_rpc_conn);

/** @} end of rpc-layer-core group */
#endif /* __COLIBRI_RPC_MACHINE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
