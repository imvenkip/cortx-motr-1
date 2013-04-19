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

#ifndef __MERO_RPC_MACHINE_H__
#define __MERO_RPC_MACHINE_H__

#include "lib/bob.h"
#include "lib/tlist.h"
#include "lib/thread.h"
#include "sm/sm.h"     /* m0_sm_group */
#include "addb/addb.h" /* m0_addb_ctx */
#include "net/net.h"   /* m0_net_transfer_mc, m0_net_domain */

/**
   @addtogroup rpc

   @{
 */

/* Imports */
struct m0_cob_domain;
struct m0_rpc_conn;
struct m0_rpc_session;
struct m0_reqh;

enum {
	/** Default Maximum RPC message size is taken as 128k */
	M0_RPC_DEF_MAX_RPC_MSG_SIZE = 1 << 17,
};

/** Collection of statistics per rpc machine */
struct m0_rpc_stats {
	/* Items */
	uint64_t rs_nr_rcvd_items;
	uint64_t rs_nr_sent_items;
	uint64_t rs_nr_failed_items;
	uint64_t rs_nr_dropped_items;
	uint64_t rs_nr_timedout_items;
	uint64_t rs_nr_sent_items_uniq;
	uint64_t rs_nr_resent_items;

	/* Packets */
	uint64_t rs_nr_rcvd_packets;
	uint64_t rs_nr_sent_packets;
	uint64_t rs_nr_failed_packets;

	/* Bytes */
	uint64_t rs_nr_sent_bytes;
	uint64_t rs_nr_rcvd_bytes;
};

/**
   RPC machine is an instance of RPC item (FOP/ADDB) processing context.
   Several such contexts might be existing simultaneously.
 */
struct m0_rpc_machine {
	struct m0_sm_group                rm_sm_grp;
	/** List of m0_rpc_chan objects, linked using rc_linkage.
	    List descriptor: rpc_chan
	 */
	struct m0_tl			  rm_chans;
	/** Transfer machine associated with this endpoint.*/
	struct m0_net_transfer_mc	  rm_tm;
	/** Cob domain in which cobs related to session will be stored */
	struct m0_cob_domain		 *rm_dom;
	/** List of m0_rpc_conn objects, linked using c_link.
	    List descriptor: rpc_conn
	    conn is in list if connection is not in {CONN_UNINITIALISED,
	    CONN_FAILED, CONN_TERMINATED} states.
	 */
	struct m0_tl			  rm_incoming_conns;
	struct m0_tl			  rm_outgoing_conns;
	/** ADDB context for this rpc_machine */
	struct m0_addb_ctx		  rm_addb_ctx;
	struct m0_rpc_stats		  rm_stats;
	/* RPC Counters */
	struct m0_addb_counter		  rm_cntr_sent_item_sizes;
	struct m0_addb_counter		  rm_cntr_rcvd_item_sizes;
	/**
	    Request handler this rpc_machine belongs to.
	    @todo There needs to be  generic mechanism to register a
		request handler (or any other handler for future use)
		with the rpc machine and a ops vector specifying a
		method to be invoked for futher processing,
		e.g. m0_reqh_fop_handle(), in case of reqh.
	*/
	struct m0_reqh                   *rm_reqh;

        /**
	    Linkage into request handler's list of rpc machines.
	    m0_reqh::rh_rpc_machines
	 */
        struct m0_tlink                   rm_rh_linkage;

	/**
	    List of m0_rpc_service instances placed using svc_tlink.
	    tl_descr: m0_rpc_services_tl
	 */
	struct m0_tl                      rm_services;

	/**
	   List of m0_rpc_machine_watch instances.
	   tlink: m0_rpc_machine_watch::mw_linkage
	   tlist descr: rmach_watch
	 */
	struct m0_tl                      rm_watch;

	/**
	   Executes ASTs in rm_sm_grp.
	 */
	struct m0_thread                  rm_worker;

	/**
	   Flag asking rm_worker thread to stop.
	 */
	bool                              rm_stopping;

	uint64_t                          rm_magix;

