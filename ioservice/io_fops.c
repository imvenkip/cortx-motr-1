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
 * Original author: Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 03/21/2011
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ioservice/io_fops.h"
#include "ioservice/io_foms.h"
#include "ioservice/cob_foms.h"

#ifdef __KERNEL__
#include "ioservice/io_fops_k.h"
#else
#include "ioservice/io_fops_u.h"
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/vec.h"	/* c2_0vec */
#include "lib/memory.h"
#include "lib/tlist.h"
#include "addb/addb.h"
#include "colibri/magic.h"
#include "fop/fop_item_type.h"
#include "rpc/item.h"
#include "rpc/rpc_opcodes.h"
#include "rpc/rpc2.h"
#include "rpc/rpc_onwire.h"
#include "ioservice/io_fops.h"
#include "fop/fom_generic.h"
#include "ioservice/io_fops_ff.h"

/*
 * Cob delete and Cob create fom types.
 */
extern struct c2_fom_type cob_delete_fomt;
extern struct c2_fom_type cob_create_fomt;

/* tlists and tlist APIs referred from rpc layer. */
C2_TL_DESCR_DECLARE(rpcbulk, extern);
C2_TL_DESCR_DECLARE(rpcitem, extern);
C2_TL_DECLARE(rpcbulk, extern, struct c2_rpc_bulk_buf);
C2_TL_DECLARE(rpcitem, extern, struct c2_rpc_item);

static struct c2_fop_file_fid *io_fop_fid_get(struct c2_fop *fop);
static void   io_item_replied(struct c2_rpc_item *item);
static void   item_io_coalesce(struct c2_rpc_item *head, struct c2_list *list,
			       uint64_t size);
int c2_io_fom_cob_rw_init(struct c2_fop *fop, struct c2_fom **m);
int cob_fom_init(struct c2_fop *fop, struct c2_fom **out);

static void io_item_free(struct c2_rpc_item *item);
static void io_fop_replied(struct c2_fop *fop, struct c2_fop *bkpfop);
static void io_fop_desc_get(struct c2_fop *fop, struct c2_net_buf_desc **desc);
static int  io_fop_coalesce(struct c2_fop *res_fop, uint64_t size);
static void cob_rpcitem_free(struct c2_rpc_item *item);

/* ADDB context for ioservice. */
static struct c2_addb_ctx bulkclient_addb;

/* ADDB instrumentation for bulk client. */
static const struct c2_addb_loc bulkclient_addb_loc = {
	.al_name = "bulkclient",
};

static const struct c2_addb_ctx_type bulkclient_addb_ctx_type = {
	.act_name = "bulkclient",
};

