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

#include "lib/bob.h"
#include "lib/tlist.h"
#include "lib/thread.h"
#include "sm/sm.h"     /* c2_sm_group */
#include "addb/addb.h" /* c2_addb_ctx */
#include "net/net.h"   /* c2_net_transfer_mc, c2_net_domain */

/**
   @addtogroup rpc

   @{
 */

/* Imports */
struct c2_cob_domain;
struct c2_reqh;

enum {
	/** Default Maximum RPC message size is taken as 128k */
	C2_RPC_DEF_MAX_RPC_MSG_SIZE = 1 << 17,
};

/** Collection of statistics per rpc machine */
struct c2_rpc_stats {
	/* Items */
	uint64_t rs_nr_rcvd_items;
	uint64_t rs_nr_sent_items;
	uint64_t rs_nr_failed_items;
	uint64_t rs_nr_dropped_items;
	uint64_t rs_nr_timedout_items;

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
struct c2_rpc_machine {
	struct c2_sm_group                rm_sm_grp;
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
	   Executes ASTs in rm_sm_grp.
	 */
	struct c2_thread                  rm_worker;

	/**
	   Flag asking rm_worker thread to stop.
	 */
	bool                              rm_stopping;

	uint64_t                          rm_magix;

	/**
	 * @see c2_net_transfer_mc:ntm_recv_queue_min_recv_size
	 * The default value is c2_net_domain_get_max_buffer_size()
	 */
	uint32_t			  rm_min_recv_size;
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
   @see c2_rpc_max_msg_size()
 */
C2_INTERNAL int c2_rpc_machine_init(struct c2_rpc_machine *machine,
				    struct c2_cob_domain *dom,
				    struct c2_net_domain *net_dom,
				    const char *ep_addr,
				    struct c2_reqh *reqh,
				    struct c2_net_buffer_pool *receive_pool,
				    uint32_t colour,
				    c2_bcount_t msg_size, uint32_t queue_len);

C2_INTERNAL void c2_rpc_machine_fini(struct c2_rpc_machine *machine);

C2_INTERNAL void c2_rpc_machine_get_stats(struct c2_rpc_machine *machine,
					  struct c2_rpc_stats *stats,
					  bool reset);

C2_BOB_DECLARE(C2_EXTERN, c2_rpc_machine);

/** @} end of rpc group */
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