	/**
	 * @see m0_net_transfer_mc:ntm_recv_queue_min_recv_size
	 * The default value is m0_net_domain_get_max_buffer_size()
	 */
	uint32_t			  rm_min_recv_size;
};

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
		 Specify the M0_BUFFER_ANY_COLOUR constant if locality
		 optimizations are not required.
   @param msg_size Maximum RPC message size.
		   The M0_RPC_DEF_MAX_RPC_MSG_SIZE constant provides a
		   suitable default value.
   @param queue_len Minimum TM receive queue length.
		    The M0_NET_TM_RECV_QUEUE_DEF_LEN constant provides a
		    suitable default value.
   @see m0_rpc_max_msg_size()
 */
M0_INTERNAL int m0_rpc_machine_init(struct m0_rpc_machine *machine,
				    struct m0_cob_domain *dom,
				    struct m0_net_domain *net_dom,
				    const char *ep_addr,
				    struct m0_reqh *reqh,
				    struct m0_net_buffer_pool *receive_pool,
				    uint32_t colour,
				    m0_bcount_t msg_size, uint32_t queue_len);

void m0_rpc_machine_fini(struct m0_rpc_machine *machine);

void m0_rpc_machine_get_stats(struct m0_rpc_machine *machine,
			      struct m0_rpc_stats *stats, bool reset);

/**
   Subroutine to post ADDB statistics on the RPC machine.
   Nothing should be posted if there was no activity since the last invocation
   of this subroutine.
 */
M0_INTERNAL void m0_rpc_machine_stats_post_addb(struct m0_rpc_machine *machine);

M0_INTERNAL void m0_rpc_machine_drain_item_sources(struct m0_rpc_machine
						   *machine);

M0_BOB_DECLARE(extern, m0_rpc_machine);

/**
 * RPC machine watch is a suite of call-backs invoked by the rpc code when new
 * connection or session is created within the rpc machine the watch is
 * attached to.
 *
 * Call-backs are invoked under the rpc machine lock.
 */
struct m0_rpc_machine_watch {
	/**
	 * Datum that can be used to associate additional state to the watcher.
	 */
	void                  *mw_datum;
	/**
	 * RPC machine this watcher is attached to. This field must be set
	 * before calling m0_rpc_machine_watch_attach().
	 */
	struct m0_rpc_machine *mw_mach;
	/**
	 * Linkage into m0_rpc_machine::rm_watch list of all watches for this
	 * machine.
	 */
	struct m0_tlink       mw_linkage;
	/**
	 * This is invoked when a connection is added to one of the rpc machine
	 * connection lists. The connection is in M0_RPC_CONN_INITIALISED state.
	 */
	void (*mw_conn_added)(struct m0_rpc_machine_watch *w,
			      struct m0_rpc_conn *conn);
	/**
	 * This is called when a session is added to the connection list of
	 * sessions (m0_rpc_conn::c_sessions). The session is in
	 * M0_RPC_SESSION_INITIALISED state.
	 */
	void (*mw_session_added)(struct m0_rpc_machine_watch *w,
				 struct m0_rpc_session *session);
	/**
	 * This call-back is called when the rpc machine is terminated, while
	 * still having attached watches. The watch is removed from the watch
	 * list before this call-back is invoked. There is neither need nor harm
	 * in calling m0_rpc_machine_watch_detach() once this call-back was
	 * invoked.
	 */
	void (*mw_mach_terminated)(struct m0_rpc_machine_watch *w);

	/** @see M0_RPC_MACHINE_WATCH_MAGIC */
	uint64_t mw_magic;
};

/**
 * Attaches a watch to its rpc machine.
 *
 * @pre watch->mw_mach != NULL
 * @pre m0_rpc_machine_is_not_locked(watch->mw_mach)
 */
void m0_rpc_machine_watch_attach(struct m0_rpc_machine_watch *watch);

/**
 * Detaches a watch from its rpc machine, if still attached.
 *
 * @pre watch->mw_mach != NULL
 * @pre m0_rpc_machine_is_not_locked(watch->mw_mach)
 */
void m0_rpc_machine_watch_detach(struct m0_rpc_machine_watch *watch);

/** @} end of rpc group */
#endif /* __MERO_RPC_MACHINE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