C2_ADDB_EV_DEFINE(bulkclient_func_fail, "bulkclient func failed.",
		  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

struct c2_fop_type c2_fop_cob_readv_fopt;
struct c2_fop_type c2_fop_cob_writev_fopt;
struct c2_fop_type c2_fop_cob_readv_rep_fopt;
struct c2_fop_type c2_fop_cob_writev_rep_fopt;
struct c2_fop_type c2_fop_cob_create_fopt;
struct c2_fop_type c2_fop_cob_delete_fopt;
struct c2_fop_type c2_fop_cob_op_reply_fopt;

C2_EXPORTED(c2_fop_cob_writev_fopt);
C2_EXPORTED(c2_fop_cob_readv_fopt);

static struct c2_fop_type *ioservice_fops[] = {
	&c2_fop_cob_readv_fopt,
	&c2_fop_cob_writev_fopt,
	&c2_fop_cob_readv_rep_fopt,
	&c2_fop_cob_writev_rep_fopt,
	&c2_fop_cob_create_fopt,
	&c2_fop_cob_delete_fopt,
	&c2_fop_cob_op_reply_fopt,
};

/* Used for IO REQUEST items only. */
const struct c2_rpc_item_ops io_req_rpc_item_ops = {
	.rio_sent	= NULL,
	.rio_replied	= io_item_replied,
	.rio_free	= io_item_free,
};

static const struct c2_rpc_item_type_ops io_item_type_ops = {
        .rito_payload_size   = c2_fop_item_type_default_onwire_size,
        .rito_io_coalesce    = item_io_coalesce,
        .rito_encode	     = c2_fop_item_type_default_encode,
        .rito_decode	     = c2_fop_item_type_default_decode,
};

const struct c2_fop_type_ops io_fop_rwv_ops = {
	.fto_fop_replied = io_fop_replied,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_io_desc_get = io_fop_desc_get,
};

/* Used for cob_create and cob_delete fops. */
const struct c2_rpc_item_ops cob_req_rpc_item_ops = {
	.rio_sent        = NULL,
	.rio_free        = cob_rpcitem_free,
};

static const struct c2_rpc_item_type_ops cob_rpc_type_ops = {
	.rito_payload_size   = c2_fop_item_type_default_onwire_size,
	.rito_io_coalesce    = NULL,
	.rito_encode         = c2_fop_item_type_default_encode,
	.rito_decode	     = c2_fop_item_type_default_decode,
};

void c2_ioservice_fop_fini(void)
{
	c2_fop_type_fini(&c2_fop_cob_op_reply_fopt);
	c2_fop_type_fini(&c2_fop_cob_delete_fopt);
	c2_fop_type_fini(&c2_fop_cob_create_fopt);
	c2_fop_type_fini(&c2_fop_cob_writev_rep_fopt);
	c2_fop_type_fini(&c2_fop_cob_readv_rep_fopt);
	c2_fop_type_fini(&c2_fop_cob_writev_fopt);
	c2_fop_type_fini(&c2_fop_cob_readv_fopt);
	c2_xc_io_fops_fini();
	c2_addb_ctx_fini(&bulkclient_addb);
}
C2_EXPORTED(c2_ioservice_fop_fini);

extern struct c2_reqh_service_type c2_ios_type;
extern const struct c2_fom_type_ops cob_fom_type_ops;
extern const struct c2_fom_type_ops io_fom_type_ops;

extern const struct c2_sm_conf io_conf;
extern struct c2_sm_state_descr io_phases[];

int c2_ioservice_fop_init(void)
{
	c2_addb_ctx_init(&bulkclient_addb, &bulkclient_addb_ctx_type,
			 &c2_addb_global_ctx);
	/*
	 * Provided by ff2c compiler after parsing io_fops_xc.ff
	 */
	c2_xc_io_fops_init();

#ifndef __KERNEL__
	c2_sm_conf_extend(c2_generic_conf.scf_state, io_phases,
			  c2_generic_conf.scf_nr_states);
#endif
	return  C2_FOP_TYPE_INIT(&c2_fop_cob_readv_fopt,
				 .name      = "Read request",
				 .opcode    =  C2_IOSERVICE_READV_OPCODE,
				 .xt        = c2_fop_cob_readv_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST,
				 .fop_ops   = &io_fop_rwv_ops,
#ifndef __KERNEL__
				 .fom_ops   = &io_fom_type_ops,
				 .sm        = &io_conf,
				 .svc_type  = &c2_ios_type,
#endif
				 .rpc_ops   = &io_item_type_ops) ?:
		C2_FOP_TYPE_INIT(&c2_fop_cob_writev_fopt,
				 .name      = "Write request",
				 .opcode    =  C2_IOSERVICE_WRITEV_OPCODE,
				 .xt        = c2_fop_cob_writev_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
					      C2_RPC_ITEM_TYPE_MUTABO,
				 .fop_ops   = &io_fop_rwv_ops,
#ifndef __KERNEL__
				 .fom_ops   = &io_fom_type_ops,
				 .sm        = &io_conf,
				 .svc_type  = &c2_ios_type,
#endif
				 .rpc_ops   = &io_item_type_ops) ?:
		C2_FOP_TYPE_INIT(&c2_fop_cob_readv_rep_fopt,
				 .name      = "Read reply",
				 .opcode    =  C2_IOSERVICE_READV_REP_OPCODE,
				 .xt        = c2_fop_cob_readv_rep_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY) ?:
		C2_FOP_TYPE_INIT(&c2_fop_cob_writev_rep_fopt,
				 .name      = "Write request",
				 .opcode    =  C2_IOSERVICE_WRITEV_REP_OPCODE,
				 .xt        = c2_fop_cob_writev_rep_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY) ?:
		C2_FOP_TYPE_INIT(&c2_fop_cob_create_fopt,
				 .name      = "Cob create request",
				 .opcode    =  C2_IOSERVICE_COB_CREATE_OPCODE,
				 .xt        = c2_fop_cob_create_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST,
#ifndef __KERNEL__
				 .fom_ops   = &cob_fom_type_ops,
				 .svc_type  = &c2_ios_type,
#endif
				 .sm        = &c2_generic_conf,
				 .rpc_ops   = &cob_rpc_type_ops) ?:
		C2_FOP_TYPE_INIT(&c2_fop_cob_delete_fopt,
				 .name      = "Cob delete request",
				 .opcode    =  C2_IOSERVICE_COB_DELETE_OPCODE,
				 .xt        = c2_fop_cob_delete_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST,
#ifndef __KERNEL__
				 .fom_ops   = &cob_fom_type_ops,
				 .svc_type  = &c2_ios_type,
#endif
				 .sm        = &c2_generic_conf,
				 .rpc_ops   = &cob_rpc_type_ops) ?:
		C2_FOP_TYPE_INIT(&c2_fop_cob_op_reply_fopt,
				 .name      = "Cob create or delete reply",
				 .opcode    =  C2_IOSERVICE_COB_OP_REPLY_OPCODE,
				 .xt        = c2_fop_cob_op_reply_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY,
				 .rpc_ops   = &cob_rpc_type_ops);
}
C2_EXPORTED(c2_ioservice_fop_init);

/**
   @page io_bulk_client IO bulk transfer Detailed Level Design.

   - @ref bulkclient-ovw
   - @ref bulkclient-def
   - @ref bulkclient-req
   - @ref bulkclient-depends
   - @ref bulkclient-highlights
   - @subpage bulkclient-fspec "IO bulk client Func Spec"
   - @ref bulkclient-lspec
      - @ref bulkclient-lspec-comps
      - @ref bulkclient-lspec-sc1
      - @ref bulkclient-lspec-state
      - @ref bulkclient-lspec-thread
      - @ref bulkclient-lspec-numa
   - @ref bulkclient-conformance
   - @ref bulkclient-ut
   - @ref bulkclient-st
   - @ref bulkclient-O
   - @ref bulkclient-ref

   <hr>
   @section bulkclient-ovw Overview

   This document describes the working of client side of io bulk transfer.
   This functionality is used only for io path.
   IO bulk client constitues the client side of bulk IO carried out between
   Colibri client file system and data server (ioservice aka bulk io server).
   Colibri network layer incorporates a bulk transport mechanism to
   transfer user buffers in zero-copy fashion.
   The generic io fop contains a network buffer descriptor which refers to a
   network buffer.
   The bulk client creates IO fops and attaches the kernel pages or a vector
   in user mode to net buffer associated with io fop and submits it
   to rpc layer.
   The rpc layer populates the net buffer descriptor from io fop and sends
   the fop over wire.
   The receiver starts the zero-copy of buffers using the net buffer
   descriptor from io fop.

   <hr>
   @section bulkclient-def Definitions

   - c2t1fs - Colibri client file system. It works as a kernel module.
   - Bulk transport - Event based, asynchronous message passing functionality
   of Colibri network layer.
   - io fop - A generic io fop that is used for read and write.
   - rpc bulk - An interface to abstract the usage of network buffers by
   client and server programs.
   - ioservice - A service providing io routines in Colibri. It runs only
   on server side.

   <hr>
   @section bulkclient-req Requirements

   - R.bulkclient.rpcbulk The bulk client should use rpc bulk abstraction
   while enqueueing buffers for bulk transfer.
   - R.bulkclient.fopcreation The bulk client should create io fops as needed
   if pages overrun the existing rpc bulk structure.
   - R.bulkclient.netbufdesc The generic io fop should contain a network
   buffer descriptor which points to an in-memory network buffer.
   - R.bulkclient.iocoalescing The IO coalescing code should conform to
   new format of io fop. This is actually a side-effect and not a core
   part of functionality. Since the format of IO fop changes, the IO
   coalescing code which depends on it, needs to be restructured.

   <hr>
   @section bulkclient-depends Dependencies

   - r.misc.net_rpc_convert Bulk Client needs Colibri client file system to be
   using new network layer apis which include c2_net_domain and c2_net_buffer.
   - r.fop.referring_another_fop With introduction of a net buffer
   descriptor in io fop, a mechanism needs to be introduced so that fop
   definitions from one component can refer to definitions from another
   component. c2_net_buf_desc is a fop used to represent on-wire
   representation of a c2_net_buffer. @see c2_net_buf_desc.

   <hr>
   @section bulkclient-highlights Design Highlights

   IO bulk client uses a generic in-memory structure representing an io fop
   and its associated network buffer.
   This in-memory io fop contains another abstract structure to represent
   the network buffer associated with the fop.
   The bulk client creates c2_io_fop structures as necessary and attaches
   kernel pages or user space vector to associated c2_rpc_bulk structure
   and submits the fop to rpc layer.
   Rpc layer populates the network buffer descriptor embedded in the io fop
   and sends the fop over wire. The associated network buffer is added to
   appropriate buffer queue of transfer machine owned by rpc layer.
   Once, the receiver side receives the io fop, it acquires a local network
   buffer and calls a c2_rpc_bulk apis to start the zero-copy.
   So, io fop typically carries the net buf descriptor and bulk server asks
   the transfer machine belonging to rpc layer to start zero copy of
   data buffers.

   <hr>
   @section bulkclient-lspec Logical Specification

   - @ref bulkclient-lspec-comps
   - @ref bulkclient-lspec-sc1
      - @ref bulkclient-lspec-ds1
      - @ref bulkclient-lspec-sub1
      - @ref bulkclientDFSInternal
   - @ref bulkclient-lspec-state
   - @ref bulkclient-lspec-thread
   - @ref bulkclient-lspec-numa


   @subsection bulkclient-lspec-comps Component Overview

   The following @@dot diagram shows the interaction of bulk client
   program with rpc layer and net layer.
   @dot
   digraph {
     node [style=box];
     label = "IO bulk client interaction with rpc and net layer";
     io_bulk_client [label = "IO bulk client"];
     Rpc_bulk [label = "RPC bulk abstraction"];
     IO_fop [label = "IO fop"];
     nwlayer [label = "Network layer"];
     zerovec [label = "Zero vector"];
     io_bulk_client -> IO_fop;
     IO_fop -> Rpc_bulk;
     IO_fop -> zerovec;
     Rpc_bulk -> zerovec;
     Rpc_bulk -> nwlayer;
   }
   @enddot

   @subsection bulkclient-lspec-sc1 Subcomponent design

   Ioservice subsystem primarily comprises of 2 sub-components
   - IO client (comprises of IO coalescing code)
   - IO server (server part of io routines)
   The IO client subsystem under which IO requests belonging to same fid
   and intent (read/write) are clubbed together in one fop and this resultant
   fop is sent instead of member io fops.

   @subsubsection bulkclient-lspec-ds1 Subcomponent Data Structures

   The IO coalescing subsystem from ioservice primarily works on IO segments.
   IO segment is in-memory structure that represents a contiguous chunk of
   IO data along with extent information.
   An internal data structure ioseg represents the IO segment.
   - ioseg An in-memory structure used to represent a segment of IO data.

   @subsubsection bulkclient-lspec-sub1 Subcomponent Subroutines

   - ioseg_get() - Retrieves an ioseg given its index in zero vector.

   @subsection bulkclient-lspec-state State Specification

   @dot
   digraph bulk_io_client_states {
	size = "5,6"
	label = "States encountered during io from bulk client"
	node [shape = record, fontname=Helvetica, fontsize=12]
	S0 [label = "", shape="plaintext", layer=""]
	S1 [label = "IO fop initialized"]
	S2 [label = "Rpc bulk structure initialized"]
	S3 [label = "Pages added to rpc bulk structure"]
	S4 [label = "Net buf desc stored in io fop wire format."]
	S5 [label = "Rpc item posted to rpc layer"]
	S6 [label = "Client waiting for reply"]
	S7 [label = "Reply received"]
	S8 [label = "Terminate"]
	S0 -> S1 [label = "Allocate"]
	S1 -> S2 [label = "c2_rpc_bulk_init()"]
	S1 -> S8 [label = "Failed"]
	S2 -> S8 [label = "Failed"]
	S2 -> S3 [label = "c2_rpc_bulk_buf_page_add()"]
	S3 -> S8 [label = "Failed"]
	S3 -> S4 [label = "c2_rpc_bulk_store()"]
	S4 -> S5 [label = "c2_rpc_post()"]
	S5 -> S6 [label = "c2_chan_wait(item->ri_chan)"]
	S6 -> S7 [label = "c2_chan_signal(item->ri_chan)"]
	S7 -> S8 [label = "c2_rpc_bulk_fini(rpc_bulk)"]
   }
   @enddot

   @subsection bulkclient-lspec-thread Threading and Concurrency Model

   No need of explicit locking for structures like c2_io_fop and ioseg
   since they are taken care by locking at upper layers like locking at
   the c2t1fs part for dispatching IO requests.

   @subsection bulkclient-lspec-numa NUMA optimizations

   The performance need not be optimized by associating the incoming thread
   to a particular processor. However, keeping in sync with the design of
   request handler which tries to protect the locality of threads executing
   in a particular context by establishing affinity to some designated
   processor, this can be achieved. But this is still at a level higher than
   the io fop processing.

   <hr>
   @section bulkclient-conformance Conformance

   - I.bulkclient.rpcbulk The bulk client uses rpc bulk APIs to enqueue
   kernel pages to the network buffer.
   - I.bulkclient.fopcreation bulk client creates new io fops until all
   kernel pages are enqueued.
   - I.bulkclient.netbufdesc The on-wire definition of io_fop contains a
   net buffer descriptor. @see c2_net_buf_desc
   - I.bulkclient.iocoalescing Since all IO coalescing code is built around
   the definition of IO fop, it will conform to new format of io fop.

   <hr>
   @section bulkclient-ut Unit Tests

   All external interfaces based on c2_io_fop and c2_rpc_bulk will be
   unit tested. All unit tests will stress success and failure conditions.
   Boundary condition testing is also included.
   - The c2_io_fop* and c2_rpc_bulk* interfaces will be unit tested
   first in the order
	- c2_io_fop_init Check if the inline c2_fop and c2_rpc_bulk are
	initialized properly.
	- c2_rpc_bulk_page_add/c2_rpc_bulk_buffer_add to add pages/buffers
	to the rpc_bulk structure and cross check if they are actually added
	or not.
	- Add more pages/buffers to rpc_bulk structure to check if they
	return proper error code.
	- Try c2_io_fop_fini to check if an initialized c2_io_fop and
	the inline c2_rpc_bulk get properly finalized.
	- Initialize and start a network transport and a transfer machine.
	Invoke c2_rpc_bulk_store on rpc_bulk structure and cross check if
	the net buffer descriptor is properly populated in the io fop.
	- Tweak the parameters of transfer machine so that it goes into
	degraded/failed state and invoke c2_rpc_bulk_store and check if
	c2_rpc_bulk_store returns proper error code.
	- Start another transfer machine and invoke c2_rpc_bulk_load to
	check if it recognizes the net buf descriptor and starts buffer
	transfer properly.
	- Tweak the parameters of second transfer machine so that it goes
	into degraded/failed state and invoke c2_rpc_bulk_load and check if
	it returns proper error code.

   <hr>
   @section bulkclient-st System Tests

   Not applicable.

   <hr>
   @section bulkclient-O Analysis

   - m denotes the number of IO fops with same fid and intent (read/write).
   - n denotes the total number of IO segments in m IO fops.
   - Memory consumption O(n) During IO coalescing, n number of IO segments
   are allocated and subsequently deallocated, once the resulting IO fop
   is created.
   - Processor cycles O(n) During IO coalescing, all n segments are traversed
   and resultant IO fop is created.
   - Locks Minimal locks since locking is mostly taken care by upper layers.
   - Messages Not applicable.

   <hr>
   @section bulkclient-ref References

   - <a href="https://docs.google.com/a/xyratex.com/document/d/1tm_IfkSsW6zfOxQlPMHeZ5gjF1Xd0FAUHeGOaNpUcHA/edit?hl=en_US">RPC Bulk Transfer Task Plan</a>
   - <a href="https://docs.google.com/a/xyratex.com/Doc?docid=0ATg1HFjUZcaZZGNkNXg4cXpfMjQ3Z3NraDI4ZG0&hl=en_US">Detailed level design HOWTO</a>,
   an older document on which this style guide is partially based.

 */

/**
   @defgroup bulkclientDFSInternal IO bulk client Detailed Function Spec
   @brief Detailed Function Specification for IO bulk client.

   @{
 */

/**
 * Generic io segment that represents a contiguous stream of bytes
 * along with io extent. This structure is typically used by io coalescing
 * code from ioservice.
 */
struct ioseg {
	/* Magic constant to verify sanity of structure. */
	uint64_t		 is_magic;
	/* Index in target object to start io from. */
	c2_bindex_t		 is_index;
	/* Number of bytes in io segment. */
	c2_bcount_t		 is_size;
	/* Starting address of buffer. */
	void			*is_buf;
	/*
	 * Linkage to have such IO segments in a list hanging off
	 * io_seg_set::iss_list.
	 */
	struct c2_tlink		 is_linkage;
};

/** Represents coalesced set of io segments. */
struct io_seg_set {
	/** Magic constant to verify sanity of structure. */
	uint64_t	iss_magic;
	/** List of struct ioseg. */
	struct c2_tl	iss_list;
};

C2_TL_DESCR_DEFINE(iosegset, "list of coalesced io segments", static,
		   struct ioseg, is_linkage, is_magic,
		   C2_IOS_IO_SEGMENT_MAGIC, C2_IOS_IO_SEGMENT_SET_MAGIC);

C2_TL_DEFINE(iosegset, static, struct ioseg);

static void ioseg_get(const struct c2_0vec *zvec, uint32_t seg_index,
		      struct ioseg *seg)
{
	C2_PRE(zvec != NULL);
	C2_PRE(seg_index < zvec->z_bvec.ov_vec.v_nr);
	C2_PRE(seg != NULL);

	seg->is_index = zvec->z_index[seg_index];
	seg->is_size = zvec->z_bvec.ov_vec.v_count[seg_index];
	seg->is_buf = zvec->z_bvec.ov_buf[seg_index];
}

static bool io_fop_invariant(struct c2_io_fop *iofop)
{
	int i;

	if (iofop == NULL || iofop->if_magic != C2_IO_FOP_MAGIC)
		return false;

	for (i = 0; i < ARRAY_SIZE(ioservice_fops); ++i)
		if (iofop->if_fop.f_type == ioservice_fops[i])
			break;

	return i != ARRAY_SIZE(ioservice_fops);
}

int c2_io_fop_init(struct c2_io_fop *iofop, struct c2_fop_type *ftype)
{
	int rc;

	C2_PRE(iofop != NULL);
	C2_PRE(ftype != NULL);

	c2_fop_init(&iofop->if_fop, ftype, NULL);
	rc = c2_fop_data_alloc(&iofop->if_fop);
	if (rc == 0) {
		iofop->if_fop.f_item.ri_ops = &io_req_rpc_item_ops;
		iofop->if_magic = C2_IO_FOP_MAGIC;

		c2_rpc_bulk_init(&iofop->if_rbulk);
		C2_POST(io_fop_invariant(iofop));
	} else
		C2_ADDB_ADD(&bulkclient_addb, &bulkclient_addb_loc,
			    bulkclient_func_fail, "io fop data alloc failed.", rc);
	return rc;
}
C2_EXPORTED(c2_io_fop_init);

void c2_io_fop_fini(struct c2_io_fop *iofop)
{
	C2_PRE(io_fop_invariant(iofop));

	c2_rpc_bulk_fini(&iofop->if_rbulk);
	c2_fop_fini(&iofop->if_fop);
}

struct c2_rpc_bulk *c2_fop_to_rpcbulk(const struct c2_fop *fop)
{
	struct c2_io_fop *iofop;

	C2_PRE(fop != NULL);

	iofop = container_of(fop, struct c2_io_fop, if_fop);
	return &iofop->if_rbulk;
}
C2_EXPORTED(c2_fop_to_rpcbulk);

/** @} end of bulkclientDFSInternal */

bool c2_is_read_fop(const struct c2_fop *fop)
{
	C2_PRE(fop != NULL);
	return fop->f_type == &c2_fop_cob_readv_fopt;
}
C2_EXPORTED(c2_is_read_fop);

bool c2_is_write_fop(const struct c2_fop *fop)
{
	C2_PRE(fop != NULL);
	return fop->f_type == &c2_fop_cob_writev_fopt;
}
C2_EXPORTED(c2_is_write_fop);

bool c2_is_io_fop(const struct c2_fop *fop)
{
	return c2_is_read_fop(fop) || c2_is_write_fop(fop);
}

bool c2_is_read_fop_rep(const struct c2_fop *fop)
{
	C2_PRE(fop != NULL);
	return fop->f_type == &c2_fop_cob_readv_rep_fopt;
}

bool is_write_rep(const struct c2_fop *fop)
{
	C2_PRE(fop != NULL);
	return fop->f_type == &c2_fop_cob_writev_rep_fopt;
}

bool c2_is_io_fop_rep(const struct c2_fop *fop)
{
	return c2_is_read_fop_rep(fop) || is_write_rep(fop);
}

bool c2_is_cob_create_fop(const struct c2_fop *fop)
{
	C2_PRE(fop != NULL);
	return fop->f_type == &c2_fop_cob_create_fopt;
}

bool c2_is_cob_delete_fop(const struct c2_fop *fop)
{
	C2_PRE(fop != NULL);
	return fop->f_type == &c2_fop_cob_delete_fopt;
}

bool c2_is_cob_create_delete_fop(const struct c2_fop *fop)
{
	return c2_is_cob_create_fop(fop) || c2_is_cob_delete_fop(fop);
}

struct c2_fop_cob_common *c2_cobfop_common_get(struct c2_fop *fop)
{
	struct c2_fop_cob_create *cc;
	struct c2_fop_cob_delete *cd;

	C2_PRE(fop != NULL);
	C2_PRE(fop->f_type != NULL);
	C2_PRE(c2_is_cob_create_delete_fop(fop));

	if (fop->f_type == &c2_fop_cob_create_fopt) {
		cc = c2_fop_data(fop);
		return &cc->cc_common;
	} else {
		cd = c2_fop_data(fop);
		return &cd->cd_common;
	}
}

struct c2_fop_cob_rw *io_rw_get(struct c2_fop *fop)
{
	struct c2_fop_cob_readv  *rfop;
	struct c2_fop_cob_writev *wfop;

	C2_PRE(fop != NULL);
	C2_PRE(c2_is_io_fop(fop));

	if (c2_is_read_fop(fop)) {
		rfop = c2_fop_data(fop);
		return &rfop->c_rwv;
	} else {
		wfop = c2_fop_data(fop);
		return &wfop->c_rwv;
	}
}
C2_EXPORTED(io_rw_get);

struct c2_fop_cob_rw_reply *io_rw_rep_get(struct c2_fop *fop)
{
	struct c2_fop_cob_readv_rep	*rfop;
	struct c2_fop_cob_writev_rep	*wfop;

	C2_PRE(fop != NULL);
	C2_PRE(c2_is_io_fop_rep(fop));

	if (c2_is_read_fop_rep(fop)) {
		rfop = c2_fop_data(fop);
		return &rfop->c_rep;
	} else {
		wfop = c2_fop_data(fop);
		return &wfop->c_rep;
	}
}

static struct c2_0vec *io_0vec_get(struct c2_rpc_bulk_buf *rbuf)
{
	C2_PRE(rbuf != NULL);

	return &rbuf->bb_zerovec;
}

static void ioseg_unlink_free(struct ioseg *ioseg)
{
	C2_PRE(ioseg != NULL);
	C2_PRE(iosegset_tlink_is_in(ioseg));

	iosegset_tlist_del(ioseg);
	c2_free(ioseg);
}

/**
   Returns if given 2 fops belong to same type.
 */
static bool io_fop_type_equal(const struct c2_fop *fop1,
			      const struct c2_fop *fop2)
{
	C2_PRE(fop1 != NULL);
	C2_PRE(fop2 != NULL);

	return fop1->f_type == fop2->f_type;
}

static int io_fop_seg_init(struct ioseg **ns, const struct ioseg *cseg)
{
	struct ioseg *new_seg;

	C2_PRE(ns != NULL);
	C2_PRE(cseg != NULL);

	C2_ALLOC_PTR_ADDB(new_seg, &bulkclient_addb, &bulkclient_addb_loc);
	if (new_seg == NULL)
		return -ENOMEM;

	*ns = new_seg;
	*new_seg = *cseg;
	iosegset_tlink_init(new_seg);
	return 0;
}

static int io_fop_seg_add_cond(struct ioseg *cseg, const struct ioseg *nseg)
{
	int           rc;
	struct ioseg *new_seg;

	C2_PRE(cseg != NULL);
	C2_PRE(nseg != NULL);

	if (nseg->is_index < cseg->is_index) {
		rc = io_fop_seg_init(&new_seg, nseg);
		if (rc != 0)
			return rc;

		iosegset_tlist_add_before(cseg, new_seg);
	} else
		rc = -EINVAL;

	return rc;
}

static void io_fop_seg_coalesce(const struct ioseg *seg,
				struct io_seg_set *aggr_set)
{
	int           rc;
	struct ioseg *new_seg;
	struct ioseg *ioseg;

	C2_PRE(seg != NULL);
	C2_PRE(aggr_set != NULL);

	/*
	 * Coalesces all io segments in increasing order of offset.
	 * This will create new net buffer/s which will be associated with
	 * only one io fop and it will be sent on wire. While rest of io fops
	 * will hang off a list c2_rpc_item::ri_compound_items.
	 */
	c2_tl_for(iosegset, &aggr_set->iss_list, ioseg) {
		rc = io_fop_seg_add_cond(ioseg, seg);
		if (rc == 0 || rc == -ENOMEM)
			return;
	} c2_tl_endfor;

	rc = io_fop_seg_init(&new_seg, seg);
	if (rc != 0)
		return;
	iosegset_tlist_add_tail(&aggr_set->iss_list, new_seg);
}

static void io_fop_segments_coalesce(const struct c2_0vec *iovec,
				     struct io_seg_set *aggr_set)
{
	uint32_t     i;
	struct ioseg seg;

	C2_PRE(iovec != NULL);
	C2_PRE(aggr_set != NULL);

	/*
	 * For each segment from incoming IO vector, check if it can
	 * be merged with any of the existing segments from aggr_set.
	 * If yes, merge it else, add a new entry in aggr_set.
	 */
	for (i = 0; i < iovec->z_bvec.ov_vec.v_nr; ++i) {
		ioseg_get(iovec, i, &seg);
		io_fop_seg_coalesce(&seg, aggr_set);
	}
}

static inline struct c2_net_domain *io_fop_netdom_get(const struct c2_fop *fop)
{
	return io_fop_tm_get(fop)->ntm_dom;
}

/*
 * Creates and populates net buffers as needed using the list of
 * coalesced io segments.
 */
static int io_netbufs_prepare(struct c2_fop *coalesced_fop,
			      struct io_seg_set *seg_set)
{
	int			 rc;
	int32_t			 max_segs_nr;
	int32_t			 curr_segs_nr;
	int32_t			 nr;
	c2_bcount_t		 max_bufsize;
	c2_bcount_t		 curr_bufsize;
	uint32_t		 segs_nr;
	struct ioseg		*ioseg;
	struct c2_net_domain	*netdom;
	struct c2_rpc_bulk	*rbulk;
	struct c2_rpc_bulk_buf	*buf;

	C2_PRE(coalesced_fop != NULL);
	C2_PRE(seg_set != NULL);
	C2_PRE(!iosegset_tlist_is_empty(&seg_set->iss_list));

	netdom = io_fop_netdom_get(coalesced_fop);
	max_bufsize = c2_net_domain_get_max_buffer_size(netdom);
	max_segs_nr = c2_net_domain_get_max_buffer_segments(netdom);
	rbulk = c2_fop_to_rpcbulk(coalesced_fop);
	curr_segs_nr = iosegset_tlist_length(&seg_set->iss_list);
	ioseg = iosegset_tlist_head(&seg_set->iss_list);

	while (curr_segs_nr != 0) {
		curr_bufsize = 0;
		segs_nr = 0;
		/*
		 * Calculates the number of segments that can fit into max
		 * buffer size. These are needed to add a c2_rpc_bulk_buf
		 * structure into struct c2_rpc_bulk. Selected io segments
		 * are removed from io segments list, hence the loop always
		 * starts from the first element.
		 */
		c2_tl_for(iosegset, &seg_set->iss_list, ioseg) {
			if (curr_bufsize + ioseg->is_size <= max_bufsize &&
			    segs_nr <= max_segs_nr) {
				curr_bufsize += ioseg->is_size;
				++segs_nr;
			} else
				break;
		} c2_tl_endfor;

		rc = c2_rpc_bulk_buf_add(rbulk, segs_nr, netdom, NULL, &buf);
		if (rc != 0)
			goto cleanup;

		nr = 0;
		c2_tl_for(iosegset, &seg_set->iss_list, ioseg) {
			rc = c2_rpc_bulk_buf_databuf_add(buf, ioseg->is_buf,
							 ioseg->is_size,
							 ioseg->is_index,
							 netdom);

			/*
			 * Since size and fragment calculations are made before
			 * hand, this buffer addition should succeed.
			 */
			C2_ASSERT(rc == 0);

			ioseg_unlink_free(ioseg);
			if (++nr == segs_nr)
				break;
		} c2_tl_endfor;
		C2_POST(c2_vec_count(&buf->bb_zerovec.z_bvec.ov_vec) <=
			max_bufsize);
		C2_POST(buf->bb_zerovec.z_bvec.ov_vec.v_nr <= max_segs_nr);
		curr_segs_nr -= segs_nr;
	}
	return 0;
cleanup:
	C2_ASSERT(rc != 0);
	c2_rpc_bulk_buflist_empty(rbulk);
	return rc;
}

/* Deallocates memory claimed by index vector/s from io fop wire format. */
void io_fop_ivec_dealloc(struct c2_fop *fop)
{
	int			 i;
	struct c2_fop_cob_rw	*rw;
	struct c2_io_indexvec	*ivec;

	C2_PRE(fop != NULL);

	rw = io_rw_get(fop);
	ivec = rw->crw_ivecs.cis_ivecs;

	for (i = 0; i < rw->crw_ivecs.cis_nr; ++i) {
		c2_free(ivec[i].ci_iosegs);
		ivec[i].ci_iosegs = NULL;
	}
	c2_free(ivec);
	rw->crw_ivecs.cis_ivecs = NULL;
	rw->crw_ivecs.cis_nr = 0;
}

/* Allocates memory for index vector/s from io fop wore format. */
static int io_fop_ivec_alloc(struct c2_fop *fop, struct c2_rpc_bulk *rbulk)
{
	int			 cnt = 0;
	struct c2_fop_cob_rw	*rw;
	struct c2_io_indexvec	*ivec;
	struct c2_rpc_bulk_buf	*rbuf;

	C2_PRE(fop != NULL);
	C2_PRE(rbulk != NULL);
	C2_PRE(c2_mutex_is_locked(&rbulk->rb_mutex));

	rbulk = c2_fop_to_rpcbulk(fop);
	rw = io_rw_get(fop);
	rw->crw_ivecs.cis_nr = rpcbulk_tlist_length(&rbulk->rb_buflist);
	C2_ALLOC_ARR_ADDB(rw->crw_ivecs.cis_ivecs, rw->crw_ivecs.cis_nr,
			  &bulkclient_addb, &bulkclient_addb_loc);
	if (rw->crw_ivecs.cis_ivecs == NULL)
		return -ENOMEM;

	ivec = rw->crw_ivecs.cis_ivecs;
	c2_tl_for(rpcbulk, &rbulk->rb_buflist, rbuf) {
		C2_ALLOC_ARR_ADDB(ivec[cnt].ci_iosegs,
				  rbuf->bb_zerovec.z_bvec.ov_vec.v_nr,
				  &bulkclient_addb, &bulkclient_addb_loc);
		if (ivec[cnt].ci_iosegs == NULL)
			goto cleanup;
		ivec[cnt].ci_nr = rbuf->bb_zerovec.z_bvec.ov_vec.v_nr;
		++cnt;
	} c2_tl_endfor;
	return 0;
cleanup:
	io_fop_ivec_dealloc(fop);
	return -ENOMEM;
}

/* Populates index vector/s from io fop wire format. */
static void io_fop_ivec_prepare(struct c2_fop *res_fop,
				struct c2_rpc_bulk *rbulk)
{
	int			 cnt = 0;
	uint32_t		 j;
	struct c2_fop_cob_rw	*rw;
	struct c2_io_indexvec	*ivec;
	struct c2_rpc_bulk_buf	*buf;

	C2_PRE(res_fop != NULL);
	C2_PRE(rbulk != NULL);
	C2_PRE(c2_mutex_is_locked(&rbulk->rb_mutex));

	rw = io_rw_get(res_fop);
	rw->crw_ivecs.cis_nr = rpcbulk_tlist_length(&rbulk->rb_buflist);
	ivec = rw->crw_ivecs.cis_ivecs;

	/*
	 * Adds same number of index vector in io fop as there are buffers in
	 * c2_rpc_bulk::rb_buflist.
	 */
	c2_tl_for(rpcbulk, &rbulk->rb_buflist, buf) {
		for (j = 0; j < ivec[cnt].ci_nr ; ++j) {
			ivec[cnt].ci_iosegs[j].ci_index =
				buf->bb_zerovec.z_index[j];
			ivec[cnt].ci_iosegs[j].ci_count =
				buf->bb_zerovec.z_bvec.ov_vec.v_count[j];
		}
		++cnt;
	} c2_tl_endfor;
}

static void io_fop_bulkbuf_move(struct c2_fop *src, struct c2_fop *dest)
{
	struct c2_rpc_bulk	*sbulk;
	struct c2_rpc_bulk	*dbulk;
	struct c2_rpc_bulk_buf	*rbuf;
	struct c2_fop_cob_rw	*srw;
	struct c2_fop_cob_rw	*drw;

	C2_PRE(src != NULL);
	C2_PRE(dest != NULL);

	sbulk = c2_fop_to_rpcbulk(src);
	dbulk = c2_fop_to_rpcbulk(dest);
	c2_mutex_lock(&sbulk->rb_mutex);
	c2_tl_for(rpcbulk, &sbulk->rb_buflist, rbuf) {
		rpcbulk_tlist_del(rbuf);
		rpcbulk_tlist_add(&dbulk->rb_buflist, rbuf);
	} c2_tl_endfor;
	dbulk->rb_bytes = sbulk->rb_bytes;
	dbulk->rb_rc = sbulk->rb_rc;
	c2_mutex_unlock(&sbulk->rb_mutex);

	srw = io_rw_get(src);
	drw = io_rw_get(dest);
	drw->crw_desc = srw->crw_desc;
	drw->crw_ivecs = srw->crw_ivecs;
}

static int io_fop_desc_alloc(struct c2_fop *fop, struct c2_rpc_bulk *rbulk)
{
	struct c2_fop_cob_rw	*rw;

	C2_PRE(fop != NULL);
	C2_PRE(rbulk != NULL);
	C2_PRE(c2_mutex_is_locked(&rbulk->rb_mutex));

	rbulk = c2_fop_to_rpcbulk(fop);
	rw = io_rw_get(fop);
	rw->crw_desc.id_nr = rpcbulk_tlist_length(&rbulk->rb_buflist);
	C2_ALLOC_ARR_ADDB(rw->crw_desc.id_descs, rw->crw_desc.id_nr,
			  &bulkclient_addb, &bulkclient_addb_loc);

	return rw->crw_desc.id_descs == NULL ? -ENOMEM : 0;
}

static void io_fop_desc_dealloc(struct c2_fop *fop)
{
	uint32_t                 i;
	struct c2_fop_cob_rw	*rw;

	C2_PRE(fop != NULL);

	rw = io_rw_get(fop);

	/*
	 * These descriptors are allocated by c2_rpc_bulk_store()
	 * code during adding them as part of on-wire representation
	 * of io fop. They should not be deallocated by rpc code
	 * since it will unnecessarily pollute rpc layer code
	 * with io details.
	 */
	for (i = 0; i < rw->crw_desc.id_nr; ++i)
		c2_net_desc_free(&rw->crw_desc.id_descs[i]);

	c2_free(rw->crw_desc.id_descs);
	rw->crw_desc.id_descs = NULL;
	rw->crw_desc.id_nr = 0;
}

/*
 * Allocates memory for net buf descriptors array and index vector array
 * and populates the array of index vectors in io fop wire format.
 */
int c2_io_fop_prepare(struct c2_fop *fop)
{
	int		       rc;
	struct c2_rpc_bulk    *rbulk;
	enum c2_net_queue_type q;

	C2_PRE(fop != NULL);
	C2_PRE(c2_is_io_fop(fop));

	rbulk = c2_fop_to_rpcbulk(fop);
	c2_mutex_lock(&rbulk->rb_mutex);
	rc = io_fop_desc_alloc(fop, rbulk);
	if (rc != 0) {
		rc = -ENOMEM;
		goto err;
	}

	rc = io_fop_ivec_alloc(fop, rbulk);
	if (rc != 0) {
		io_fop_desc_dealloc(fop);
		rc = -ENOMEM;
		goto err;
	}

	io_fop_ivec_prepare(fop, rbulk);
	q = c2_is_read_fop(fop) ? C2_NET_QT_PASSIVE_BULK_RECV :
			   C2_NET_QT_PASSIVE_BULK_SEND;
	c2_rpc_bulk_qtype(rbulk, q);
err:
	c2_mutex_unlock(&rbulk->rb_mutex);
	return rc;
}
C2_EXPORTED(c2_io_fop_prepare);

/*
 * Creates new net buffers from aggregate list and adds them to
 * associated c2_rpc_bulk object. Also calls c2_io_fop_prepare() to
 * allocate memory for net buf desc sequence and index vector
 * sequence in io fop wire format.
 */
static int io_fop_desc_ivec_prepare(struct c2_fop *fop,
				    struct io_seg_set *aggr_set)
{
	int			rc;
	struct c2_rpc_bulk     *rbulk;

	C2_PRE(fop != NULL);
	C2_PRE(aggr_set != NULL);

	rbulk = c2_fop_to_rpcbulk(fop);

	rc = io_netbufs_prepare(fop, aggr_set);
	if (rc != 0) {
		C2_ADDB_ADD(&bulkclient_addb, &bulkclient_addb_loc,
			    bulkclient_func_fail,
			    "io_fop_desc_ivec_prepare failed.", rc);
		return rc;
	}

	rc = c2_io_fop_prepare(fop);
	if (rc != 0)
		c2_rpc_bulk_buflist_empty(rbulk);

	return rc;
}

/*
 * Deallocates memory for sequence of net buf desc and sequence of index
 * vectors from io fop wire format.
 */
void c2_io_fop_destroy(struct c2_fop *fop)
{
	C2_PRE(fop != NULL);

	io_fop_desc_dealloc(fop);
	io_fop_ivec_dealloc(fop);
}

static inline size_t io_fop_size_get(struct c2_fop *fop)
{
	struct c2_xcode_ctx  ctx;

	C2_PRE(fop != NULL);
	C2_PRE(fop->f_type != NULL);

	c2_xcode_ctx_init(&ctx, &C2_FOP_XCODE_OBJ(fop));
	return c2_xcode_length(&ctx);
}

/**
 * Coalesces the io fops with same fid and intent (read/write). A list of
 * coalesced io segments is generated which is attached to a single
 * io fop - res_fop (which is already bound to a session) in form of
 * one of more network buffers and rest of the io fops hang off a list
 * c2_rpc_item::ri_compound_items in resultant fop.
 * The index vector array from io fop is also populated from the list of
 * coalesced io segments.
 * The res_fop contents are backed up and restored on receiving reply
 * so that upper layer is transparent of these operations.
 * @see item_io_coalesce().
 * @see c2_io_fop_init().
 * @see c2_rpc_bulk_init().
 */
static int io_fop_coalesce(struct c2_fop *res_fop, uint64_t size)
{
	int			   rc;
	struct c2_fop		  *fop;
	struct c2_fop		  *bkp_fop;
	struct c2_tl		  *items_list;
	struct c2_0vec		  *iovec;
	struct ioseg		  *ioseg;
	struct c2_io_fop	  *cfop;
	struct io_seg_set	   aggr_set;
	struct c2_rpc_item	  *item;
	struct c2_rpc_bulk	  *rbulk;
	struct c2_rpc_bulk	  *bbulk;
	struct c2_fop_cob_rw	  *rw;
	struct c2_rpc_bulk_buf    *rbuf;
	struct c2_net_transfer_mc *tm;

	C2_PRE(res_fop != NULL);
	C2_PRE(c2_is_io_fop(res_fop));

	C2_ALLOC_PTR_ADDB(cfop, &bulkclient_addb, &bulkclient_addb_loc);
	if (cfop == NULL)
		return -ENOMEM;

	rc = c2_io_fop_init(cfop, res_fop->f_type);
	if (rc != 0) {
		c2_free(cfop);
		return rc;
	}
	tm = io_fop_tm_get(res_fop);
	bkp_fop = &cfop->if_fop;
	aggr_set.iss_magic = C2_IOS_IO_SEGMENT_SET_MAGIC;
	iosegset_tlist_init(&aggr_set.iss_list);

	/*
	 * Traverses the fop_list, get the IO vector from each fop,
	 * pass it to a coalescing routine and get result back
	 * in another list.
	 */
	items_list = &res_fop->f_item.ri_compound_items;
	C2_ASSERT(!rpcitem_tlist_is_empty(items_list));

	c2_tl_for(rpcitem, items_list, item) {
		fop = c2_rpc_item_to_fop(item);
		rbulk = c2_fop_to_rpcbulk(fop);
		c2_mutex_lock(&rbulk->rb_mutex);
		c2_tl_for(rpcbulk, &rbulk->rb_buflist, rbuf) {
			iovec = io_0vec_get(rbuf);
			io_fop_segments_coalesce(iovec, &aggr_set);
		} c2_tl_endfor;
		c2_mutex_unlock(&rbulk->rb_mutex);
	} c2_tl_endfor;

	/*
	 * Removes c2_rpc_bulk_buf from the c2_rpc_bulk::rb_buflist and
	 * add it to same list belonging to bkp_fop.
	 */
	io_fop_bulkbuf_move(res_fop, bkp_fop);

	/*
	 * Prepares net buffers from set of io segments, allocates memory
	 * for net buf desriptors and index vectors and populates the index
	 * vectors
	 */
	rc = io_fop_desc_ivec_prepare(res_fop, &aggr_set);
	if (rc != 0)
		goto cleanup;

	/*
	 * Adds the net buffers from res_fop to transfer machine and
	 * populates res_fop with net buf descriptor/s got from network
	 * buffer addition.
	 */
	rw = io_rw_get(res_fop);
	rbulk = c2_fop_to_rpcbulk(res_fop);
	rc = c2_rpc_bulk_store(rbulk, res_fop->f_item.ri_session->s_conn,
			       rw->crw_desc.id_descs);
	if (rc != 0) {
		C2_ADDB_ADD(&bulkclient_addb, &bulkclient_addb_loc,
			    bulkclient_func_fail,
			    "c2_rpc_bulk_store() failed for coalesced io fop.",
			    rc);
		c2_io_fop_destroy(res_fop);
		goto cleanup;
	}

	/*
	 * Checks if current size of res_fop fits into the size
	 * provided as input.
	 */
	if (io_fop_size_get(res_fop) > size) {
		C2_ADDB_ADD(&bulkclient_addb, &bulkclient_addb_loc,
			    bulkclient_func_fail, "Size of coalesced fop"
			    "exceeded remaining space in send net buffer.",
			    -EMSGSIZE);
		c2_mutex_lock(&rbulk->rb_mutex);
		c2_tl_for(rpcbulk, &rbulk->rb_buflist, rbuf) {
			c2_net_buffer_del(rbuf->bb_nbuf, tm);
		} c2_tl_endfor;
		c2_mutex_unlock(&rbulk->rb_mutex);
		c2_io_fop_destroy(res_fop);
		goto cleanup;
	}

	/*
	 * Removes the net buffers belonging to coalesced member fops
	 * from transfer machine since these buffers are coalesced now
	 * and are part of res_fop.
	 */
	c2_tl_for(rpcitem, items_list, item) {
		fop = c2_rpc_item_to_fop(item);
		if (fop == res_fop)
			continue;
		rbulk = c2_fop_to_rpcbulk(fop);
		c2_mutex_lock(&rbulk->rb_mutex);
		c2_tl_for(rpcbulk, &rbulk->rb_buflist, rbuf) {
			c2_net_buffer_del(rbuf->bb_nbuf, tm);
		} c2_tl_endfor;
		c2_mutex_unlock(&rbulk->rb_mutex);
	} c2_tl_endfor;

	/*
	 * Removes the net buffers from transfer machine contained by rpc bulk
	 * structure belonging to res_fop since they will be replaced by
	 * new coalesced net buffers.
	 */
	bbulk = c2_fop_to_rpcbulk(bkp_fop);
	rbulk = c2_fop_to_rpcbulk(res_fop);
	c2_mutex_lock(&bbulk->rb_mutex);
	c2_mutex_lock(&rbulk->rb_mutex);
	c2_tl_for(rpcbulk, &bbulk->rb_buflist, rbuf) {
		rpcbulk_tlist_del(rbuf);
		rpcbulk_tlist_add(&rbulk->rb_buflist, rbuf);
		c2_net_buffer_del(rbuf->bb_nbuf, tm);
		rbulk->rb_bytes -= c2_vec_count(&rbuf->bb_nbuf->
						nb_buffer.ov_vec);
	} c2_tl_endfor;
	c2_mutex_unlock(&rbulk->rb_mutex);
	c2_mutex_unlock(&bbulk->rb_mutex);

	C2_POST(rw->crw_desc.id_nr == rw->crw_ivecs.cis_nr);
	C2_ADDB_ADD(&bulkclient_addb, &bulkclient_addb_loc, c2_addb_trace,
		    "io fops coalesced successfully.");
	rpcitem_tlist_add(items_list, &bkp_fop->f_item);
	return rc;
cleanup:
	C2_ASSERT(rc != 0);
	c2_tl_for(iosegset, &aggr_set.iss_list, ioseg) {
		ioseg_unlink_free(ioseg);
	} c2_tl_endfor;
	iosegset_tlist_fini(&aggr_set.iss_list);
	io_fop_bulkbuf_move(bkp_fop, res_fop);
	c2_io_fop_fini(cfop);
	c2_free(cfop);
	return rc;
}
C2_EXPORTED(io_fop_coalesce);

static struct c2_fop_file_fid *io_fop_fid_get(struct c2_fop *fop)
{
	return &(io_rw_get(fop))->crw_fid;
}

static bool io_fop_fid_equal(struct c2_fop *fop1, struct c2_fop *fop2)
{
	struct c2_fop_file_fid *ffid1;
	struct c2_fop_file_fid *ffid2;

	C2_PRE(fop1 != NULL);
	C2_PRE(fop2 != NULL);

	ffid1 = io_fop_fid_get(fop1);
	ffid2 = io_fop_fid_get(fop2);

	return ffid1->f_seq == ffid2->f_seq && ffid1->f_oid == ffid2->f_oid;
}

static void io_fop_replied(struct c2_fop *fop, struct c2_fop *bkpfop)
{
	struct c2_io_fop     *cfop;
	struct c2_rpc_bulk   *rbulk;
	struct c2_fop_cob_rw *srw;
	struct c2_fop_cob_rw *drw;

	C2_PRE(fop != NULL);
	C2_PRE(bkpfop != NULL);
	C2_PRE(c2_is_io_fop(fop));
	C2_PRE(c2_is_io_fop(bkpfop));

	rbulk = c2_fop_to_rpcbulk(fop);
	c2_mutex_lock(&rbulk->rb_mutex);
	C2_ASSERT(rpcbulk_tlist_is_empty(&rbulk->rb_buflist));
	c2_mutex_unlock(&rbulk->rb_mutex);

	srw = io_rw_get(bkpfop);
	drw = io_rw_get(fop);
	drw->crw_desc = srw->crw_desc;
	drw->crw_ivecs = srw->crw_ivecs;
	cfop = container_of(bkpfop, struct c2_io_fop, if_fop);
	c2_io_fop_fini(cfop);
	c2_free(cfop);
}
C2_EXPORTED(io_fop_replied);

static void io_fop_desc_get(struct c2_fop *fop, struct c2_net_buf_desc **desc)
{
	struct c2_fop_cob_rw *rw;

	C2_PRE(fop != NULL);
	C2_PRE(desc != NULL);

	rw = io_rw_get(fop);
	*desc = rw->crw_desc.id_descs;
}
C2_EXPORTED(io_fop_desc_get);

static void cob_rpcitem_free(struct c2_rpc_item *item)
{
	struct c2_fop *fop;

	C2_PRE(item != NULL);

	fop = c2_rpc_item_to_fop(item);
	C2_ASSERT(c2_is_cob_create_delete_fop(fop));

	if (c2_is_cob_create_fop(fop)) {
		struct c2_fop_cob_create *cc;
		cc = c2_fop_data(fop);
		c2_free(cc->cc_cobname.cn_name);
	}
	c2_fop_free(fop);
}

/* Rpc item ops for IO operations. */
static void io_item_replied(struct c2_rpc_item *item)
{
	struct c2_fop		   *fop;
	struct c2_fop		   *rfop;
	struct c2_fop		   *bkpfop;
	struct c2_rpc_item	   *ritem;
	struct c2_rpc_bulk	   *rbulk;
	struct c2_fop_cob_rw_reply *reply;

	C2_PRE(item != NULL);

	fop = c2_rpc_item_to_fop(item);
	rbulk = c2_fop_to_rpcbulk(fop);
	rfop = c2_rpc_item_to_fop(item->ri_reply);
	reply = io_rw_rep_get(rfop);

	C2_ASSERT(ergo(reply->rwr_rc == 0,
		       reply->rwr_count == rbulk->rb_bytes));

	if (reply->rwr_rc != 0)
		C2_ADDB_ADD(&bulkclient_addb, &bulkclient_addb_loc,
			    bulkclient_func_fail, "io fop failed.",
			    item->ri_error);

	/*
	 * Restores the contents of master coalesced fop from the first
	 * rpc item in c2_rpc_item::ri_compound_items list. This item
	 * is inserted by io coalescing code.
	 */
	if (!rpcitem_tlist_is_empty(&item->ri_compound_items)) {
		C2_ADDB_ADD(&bulkclient_addb, &bulkclient_addb_loc,
			    c2_addb_trace,
			    "Reply received for coalesced io fops.");
		c2_io_fop_destroy(fop);
		ritem = rpcitem_tlist_head(&item->ri_compound_items);
		rpcitem_tlist_del(ritem);
		bkpfop = c2_rpc_item_to_fop(ritem);
		if (fop->f_type->ft_ops->fto_fop_replied != NULL)
			fop->f_type->ft_ops->fto_fop_replied(fop, bkpfop);
	}

	/*
	 * The rpc_item->ri_chan is signaled by sessions code
	 * (rpc_item_replied()) which is why only member coalesced items
	 * (items which were member of a parent coalesced item) are
	 * signaled from here as they are not sent on wire but hang off
	 * a list from parent coalesced item.
	 */
	c2_tl_for(rpcitem, &item->ri_compound_items, ritem) {
		fop = c2_rpc_item_to_fop(ritem);
		rbulk = c2_fop_to_rpcbulk(fop);
		c2_mutex_lock(&rbulk->rb_mutex);
		C2_ASSERT(rbulk != NULL && c2_tlist_is_empty(&rpcbulk_tl,
			  &rbulk->rb_buflist));
		/* Notifies all member coalesced items of completion status. */
		rbulk->rb_rc = item->ri_error;
		c2_mutex_unlock(&rbulk->rb_mutex);
		c2_chan_broadcast(&ritem->ri_chan);
	} c2_tl_endfor;
}

static void item_io_coalesce(struct c2_rpc_item *head, struct c2_list *list,
			     uint64_t size)
{
	int			 rc;
	struct c2_fop		*bfop;
	struct c2_fop		*ufop;
	struct c2_rpc_item	*item;
	struct c2_rpc_item	*item_next;

	C2_PRE(head != NULL);
	C2_PRE(list != NULL);
	C2_PRE(size > 0);

	if (c2_list_is_empty(list))
		return;

	/*
	 * Traverses through the list and finds out items that match with
	 * head on basis of fid and intent (read/write). Matching items
	 * are removed from session->s_unbound_items list and added to
	 * head->compound_items list.
	 */
	bfop = c2_rpc_item_to_fop(head);
	c2_list_for_each_entry_safe(list, item, item_next, struct c2_rpc_item,
				    ri_unbound_link) {
		ufop = c2_rpc_item_to_fop(item);
		if (io_fop_type_equal(bfop, ufop) &&
		    io_fop_fid_equal(bfop, ufop)) {
			/*
			 * The item is removed from session->unbound list
			 * only when io coalescing succeeds because
			 * this routine is called from a loop over
			 * session->unbound items and any tinkering will
			 * mess up the order of processing of items in
			 * session->unbound items list.
			 */
			rpcitem_tlist_add(&head->ri_compound_items, item);
		}
	}

	if (rpcitem_tlist_is_empty(&head->ri_compound_items))
		return;

	/*
	 * Add the bound item to list of compound items as this will
	 * include the bound item's io vector in io coalescing
	 */
	rpcitem_tlist_add(&head->ri_compound_items, head);

	rc = bfop->f_type->ft_ops->fto_io_coalesce(bfop, size);
	if (rc != 0) {
		c2_tl_for(rpcitem, &head->ri_compound_items, item) {
			C2_ADDB_ADD(&bulkclient_addb, &bulkclient_addb_loc,
				    bulkclient_func_fail,
				    "io_fop_coalesce failed.", rc);
			rpcitem_tlist_del(item);
		} c2_tl_endfor;
	} else {
		/*
		 * Item at head is the backup item which is not present
		 * in sessions unbound list.
		 */
		item_next = rpcitem_tlist_head(&head->ri_compound_items);
		rpcitem_tlist_del(head);
		c2_tl_for (rpcitem, &head->ri_compound_items, item) {
			if (item != item_next)
				c2_list_del(&item->ri_unbound_link);
		} c2_tl_endfor;
	}
}

static void io_item_free_internal(struct c2_rpc_item *item)
{
	struct c2_fop    *fop;
	struct c2_io_fop *iofop;

	C2_PRE(item != NULL);

	fop = c2_rpc_item_to_fop(item);
	iofop = container_of(fop, struct c2_io_fop, if_fop);
	c2_io_fop_destroy(&iofop->if_fop);
	c2_io_fop_fini(iofop);
	c2_free(iofop);
}

/*
 * From bulk client side, IO REQUEST fops are typically bundled in
 * struct c2_io_fop. So c2_io_fop is deallocated from here.
 */
static void io_item_free(struct c2_rpc_item *item)
{
	struct c2_rpc_item *ri;

	C2_PRE(item != NULL);

	c2_tl_for (rpcitem, &item->ri_compound_items, ri) {
		rpcitem_tlist_del(ri);
		io_item_free_internal(ri);
	} c2_tl_endfor;

	io_item_free_internal(item);
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
