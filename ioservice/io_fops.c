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

#undef M0_ADDB_CT_CREATE_DEFINITION
#define M0_ADDB_CT_CREATE_DEFINITION
#include "ioservice/io_service_addb.h"

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "lib/vec.h"	/* m0_0vec */
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_IOSERVICE
#include "lib/trace.h"
#include "lib/tlist.h"
#include "addb/addb.h"
#include "mero/magic.h"
#include "fop/fop_item_type.h"
#include "rpc/item.h"
#include "rpc/rpc_opcodes.h"
#include "rpc/rpc.h"
#include "ioservice/io_fops.h"
#include "ioservice/io_fops_xc.h"
#include "fop/fom_generic.h"

/**
 * This addb ctx would be used only to post for exception records
 * where io/cob foms ctx would not be available.
 * This happens mainly in case of service_allocation()'s memory allocation
 * & other memory allocation failures.
 */
struct m0_addb_ctx m0_ios_addb_ctx;

/* tlists and tlist APIs referred from rpc layer. */
M0_TL_DESCR_DECLARE(rpcbulk, M0_EXTERN);
M0_TL_DESCR_DECLARE(rpcitem, M0_EXTERN);
M0_TL_DECLARE(rpcbulk, M0_INTERNAL, struct m0_rpc_bulk_buf);
M0_TL_DECLARE(rpcitem, M0_INTERNAL, struct m0_rpc_item);

static struct m0_fid *io_fop_fid_get(struct m0_fop *fop);

static void item_io_coalesce(struct m0_rpc_item *head, struct m0_list *list,
			     uint64_t size);
static void io_item_replied (struct m0_rpc_item *item);
static void io_fop_replied  (struct m0_fop *fop, struct m0_fop *bkpfop);
static void io_fop_desc_get (struct m0_fop *fop, struct m0_net_buf_desc **desc);
static int  io_fop_coalesce (struct m0_fop *res_fop, uint64_t size);
static void item_io_coalesce(struct m0_rpc_item *head, struct m0_list *list,
			     uint64_t size);

struct m0_fop_type m0_fop_cob_readv_fopt;
struct m0_fop_type m0_fop_cob_writev_fopt;
struct m0_fop_type m0_fop_cob_readv_rep_fopt;
struct m0_fop_type m0_fop_cob_writev_rep_fopt;
struct m0_fop_type m0_fop_cob_create_fopt;
struct m0_fop_type m0_fop_cob_delete_fopt;
struct m0_fop_type m0_fop_cob_op_reply_fopt;
struct m0_fop_type m0_fop_fv_notification_fopt;

M0_EXPORTED(m0_fop_cob_writev_fopt);
M0_EXPORTED(m0_fop_cob_readv_fopt);

static struct m0_fop_type *ioservice_fops[] = {
	&m0_fop_cob_readv_fopt,
	&m0_fop_cob_writev_fopt,
	&m0_fop_cob_readv_rep_fopt,
	&m0_fop_cob_writev_rep_fopt,
	&m0_fop_cob_create_fopt,
	&m0_fop_cob_delete_fopt,
	&m0_fop_cob_op_reply_fopt,
	&m0_fop_fv_notification_fopt,
};

/* Used for IO REQUEST items only. */
const struct m0_rpc_item_ops io_req_rpc_item_ops = {
	.rio_replied	= io_item_replied,
};

static const struct m0_rpc_item_type_ops io_item_type_ops = {
	M0_FOP_DEFAULT_ITEM_TYPE_OPS,
        .rito_io_coalesce    = item_io_coalesce,
};

const struct m0_fop_type_ops io_fop_rwv_ops = {
	.fto_fop_replied = io_fop_replied,
	.fto_io_coalesce = io_fop_coalesce,
	.fto_io_desc_get = io_fop_desc_get,
};

struct m0_fol_rec_part_type m0_io_write_part_type;
struct m0_fol_rec_part_type m0_io_create_part_type;
struct m0_fol_rec_part_type m0_io_delete_part_type;

M0_INTERNAL void m0_ioservice_fop_fini(void)
{
	m0_fop_type_fini(&m0_fop_cob_op_reply_fopt);
	m0_fop_type_fini(&m0_fop_fv_notification_fopt);
	m0_fop_type_fini(&m0_fop_cob_delete_fopt);
	m0_fop_type_fini(&m0_fop_cob_create_fopt);
	m0_fop_type_fini(&m0_fop_cob_writev_rep_fopt);
	m0_fop_type_fini(&m0_fop_cob_readv_rep_fopt);
	m0_fop_type_fini(&m0_fop_cob_writev_fopt);
	m0_fop_type_fini(&m0_fop_cob_readv_fopt);

	m0_fol_rec_part_type_fini(&m0_io_write_part_type);
	m0_fol_rec_part_type_fini(&m0_io_create_part_type);
	m0_fol_rec_part_type_fini(&m0_io_delete_part_type);

	m0_xc_io_fops_fini();
	m0_addb_ctx_fini(&m0_ios_addb_ctx);
}

extern struct m0_reqh_service_type m0_ios_type;
extern const struct m0_fom_type_ops cob_fom_type_ops;
extern const struct m0_fom_type_ops io_fom_type_ops;

extern const struct m0_sm_conf io_conf;
extern struct m0_sm_state_descr io_phases[];

M0_INTERNAL int m0_ioservice_fop_init(void)
{
	m0_addb_ctx_type_register(&m0_addb_ct_ios_mod);
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &m0_ios_addb_ctx, &m0_addb_ct_ios_mod,
			 &m0_addb_proc_ctx);
	m0_xc_io_fops_init();
#ifndef __KERNEL__
	m0_sm_conf_extend(m0_generic_conf.scf_state, io_phases,
			  m0_generic_conf.scf_nr_states);
#endif
	return  M0_FOP_TYPE_INIT(&m0_fop_cob_readv_fopt,
				 .name      = "Read request",
				 .opcode    = M0_IOSERVICE_READV_OPCODE,
				 .xt        = m0_fop_cob_readv_xc,
				 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
				 .fop_ops   = &io_fop_rwv_ops,
#ifndef __KERNEL__
				 .fom_ops   = &io_fom_type_ops,
				 .sm        = &io_conf,
				 .svc_type  = &m0_ios_type,
#endif
				 .rpc_ops   = &io_item_type_ops) ?:
		M0_FOP_TYPE_INIT(&m0_fop_cob_writev_fopt,
				 .name      = "Write request",
				 .opcode    = M0_IOSERVICE_WRITEV_OPCODE,
				 .xt        = m0_fop_cob_writev_xc,
				 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
					      M0_RPC_ITEM_TYPE_MUTABO,
				 .fop_ops   = &io_fop_rwv_ops,
#ifndef __KERNEL__
				 .fom_ops   = &io_fom_type_ops,
				 .sm        = &io_conf,
				 .svc_type  = &m0_ios_type,
#endif
				 .rpc_ops   = &io_item_type_ops) ?:
		M0_FOP_TYPE_INIT(&m0_fop_cob_readv_rep_fopt,
				 .name      = "Read reply",
				 .opcode    = M0_IOSERVICE_READV_REP_OPCODE,
				 .xt        = m0_fop_cob_readv_rep_xc,
				 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY) ?:
		M0_FOP_TYPE_INIT(&m0_fop_cob_writev_rep_fopt,
				 .name      = "Write request",
				 .opcode    = M0_IOSERVICE_WRITEV_REP_OPCODE,
				 .xt        = m0_fop_cob_writev_rep_xc,
				 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY) ?:
		M0_FOP_TYPE_INIT(&m0_fop_cob_create_fopt,
				 .name      = "Cob create request",
				 .opcode    = M0_IOSERVICE_COB_CREATE_OPCODE,
				 .xt        = m0_fop_cob_create_xc,
				 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
#ifndef __KERNEL__
				 .fom_ops   = &cob_fom_type_ops,
				 .svc_type  = &m0_ios_type,
#endif
				 .sm        = &m0_generic_conf) ?:
		M0_FOP_TYPE_INIT(&m0_fop_cob_delete_fopt,
				 .name      = "Cob delete request",
				 .opcode    = M0_IOSERVICE_COB_DELETE_OPCODE,
				 .xt        = m0_fop_cob_delete_xc,
				 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
#ifndef __KERNEL__
				 .fom_ops   = &cob_fom_type_ops,
				 .svc_type  = &m0_ios_type,
#endif
				 .sm        = &m0_generic_conf) ?:
		M0_FOP_TYPE_INIT(&m0_fop_cob_op_reply_fopt,
				 .name      = "Cob create or delete reply",
				 .opcode    =  M0_IOSERVICE_COB_OP_REPLY_OPCODE,
				 .xt        = m0_fop_cob_op_reply_xc,
				 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY) ?:
		M0_FOP_TYPE_INIT(&m0_fop_fv_notification_fopt,
				 .name   = "Failure vector update notification",
				 .opcode = M0_IOSERVICE_FV_NOTIFICATION_OPCODE,
				 .xt        = m0_fop_fv_notification_xc,
				 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
					      M0_RPC_ITEM_TYPE_ONEWAY) ?:
		m0_fol_rec_part_type_init(&m0_io_write_part_type, "IO write record part",
					  m0_io_write_rec_part_xc);
		m0_fol_rec_part_type_init(&m0_io_create_part_type, "IO create record part",
					  m0_io_create_rec_part_xc) ?:
		m0_fol_rec_part_type_init(&m0_io_delete_part_type, "IO delete record part",
					  m0_io_delete_rec_part_xc);
}

/**
   @page IOFOLDLD IO FOL DLD

   - @ref IOFOLDLD-ovw
   - @ref IOFOLDLD-def
   - @ref IOFOLDLD-req
   - @ref IOFOLDLD-depends
   - @ref IOFOLDLD-highlights
   - @ref IOFOLDLD-fspec "Functional Specification"
   - @ref IOFOLDLD-lspec
   - @ref IOFOLDLD-ut
   - @ref IOFOLDLD-st
   - @ref IOFOLDLD-O
   - @ref IOFOLDLD-ref

   <hr>
   @section IOFOLDLD-ovw Overview
   This document describes the design of logging of FOL records for create, delete
   and write operations in ioservice.

   <hr>
   @section IOFOLDLD-def Definitions
   FOL (File operation log) is a per-node collection of records, describing
   updates to file system state carried out on the node.
   IO FOL is log of records for create, delete and write operations in ioservice
   which are used to perform undo or as a reply cache.

   Each File system object called unit has "verno of its latest state" as an
   attribute. This attribute is modified with every update to unit state.

   A verno consists of two components:
	-LSN (lsn): A reference to a FOL record, points to the record with
		    the last update for the unit.
	-VC: A version counter

   <hr>
   @section IOFOLDLD-req Requirements

   <hr>
   @section IOFOLDLD-depends Dependencies

   For every new write data must be written to new block.
   So that these older data blocks can be used to perform write undo.
   <hr>
   @section IOFOLDLD-highlights Design Highlights

   For each update made on server corresponding FOL record part is
   populated and added in the FOM transaction FOL record parts list.

   These FOL record parts are encoded in a single FOL record in a
   FOM generic phase after updates are executed.

   <hr>
   @section IOFOLDLD-fspec Functional Specification
   The following new APIs are introduced:

   @see m0_fol_rec_part : FOL record part.
   @see m0_fol_rec_part_type : FOL record part type.

   m0_fol_rec_part_ops contains operations for undo and redo of
   FOL record parts.

   @see m0_fol_rec_part_init() : Initializes m0_fol_rec_part with
				 m0_fol_rec_part_type_ops.
   @see m0_fol_rec_part_fini() : Finalizes FOL record part.

   @see m0_fol_rec_part_type_register() : Registers FOL record part type.
   @see m0_fol_rec_part_type_deregister() : Deregisters FOL record part type.

   FOL Record parts in ioservice,
   @see io_write_rec_part  : write updates
   @see io_create_rec_part : create updates
   @see io_delete_rec_part : delete updates

   @see ad_rec_part is added for AD write operation.

   FOL record parts list is kept in m0_fol_rec::fr_fol_rec_parts which is
   initialized in m0_fol_rec_init()

   m0_fol_rec_add() is used to compose FOL record from FOL record descriptor
   and parts.
   fol_record_encode() encodes the FOL record parts in the list
   m0_fol_rec:fr_fol_rec_parts in a buffer, which then will be added into the db
   using m0_fol_add_buf().

   @see m0_fol_rec_lookup()

   Usage:
   @code
   struct m0_foo {
	int f_key;
	int f_val;
   } M0_XCA_RECORD;

   const struct m0_fol_rec_part_ops foo_part_ops = {
	.rpo_type = &foo_part_type,
	.rpo_undo = NULL,
	.rpo_redo = NULL,
   };

   static void foo_rec_part_init(struct m0_fol_rec_part *part)
   {
	   part->rp_ops = &foo_part_ops;
   }

   const struct m0_fol_rec_part_type_ops foo_part_type_ops = {
	.rpto_rec_part_init = &foo_rec_part_init,
   };

   struct m0_fol_rec_part_type foo_part_type {
	.rpt_name = "foo FOL record part type",
	.rpt_xt   = m0_foo_xc,
	.rpt_ops  = &foo_part_type_ops
   };

   struct m0_fol_rec rec;

   void foo_fol_rec_part_add(void)
   {
	int			result;
	struct m0_fol_rec_part  foo_rec_part;
	struct m0_foo	       *foo;

	m0_fol_rec_part_list_init(&rec);
	result =  m0_fol_rec_part_type_register(&foo_part_type);
        M0_ASSERT(result == 0);

	M0_ALLOC_PTR(foo != NULL);
	M0_ASSERT(foo != NULL);
	m0_fol_rec_part_init(&foo_rec_part, foo, &foo_part_type);

	foo->f_key = 22;
        foo->f_val = 33;

	m0_fol_rec_part_list_add(&rec, foo_rec_part);

	// FOL record descriptor and parts in the list are added to db
	// in fom generic phase by using m0_fom_fol_rec_add()
	...

	m0_fol_rec_part_list_fini(&rec);
	m0_fol_rec_part_type_deregister(&foo_part_type);
   }
   @endcode

   m0_fol_rec_part_type_init() and m0_fol_rec_part_type_fini() are added
   to initialize and finalize FOL part types.
   FOL recors part types are registered in a global array of FOL record
   parts using m0_fol_rec_part_type::rpt_index.

   For each of create, delete and write IO operations FOL record parts are
   initialised with their xcode type and FOL operations.

   For create and delete operations fop data and reply fop data is stored
   in FOL record parts.
	- fop data including fid.
	- Reply fop data is added in FOL records so that it can be used
	  as Reply Cache.

   For write operation, in ad_write_launch() store AD allocated extents in
   FOL record part struct ad_rec_part.

   Add these FOL record parts in the list.

   After successful execution of updates on server side, in FOM generic phase
   using m0_fom_fol_rec_add() FOL record parts in the list are combined in a
   FOL record and is made persistent. Before this phase all FOL record parts
   needs to be added in the list after completing their updates.

   After retrieving FOL record from data base, FOL record parts are decoded
   based on part type using index and are used in undo or redo operations.
   <hr>
   @section IOFOLDLD-conformance Conformance

   <hr>
   @section IOFOLDLD-ut Unit Tests

   1) For create and delete updates,
	An io fop with a given fid is send to the ioservice, where it creates
	a cob with that fid and logs a FOL record.

	Now retrieve that FOL record using the same LSN and assert for fid and
	reply fop data.

	Also using this data, execute the cob delete opeartion on server side
	(undo operation).

	Simlilarly, do the same things for delete operation.

   2) For Write update,
	Send the data having value "A" from client to ioservice which logs fid
	and data extents in FOL record. Then send the data having value "B" to
	the ioservice.

	Now retrieve the data extents of the first write operation from FOL record
	and update the AD table by decoding ad_rec_part from FOL record.
	Then read the data from ioservice and assert for data "A".

   @endcode
   <hr>
   @section IOFOLDLD-st System Tests

   <hr>
   @section IOFOLDLD-O Analysis

   <hr>
   @section IOFOLDLD-ref References
   - <a href="https://docs.google.com/a/xyratex.com/document/d/1aNYxF5UcGiRnT2Inrf2RP5xBK5frP9ZtIK21LE1sMxI/edit"> HLD of version numbers </a>
   - <a href="https://docs.google.com/a/xyratex.com/document/d/1_5UGU0n7CATMiuG6V9eK3cMshiYFotPVnIy478MMnvM/edit"> HLD of FOL</a>,
   - <a href="https://docs.google.com/a/xyratex.com/document/d/1_N-YJZ4XcUkkhDG843lxS2TNF2YlmOjfZQelbD017jU/edit">HLD of data block allocator</a>.
   - @ref fol
   - @ref stobad

 */

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
   Mero client file system and data server (ioservice aka bulk io server).
   Mero network layer incorporates a bulk transport mechanism to
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

   - m0t1fs - Mero client file system. It works as a kernel module.
   - Bulk transport - Event based, asynchronous message passing functionality
   of Mero network layer.
   - io fop - A generic io fop that is used for read and write.
   - rpc bulk - An interface to abstract the usage of network buffers by
   client and server programs.
   - ioservice - A service providing io routines in Mero. It runs only
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

   - r.misc.net_rpc_convert Bulk Client needs Mero client file system to be
   using new network layer apis which include m0_net_domain and m0_net_buffer.
   - r.fop.referring_another_fop With introduction of a net buffer
   descriptor in io fop, a mechanism needs to be introduced so that fop
   definitions from one component can refer to definitions from another
   component. m0_net_buf_desc is a fop used to represent on-wire
   representation of a m0_net_buffer. @see m0_net_buf_desc.

   <hr>
   @section bulkclient-highlights Design Highlights

   IO bulk client uses a generic in-memory structure representing an io fop
   and its associated network buffer.
   This in-memory io fop contains another abstract structure to represent
   the network buffer associated with the fop.
   The bulk client creates m0_io_fop structures as necessary and attaches
   kernel pages or user space vector to associated m0_rpc_bulk structure
   and submits the fop to rpc layer.
   Rpc layer populates the network buffer descriptor embedded in the io fop
   and sends the fop over wire. The associated network buffer is added to
   appropriate buffer queue of transfer machine owned by rpc layer.
   Once, the receiver side receives the io fop, it acquires a local network
   buffer and calls a m0_rpc_bulk apis to start the zero-copy.
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
	S1 -> S2 [label = "m0_rpc_bulk_init()"]
	S1 -> S8 [label = "Failed"]
	S2 -> S8 [label = "Failed"]
	S2 -> S3 [label = "m0_rpc_bulk_buf_page_add()"]
	S3 -> S8 [label = "Failed"]
	S3 -> S4 [label = "m0_rpc_bulk_store()"]
	S4 -> S5 [label = "m0_rpc_post()"]
	S5 -> S6 [label = "m0_chan_wait(item->ri_chan)"]
	S6 -> S7 [label = "m0_chan_signal(item->ri_chan)"]
	S7 -> S8 [label = "m0_rpc_bulk_fini(rpc_bulk)"]
   }
   @enddot

   @subsection bulkclient-lspec-thread Threading and Concurrency Model

   No need of explicit locking for structures like m0_io_fop and ioseg
   since they are taken care by locking at upper layers like locking at
   the m0t1fs part for dispatching IO requests.

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
   net buffer descriptor. @see m0_net_buf_desc
   - I.bulkclient.iocoalescing Since all IO coalescing code is built around
   the definition of IO fop, it will conform to new format of io fop.

   <hr>
   @section bulkclient-ut Unit Tests

   All external interfaces based on m0_io_fop and m0_rpc_bulk will be
   unit tested. All unit tests will stress success and failure conditions.
   Boundary condition testing is also included.
   - The m0_io_fop* and m0_rpc_bulk* interfaces will be unit tested
   first in the order
	- m0_io_fop_init Check if the inline m0_fop and m0_rpc_bulk are
	initialized properly.
	- m0_rpc_bulk_page_add/m0_rpc_bulk_buffer_add to add pages/buffers
	to the rpc_bulk structure and cross check if they are actually added
	or not.
	- Add more pages/buffers to rpc_bulk structure to check if they
	return proper error code.
	- Try m0_io_fop_fini to check if an initialized m0_io_fop and
	the inline m0_rpc_bulk get properly finalized.
	- Initialize and start a network transport and a transfer machine.
	Invoke m0_rpc_bulk_store on rpc_bulk structure and cross check if
	the net buffer descriptor is properly populated in the io fop.
	- Tweak the parameters of transfer machine so that it goes into
	degraded/failed state and invoke m0_rpc_bulk_store and check if
	m0_rpc_bulk_store returns proper error code.
	- Start another transfer machine and invoke m0_rpc_bulk_load to
	check if it recognizes the net buf descriptor and starts buffer
	transfer properly.
	- Tweak the parameters of second transfer machine so that it goes
	into degraded/failed state and invoke m0_rpc_bulk_load and check if
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

   - <a href="https://docs.google.com/a/xyratex.com/document/d/
1tm_IfkSsW6zfOxQlPMHeZ5gjF1Xd0FAUHeGOaNpUcHA/edit?hl=en_US">
RPC Bulk Transfer Task Plan</a>
   - <a href="https://docs.google.com/a/xyratex.com/
Doc?docid=0ATg1HFjUZcaZZGNkNXg4cXpfMjQ3Z3NraDI4ZG0&hl=en_US">
Detailed level design HOWTO</a>,
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
	m0_bindex_t		 is_index;
	/* Number of bytes in io segment. */
	m0_bcount_t		 is_size;
	/* Starting address of buffer. */
	void			*is_buf;
	/*
	 * Linkage to have such IO segments in a list hanging off
	 * io_seg_set::iss_list.
	 */
	struct m0_tlink		 is_linkage;
};

/** Represents coalesced set of io segments. */
struct io_seg_set {
	/** Magic constant to verify sanity of structure. */
	uint64_t	iss_magic;
	/** List of struct ioseg. */
	struct m0_tl	iss_list;
};

M0_TL_DESCR_DEFINE(iosegset, "list of coalesced io segments", static,
		   struct ioseg, is_linkage, is_magic,
		   M0_IOS_IO_SEGMENT_MAGIC, M0_IOS_IO_SEGMENT_SET_MAGIC);

M0_TL_DEFINE(iosegset, static, struct ioseg);

static void ioseg_get(const struct m0_0vec *zvec, uint32_t seg_index,
		      struct ioseg *seg)
{
	M0_PRE(zvec != NULL);
	M0_PRE(seg_index < zvec->z_bvec.ov_vec.v_nr);
	M0_PRE(seg != NULL);

	seg->is_index = zvec->z_index[seg_index];
	seg->is_size = zvec->z_bvec.ov_vec.v_count[seg_index];
	seg->is_buf = zvec->z_bvec.ov_buf[seg_index];
}

static bool io_fop_invariant(struct m0_io_fop *iofop)
{
	int i;

	if (iofop == NULL || iofop->if_magic != M0_IO_FOP_MAGIC)
		return false;

	for (i = 0; i < ARRAY_SIZE(ioservice_fops); ++i)
		if (iofop->if_fop.f_type == ioservice_fops[i])
			break;

	return i != ARRAY_SIZE(ioservice_fops);
}

M0_INTERNAL int m0_io_fop_init(struct m0_io_fop *iofop,
			       struct m0_fop_type *ftype,
			       void (*fop_release)(struct m0_ref *))
{
	int rc;

	M0_PRE(iofop != NULL);
	M0_PRE(ftype != NULL);

	m0_fop_init(&iofop->if_fop, ftype, NULL,
		    fop_release ?: m0_io_fop_release);
	rc = m0_fop_data_alloc(&iofop->if_fop);
	if (rc == 0) {
		iofop->if_fop.f_item.ri_ops = &io_req_rpc_item_ops;
		iofop->if_magic = M0_IO_FOP_MAGIC;

		m0_rpc_bulk_init(&iofop->if_rbulk);
		M0_POST(io_fop_invariant(iofop));
	} else {
		IOS_ADDB_FUNCFAIL(rc, IO_FOP_INIT, &m0_ios_addb_ctx);
	}
	return rc;
}

M0_INTERNAL void m0_io_fop_fini(struct m0_io_fop *iofop)
{
	M0_PRE(io_fop_invariant(iofop));
	m0_rpc_bulk_fini(&iofop->if_rbulk);
	m0_fop_fini(&iofop->if_fop);
}

M0_INTERNAL struct m0_rpc_bulk *m0_fop_to_rpcbulk(const struct m0_fop *fop)
{
	struct m0_io_fop *iofop;

	M0_PRE(fop != NULL);

	iofop = container_of(fop, struct m0_io_fop, if_fop);
	return &iofop->if_rbulk;
}

/** @} end of bulkclientDFSInternal */

M0_INTERNAL bool m0_is_read_fop(const struct m0_fop *fop)
{
	M0_PRE(fop != NULL);
	return fop->f_type == &m0_fop_cob_readv_fopt;
}

M0_INTERNAL bool m0_is_write_fop(const struct m0_fop *fop)
{
	M0_PRE(fop != NULL);
	return fop->f_type == &m0_fop_cob_writev_fopt;
}

M0_INTERNAL bool m0_is_io_fop(const struct m0_fop *fop)
{
	return m0_is_read_fop(fop) || m0_is_write_fop(fop);
}

M0_INTERNAL bool m0_is_read_fop_rep(const struct m0_fop *fop)
{
	M0_PRE(fop != NULL);
	return fop->f_type == &m0_fop_cob_readv_rep_fopt;
}

M0_INTERNAL bool is_write_rep(const struct m0_fop *fop)
{
	M0_PRE(fop != NULL);
	return fop->f_type == &m0_fop_cob_writev_rep_fopt;
}

M0_INTERNAL bool m0_is_io_fop_rep(const struct m0_fop *fop)
{
	return m0_is_read_fop_rep(fop) || is_write_rep(fop);
}

M0_INTERNAL bool m0_is_cob_create_fop(const struct m0_fop *fop)
{
	M0_PRE(fop != NULL);
	return fop->f_type == &m0_fop_cob_create_fopt;
}

M0_INTERNAL bool m0_is_cob_delete_fop(const struct m0_fop *fop)
{
	M0_PRE(fop != NULL);
	return fop->f_type == &m0_fop_cob_delete_fopt;
}

M0_INTERNAL bool m0_is_cob_create_delete_fop(const struct m0_fop *fop)
{
	return m0_is_cob_create_fop(fop) || m0_is_cob_delete_fop(fop);
}

M0_INTERNAL struct m0_fop_cob_common *m0_cobfop_common_get(struct m0_fop *fop)
{
	struct m0_fop_cob_create *cc;
	struct m0_fop_cob_delete *cd;

	M0_PRE(fop != NULL);
	M0_PRE(fop->f_type != NULL);
	M0_PRE(m0_is_cob_create_delete_fop(fop));

	if (fop->f_type == &m0_fop_cob_create_fopt) {
		cc = m0_fop_data(fop);
		return &cc->cc_common;
	} else {
		cd = m0_fop_data(fop);
		return &cd->cd_common;
	}
}

M0_INTERNAL struct m0_fop_cob_rw *io_rw_get(struct m0_fop *fop)
{
	struct m0_fop_cob_readv  *rfop;
	struct m0_fop_cob_writev *wfop;

	M0_PRE(fop != NULL);
	M0_PRE(m0_is_io_fop(fop));

	if (m0_is_read_fop(fop)) {
		rfop = m0_fop_data(fop);
		return &rfop->c_rwv;
	} else {
		wfop = m0_fop_data(fop);
		return &wfop->c_rwv;
	}
}

M0_INTERNAL struct m0_fop_cob_rw_reply *io_rw_rep_get(struct m0_fop *fop)
{
	struct m0_fop_cob_readv_rep	*rfop;
	struct m0_fop_cob_writev_rep	*wfop;

	M0_PRE(fop != NULL);
	M0_PRE(m0_is_io_fop_rep(fop));

	if (m0_is_read_fop_rep(fop)) {
		rfop = m0_fop_data(fop);
		return &rfop->c_rep;
	} else {
		wfop = m0_fop_data(fop);
		return &wfop->c_rep;
	}
}

static struct m0_0vec *io_0vec_get(struct m0_rpc_bulk_buf *rbuf)
{
	M0_PRE(rbuf != NULL);

	return &rbuf->bb_zerovec;
}

static void ioseg_unlink_free(struct ioseg *ioseg)
{
	M0_PRE(ioseg != NULL);
	M0_PRE(iosegset_tlink_is_in(ioseg));

	iosegset_tlist_del(ioseg);
	m0_free(ioseg);
}

/**
   Returns if given 2 fops belong to same type.
 */
static bool io_fop_type_equal(const struct m0_fop *fop1,
			      const struct m0_fop *fop2)
{
	M0_PRE(fop1 != NULL);
	M0_PRE(fop2 != NULL);

	return fop1->f_type == fop2->f_type;
}

static int io_fop_seg_init(struct ioseg **ns, const struct ioseg *cseg)
{
	struct ioseg *new_seg = 0;

	M0_PRE(ns != NULL);
	M0_PRE(cseg != NULL);

	IOS_ALLOC_PTR(new_seg, &m0_ios_addb_ctx, IO_FOP_SEG_INIT);
	if (new_seg == NULL)
		return -ENOMEM;

	*ns = new_seg;
	M0_ASSERT(new_seg != NULL); /* suppress compiler warning on next stmt */
	*new_seg = *cseg;
	iosegset_tlink_init(new_seg);
	return 0;
}

static int io_fop_seg_add_cond(struct ioseg *cseg, const struct ioseg *nseg)
{
	int           rc;
	struct ioseg *new_seg;

	M0_PRE(cseg != NULL);
	M0_PRE(nseg != NULL);

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

	M0_PRE(seg != NULL);
	M0_PRE(aggr_set != NULL);

	/*
	 * Coalesces all io segments in increasing order of offset.
	 * This will create new net buffer/s which will be associated with
	 * only one io fop and it will be sent on wire. While rest of io fops
	 * will hang off a list m0_rpc_item::ri_compound_items.
	 */
	m0_tl_for(iosegset, &aggr_set->iss_list, ioseg) {
		rc = io_fop_seg_add_cond(ioseg, seg);
		if (rc == 0 || rc == -ENOMEM)
			return;
	} m0_tl_endfor;

	rc = io_fop_seg_init(&new_seg, seg);
	if (rc != 0)
		return;
	iosegset_tlist_add_tail(&aggr_set->iss_list, new_seg);
}

static void io_fop_segments_coalesce(const struct m0_0vec *iovec,
				     struct io_seg_set *aggr_set)
{
	uint32_t     i;
	struct ioseg seg = { 0 };

	M0_PRE(iovec != NULL);
	M0_PRE(aggr_set != NULL);

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

static inline struct m0_net_domain *io_fop_netdom_get(const struct m0_fop *fop)
{
	return io_fop_tm_get(fop)->ntm_dom;
}

/*
 * Creates and populates net buffers as needed using the list of
 * coalesced io segments.
 */
static int io_netbufs_prepare(struct m0_fop *coalesced_fop,
			      struct io_seg_set *seg_set)
{
	int			 rc;
	int32_t			 max_segs_nr;
	int32_t			 curr_segs_nr;
	int32_t			 nr;
	m0_bcount_t		 max_bufsize;
	m0_bcount_t		 curr_bufsize;
	uint32_t		 segs_nr;
	struct ioseg		*ioseg;
	struct m0_net_domain	*netdom;
	struct m0_rpc_bulk	*rbulk;
	struct m0_rpc_bulk_buf	*buf;

	M0_PRE(coalesced_fop != NULL);
	M0_PRE(seg_set != NULL);
	M0_PRE(!iosegset_tlist_is_empty(&seg_set->iss_list));

	netdom = io_fop_netdom_get(coalesced_fop);
	max_bufsize = m0_net_domain_get_max_buffer_size(netdom);
	max_segs_nr = m0_net_domain_get_max_buffer_segments(netdom);
	rbulk = m0_fop_to_rpcbulk(coalesced_fop);
	curr_segs_nr = iosegset_tlist_length(&seg_set->iss_list);
	ioseg = iosegset_tlist_head(&seg_set->iss_list);

	while (curr_segs_nr != 0) {
		curr_bufsize = 0;
		segs_nr = 0;
		/*
		 * Calculates the number of segments that can fit into max
		 * buffer size. These are needed to add a m0_rpc_bulk_buf
		 * structure into struct m0_rpc_bulk. Selected io segments
		 * are removed from io segments list, hence the loop always
		 * starts from the first element.
		 */
		m0_tl_for(iosegset, &seg_set->iss_list, ioseg) {
			if (curr_bufsize + ioseg->is_size <= max_bufsize &&
			    segs_nr <= max_segs_nr) {
				curr_bufsize += ioseg->is_size;
				++segs_nr;
			} else
				break;
		} m0_tl_endfor;

		rc = m0_rpc_bulk_buf_add(rbulk, segs_nr, netdom, NULL, &buf);
		if (rc != 0)
			goto cleanup;

		nr = 0;
		m0_tl_for(iosegset, &seg_set->iss_list, ioseg) {
			rc = m0_rpc_bulk_buf_databuf_add(buf, ioseg->is_buf,
							 ioseg->is_size,
							 ioseg->is_index,
							 netdom);

			/*
			 * Since size and fragment calculations are made before
			 * hand, this buffer addition should succeed.
			 */
			M0_ASSERT(rc == 0);

			ioseg_unlink_free(ioseg);
			if (++nr == segs_nr)
				break;
		} m0_tl_endfor;
		M0_POST(m0_vec_count(&buf->bb_zerovec.z_bvec.ov_vec) <=
			max_bufsize);
		M0_POST(buf->bb_zerovec.z_bvec.ov_vec.v_nr <= max_segs_nr);
		curr_segs_nr -= segs_nr;
	}
	return 0;
cleanup:
	M0_ASSERT(rc != 0);
	m0_rpc_bulk_buflist_empty(rbulk);
	return rc;
}

/* Deallocates memory claimed by index vector/s from io fop wire format. */
M0_INTERNAL void io_fop_ivec_dealloc(struct m0_fop *fop)
{
	int			 i;
	struct m0_fop_cob_rw	*rw;
	struct m0_io_indexvec	*ivec;

	M0_PRE(fop != NULL);

	rw = io_rw_get(fop);
	ivec = rw->crw_ivecs.cis_ivecs;

	for (i = 0; i < rw->crw_ivecs.cis_nr; ++i) {
		m0_free(ivec[i].ci_iosegs);
		ivec[i].ci_iosegs = NULL;
	}
	m0_free(ivec);
	rw->crw_ivecs.cis_ivecs = NULL;
	rw->crw_ivecs.cis_nr = 0;
}

/* Allocates memory for index vector/s from io fop wore format. */
static int io_fop_ivec_alloc(struct m0_fop *fop, struct m0_rpc_bulk *rbulk)
{
	int			 cnt = 0;
	struct m0_fop_cob_rw	*rw;
	struct m0_io_indexvec	*ivec;
	struct m0_rpc_bulk_buf	*rbuf;

	M0_PRE(fop != NULL);
	M0_PRE(rbulk != NULL);
	M0_PRE(m0_mutex_is_locked(&rbulk->rb_mutex));

	rbulk = m0_fop_to_rpcbulk(fop);
	rw = io_rw_get(fop);
	rw->crw_ivecs.cis_nr = rpcbulk_tlist_length(&rbulk->rb_buflist);
	IOS_ALLOC_ARR(rw->crw_ivecs.cis_ivecs, rw->crw_ivecs.cis_nr,
			  &m0_ios_addb_ctx, IO_FOP_IVEC_ALLOC_1);
	if (rw->crw_ivecs.cis_ivecs == NULL)
		return -ENOMEM;

	ivec = rw->crw_ivecs.cis_ivecs;
	m0_tl_for(rpcbulk, &rbulk->rb_buflist, rbuf) {
		IOS_ALLOC_ARR(ivec[cnt].ci_iosegs,
				  rbuf->bb_zerovec.z_bvec.ov_vec.v_nr,
				  &m0_ios_addb_ctx, IO_FOP_IVEC_ALLOC_2);
		if (ivec[cnt].ci_iosegs == NULL)
			goto cleanup;
		ivec[cnt].ci_nr = rbuf->bb_zerovec.z_bvec.ov_vec.v_nr;
		++cnt;
	} m0_tl_endfor;
	return 0;
cleanup:
	io_fop_ivec_dealloc(fop);
	return -ENOMEM;
}

/* Populates index vector/s from io fop wire format. */
static void io_fop_ivec_prepare(struct m0_fop *res_fop,
				struct m0_rpc_bulk *rbulk)
{
	int			 cnt = 0;
	uint32_t		 j;
	struct m0_fop_cob_rw	*rw;
	struct m0_io_indexvec	*ivec;
	struct m0_rpc_bulk_buf	*buf;

	M0_PRE(res_fop != NULL);
	M0_PRE(rbulk != NULL);
	M0_PRE(m0_mutex_is_locked(&rbulk->rb_mutex));

	rw = io_rw_get(res_fop);
	rw->crw_ivecs.cis_nr = rpcbulk_tlist_length(&rbulk->rb_buflist);
	ivec = rw->crw_ivecs.cis_ivecs;

	/*
	 * Adds same number of index vector in io fop as there are buffers in
	 * m0_rpc_bulk::rb_buflist.
	 */
	m0_tl_for(rpcbulk, &rbulk->rb_buflist, buf) {
		for (j = 0; j < ivec[cnt].ci_nr ; ++j) {
			ivec[cnt].ci_iosegs[j].ci_index =
				buf->bb_zerovec.z_index[j];
			ivec[cnt].ci_iosegs[j].ci_count =
				buf->bb_zerovec.z_bvec.ov_vec.v_count[j];
		}
		++cnt;
	} m0_tl_endfor;
}

static void io_fop_bulkbuf_move(struct m0_fop *src, struct m0_fop *dest)
{
	struct m0_rpc_bulk	*sbulk;
	struct m0_rpc_bulk	*dbulk;
	struct m0_rpc_bulk_buf	*rbuf;
	struct m0_fop_cob_rw	*srw;
	struct m0_fop_cob_rw	*drw;

	M0_PRE(src != NULL);
	M0_PRE(dest != NULL);

	sbulk = m0_fop_to_rpcbulk(src);
	dbulk = m0_fop_to_rpcbulk(dest);
	m0_mutex_lock(&sbulk->rb_mutex);
	m0_tl_for(rpcbulk, &sbulk->rb_buflist, rbuf) {
		rpcbulk_tlist_del(rbuf);
		rpcbulk_tlist_add(&dbulk->rb_buflist, rbuf);
	} m0_tl_endfor;
	dbulk->rb_bytes = sbulk->rb_bytes;
	dbulk->rb_rc = sbulk->rb_rc;
	m0_mutex_unlock(&sbulk->rb_mutex);

	srw = io_rw_get(src);
	drw = io_rw_get(dest);
	drw->crw_desc = srw->crw_desc;
	drw->crw_ivecs = srw->crw_ivecs;
}

static int io_fop_desc_alloc(struct m0_fop *fop, struct m0_rpc_bulk *rbulk)
{
	struct m0_fop_cob_rw	*rw;

	M0_PRE(fop != NULL);
	M0_PRE(rbulk != NULL);
	M0_PRE(m0_mutex_is_locked(&rbulk->rb_mutex));

	rbulk = m0_fop_to_rpcbulk(fop);
	rw = io_rw_get(fop);
	rw->crw_desc.id_nr = rpcbulk_tlist_length(&rbulk->rb_buflist);
	IOS_ALLOC_ARR(rw->crw_desc.id_descs, rw->crw_desc.id_nr,
			  &m0_ios_addb_ctx, IO_FOP_DESC_ALLOC);

	return rw->crw_desc.id_descs == NULL ? -ENOMEM : 0;
}

static void io_fop_desc_dealloc(struct m0_fop *fop)
{
	uint32_t                 i;
	struct m0_fop_cob_rw	*rw;

	M0_PRE(fop != NULL);

	rw = io_rw_get(fop);

	/*
	 * These descriptors are allocated by m0_rpc_bulk_store()
	 * code during adding them as part of on-wire representation
	 * of io fop. They should not be deallocated by rpc code
	 * since it will unnecessarily pollute rpc layer code
	 * with io details.
	 */
	for (i = 0; i < rw->crw_desc.id_nr; ++i)
		m0_net_desc_free(&rw->crw_desc.id_descs[i]);

	m0_free(rw->crw_desc.id_descs);
	rw->crw_desc.id_descs = NULL;
	rw->crw_desc.id_nr = 0;
}

/*
 * Allocates memory for net buf descriptors array and index vector array
 * and populates the array of index vectors in io fop wire format.
 */
M0_INTERNAL int m0_io_fop_prepare(struct m0_fop *fop)
{
	int		       rc;
	struct m0_rpc_bulk    *rbulk;
	enum m0_net_queue_type q;

	M0_PRE(fop != NULL);
	M0_PRE(m0_is_io_fop(fop));

	rbulk = m0_fop_to_rpcbulk(fop);
	m0_mutex_lock(&rbulk->rb_mutex);
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
	q = m0_is_read_fop(fop) ? M0_NET_QT_PASSIVE_BULK_RECV :
			   M0_NET_QT_PASSIVE_BULK_SEND;
	m0_rpc_bulk_qtype(rbulk, q);
err:
	m0_mutex_unlock(&rbulk->rb_mutex);
	return rc;
}

/*
 * Creates new net buffers from aggregate list and adds them to
 * associated m0_rpc_bulk object. Also calls m0_io_fop_prepare() to
 * allocate memory for net buf desc sequence and index vector
 * sequence in io fop wire format.
 */
static int io_fop_desc_ivec_prepare(struct m0_fop *fop,
				    struct io_seg_set *aggr_set)
{
	int			rc;
	struct m0_rpc_bulk     *rbulk;

	M0_PRE(fop != NULL);
	M0_PRE(aggr_set != NULL);

	rbulk = m0_fop_to_rpcbulk(fop);

	rc = io_netbufs_prepare(fop, aggr_set);
	if (rc != 0) {
		IOS_ADDB_FUNCFAIL(rc, IO_FOP_DESC_IVEC_PREP, &m0_ios_addb_ctx);
		return rc;
	}

	rc = m0_io_fop_prepare(fop);
	if (rc != 0)
		m0_rpc_bulk_buflist_empty(rbulk);

	return rc;
}

/*
 * Deallocates memory for sequence of net buf desc and sequence of index
 * vectors from io fop wire format.
 */
M0_INTERNAL void m0_io_fop_destroy(struct m0_fop *fop)
{
	M0_PRE(fop != NULL);

	io_fop_desc_dealloc(fop);
	io_fop_ivec_dealloc(fop);
}

M0_INTERNAL size_t m0_io_fop_size_get(struct m0_fop *fop)
{
	struct m0_xcode_ctx  ctx;

	M0_PRE(fop != NULL);
	M0_PRE(fop->f_type != NULL);

	m0_xcode_ctx_init(&ctx, &M0_FOP_XCODE_OBJ(fop));
	return m0_xcode_length(&ctx);
}

/**
 * Coalesces the io fops with same fid and intent (read/write). A list of
 * coalesced io segments is generated which is attached to a single
 * io fop - res_fop (which is already bound to a session) in form of
 * one of more network buffers and rest of the io fops hang off a list
 * m0_rpc_item::ri_compound_items in resultant fop.
 * The index vector array from io fop is also populated from the list of
 * coalesced io segments.
 * The res_fop contents are backed up and restored on receiving reply
 * so that upper layer is transparent of these operations.
 * @see item_io_coalesce().
 * @see m0_io_fop_init().
 * @see m0_rpc_bulk_init().
 */
static int io_fop_coalesce(struct m0_fop *res_fop, uint64_t size)
{
	int			   rc;
	struct m0_fop		  *fop;
	struct m0_fop		  *bkp_fop;
	struct m0_tl		  *items_list;
	struct m0_0vec		  *iovec;
	struct ioseg		  *ioseg;
	struct m0_io_fop	  *cfop;
	struct io_seg_set	   aggr_set;
	struct m0_rpc_item	  *item;
	struct m0_rpc_bulk	  *rbulk;
	struct m0_rpc_bulk	  *bbulk;
	struct m0_fop_cob_rw	  *rw;
	struct m0_rpc_bulk_buf    *rbuf;
	struct m0_net_transfer_mc *tm;

	M0_PRE(res_fop != NULL);
	M0_PRE(m0_is_io_fop(res_fop));

	IOS_ALLOC_PTR(cfop, &m0_ios_addb_ctx, IO_FOP_COALESCE_1);
	if (cfop == NULL)
		return -ENOMEM;

	rc = m0_io_fop_init(cfop, res_fop->f_type, NULL);
	if (rc != 0) {
		m0_free(cfop);
		return rc;
	}
	tm = io_fop_tm_get(res_fop);
	bkp_fop = &cfop->if_fop;
	aggr_set.iss_magic = M0_IOS_IO_SEGMENT_SET_MAGIC;
	iosegset_tlist_init(&aggr_set.iss_list);

	/*
	 * Traverses the fop_list, get the IO vector from each fop,
	 * pass it to a coalescing routine and get result back
	 * in another list.
	 */
	items_list = &res_fop->f_item.ri_compound_items;
	M0_ASSERT(!rpcitem_tlist_is_empty(items_list));

	m0_tl_for(rpcitem, items_list, item) {
		fop = m0_rpc_item_to_fop(item);
		rbulk = m0_fop_to_rpcbulk(fop);
		m0_mutex_lock(&rbulk->rb_mutex);
		m0_tl_for(rpcbulk, &rbulk->rb_buflist, rbuf) {
			iovec = io_0vec_get(rbuf);
			io_fop_segments_coalesce(iovec, &aggr_set);
		} m0_tl_endfor;
		m0_mutex_unlock(&rbulk->rb_mutex);
	} m0_tl_endfor;

	/*
	 * Removes m0_rpc_bulk_buf from the m0_rpc_bulk::rb_buflist and
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
	rbulk = m0_fop_to_rpcbulk(res_fop);
	rc = m0_rpc_bulk_store(rbulk, res_fop->f_item.ri_session->s_conn,
			       rw->crw_desc.id_descs);
	if (rc != 0) {
		IOS_ADDB_FUNCFAIL(rc, IO_FOP_COALESCE_2, &m0_ios_addb_ctx);
		m0_io_fop_destroy(res_fop);
		goto cleanup;
	}

	/*
	 * Checks if current size of res_fop fits into the size
	 * provided as input.
	 */
	if (m0_io_fop_size_get(res_fop) > size) {
		IOS_ADDB_FUNCFAIL(-EMSGSIZE, IO_FOP_COALESCE_3,
				  &m0_ios_addb_ctx);
		m0_mutex_lock(&rbulk->rb_mutex);
		m0_tl_for(rpcbulk, &rbulk->rb_buflist, rbuf) {
			m0_net_buffer_del(rbuf->bb_nbuf, tm);
		} m0_tl_endfor;
		m0_mutex_unlock(&rbulk->rb_mutex);
		m0_io_fop_destroy(res_fop);
		goto cleanup;
	}

	/*
	 * Removes the net buffers belonging to coalesced member fops
	 * from transfer machine since these buffers are coalesced now
	 * and are part of res_fop.
	 */
	m0_tl_for(rpcitem, items_list, item) {
		fop = m0_rpc_item_to_fop(item);
		if (fop == res_fop)
			continue;
		rbulk = m0_fop_to_rpcbulk(fop);
		m0_mutex_lock(&rbulk->rb_mutex);
		m0_tl_for(rpcbulk, &rbulk->rb_buflist, rbuf) {
			m0_net_buffer_del(rbuf->bb_nbuf, tm);
		} m0_tl_endfor;
		m0_mutex_unlock(&rbulk->rb_mutex);
	} m0_tl_endfor;

	/*
	 * Removes the net buffers from transfer machine contained by rpc bulk
	 * structure belonging to res_fop since they will be replaced by
	 * new coalesced net buffers.
	 */
	bbulk = m0_fop_to_rpcbulk(bkp_fop);
	rbulk = m0_fop_to_rpcbulk(res_fop);
	m0_mutex_lock(&bbulk->rb_mutex);
	m0_mutex_lock(&rbulk->rb_mutex);
	m0_tl_for(rpcbulk, &bbulk->rb_buflist, rbuf) {
		rpcbulk_tlist_del(rbuf);
		rpcbulk_tlist_add(&rbulk->rb_buflist, rbuf);
		m0_net_buffer_del(rbuf->bb_nbuf, tm);
		rbulk->rb_bytes -= m0_vec_count(&rbuf->bb_nbuf->
						nb_buffer.ov_vec);
	} m0_tl_endfor;
	m0_mutex_unlock(&rbulk->rb_mutex);
	m0_mutex_unlock(&bbulk->rb_mutex);

	M0_POST(rw->crw_desc.id_nr == rw->crw_ivecs.cis_nr);
	M0_LOG(M0_DEBUG, "io fops coalesced successfully.");
	rpcitem_tlist_add(items_list, &bkp_fop->f_item);
	return rc;
cleanup:
	M0_ASSERT(rc != 0);
	m0_tl_for(iosegset, &aggr_set.iss_list, ioseg) {
		ioseg_unlink_free(ioseg);
	} m0_tl_endfor;
	iosegset_tlist_fini(&aggr_set.iss_list);
	io_fop_bulkbuf_move(bkp_fop, res_fop);
	m0_io_fop_fini(cfop);
	m0_free(cfop);
	return rc;
}

static struct m0_fid *io_fop_fid_get(struct m0_fop *fop)
{
	return &(io_rw_get(fop))->crw_fid;
}

static bool io_fop_fid_equal(struct m0_fop *fop1, struct m0_fop *fop2)
{
        return m0_fid_eq(io_fop_fid_get(fop1), io_fop_fid_get(fop2));
}

static void io_fop_replied(struct m0_fop *fop, struct m0_fop *bkpfop)
{
	struct m0_io_fop     *cfop;
	struct m0_rpc_bulk   *rbulk;
	struct m0_fop_cob_rw *srw;
	struct m0_fop_cob_rw *drw;

	M0_PRE(fop != NULL);
	M0_PRE(bkpfop != NULL);
	M0_PRE(m0_is_io_fop(fop));
	M0_PRE(m0_is_io_fop(bkpfop));

	rbulk = m0_fop_to_rpcbulk(fop);
	m0_mutex_lock(&rbulk->rb_mutex);
	M0_ASSERT(rpcbulk_tlist_is_empty(&rbulk->rb_buflist));
	m0_mutex_unlock(&rbulk->rb_mutex);

	srw = io_rw_get(bkpfop);
	drw = io_rw_get(fop);
	drw->crw_desc = srw->crw_desc;
	drw->crw_ivecs = srw->crw_ivecs;
	cfop = container_of(bkpfop, struct m0_io_fop, if_fop);
	m0_io_fop_fini(cfop);
	m0_free(cfop);
}

static void io_fop_desc_get(struct m0_fop *fop, struct m0_net_buf_desc **desc)
{
	struct m0_fop_cob_rw *rw;

	M0_PRE(fop != NULL);
	M0_PRE(desc != NULL);

	rw = io_rw_get(fop);
	*desc = rw->crw_desc.id_descs;
}

/* Rpc item ops for IO operations. */
static void io_item_replied(struct m0_rpc_item *item)
{
	struct m0_fop		   *fop;
	struct m0_fop		   *rfop;
	/* struct m0_fop           *bkpfop; */
	/* struct m0_rpc_item	   *ritem;  */
	struct m0_rpc_bulk	   *rbulk;
	struct m0_fop_cob_rw_reply *reply;

	M0_PRE(item != NULL);

	if (item->ri_error != 0) {
		IOS_ADDB_FUNCFAIL(item->ri_error, IO_ITEM_REPLIED,
				  &m0_ios_addb_ctx);
		return;
	}
	fop = m0_rpc_item_to_fop(item);
	rbulk = m0_fop_to_rpcbulk(fop);
	rfop = m0_rpc_item_to_fop(item->ri_reply);
	reply = io_rw_rep_get(rfop);

	M0_ASSERT(ergo(reply->rwr_rc == 0,
		       reply->rwr_count == rbulk->rb_bytes));

#if 0
	/** @todo Rearrange IO item merging code to work with new
		  formation code.
	 */
	/*
	 * Restores the contents of master coalesced fop from the first
	 * rpc item in m0_rpc_item::ri_compound_items list. This item
	 * is inserted by io coalescing code.
	 */
	if (!rpcitem_tlist_is_empty(&item->ri_compound_items)) {
		M0_LOG(M0_DEBUG, "Reply received for coalesced io fops.");
		ritem = rpcitem_tlist_head(&item->ri_compound_items);
		rpcitem_tlist_del(ritem);
		bkpfop = m0_rpc_item_to_fop(ritem);
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
	m0_tl_for(rpcitem, &item->ri_compound_items, ritem) {
		fop = m0_rpc_item_to_fop(ritem);
		rbulk = m0_fop_to_rpcbulk(fop);
		m0_mutex_lock(&rbulk->rb_mutex);
		M0_ASSERT(rbulk != NULL && m0_tlist_is_empty(&rpcbulk_tl,
			  &rbulk->rb_buflist));
		/* Notifies all member coalesced items of completion status. */
		rbulk->rb_rc = item->ri_error;
		m0_mutex_unlock(&rbulk->rb_mutex);
		/* XXX Use rpc_item_replied()
		       But we'll fix it later because this code path will need
		       significant changes because of new formation code.
		 */
		/* m0_chan_broadcast(&ritem->ri_chan); */
	} m0_tl_endfor;
#endif
}

static void item_io_coalesce(struct m0_rpc_item *head, struct m0_list *list,
			     uint64_t size)
{
	int			 rc;
	struct m0_fop		*bfop;
	struct m0_fop		*ufop;
	struct m0_rpc_item	*item;
	struct m0_rpc_item	*item_next;

	M0_PRE(head != NULL);
	M0_PRE(list != NULL);
	M0_PRE(size > 0);

	if (m0_list_is_empty(list))
		return;

	/*
	 * Traverses through the list and finds out items that match with
	 * head on basis of fid and intent (read/write). Matching items
	 * are removed from session->s_unbound_items list and added to
	 * head->compound_items list.
	 */
	bfop = m0_rpc_item_to_fop(head);
	m0_list_for_each_entry_safe(list, item, item_next, struct m0_rpc_item,
				    ri_unbound_link) {
		ufop = m0_rpc_item_to_fop(item);
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
		m0_tl_for(rpcitem, &head->ri_compound_items, item) {
			IOS_ADDB_FUNCFAIL(rc, ITEM_IO_COALESCE,
					  &m0_ios_addb_ctx);
			rpcitem_tlist_del(item);
		} m0_tl_endfor;
	} else {
		/*
		 * Item at head is the backup item which is not present
		 * in sessions unbound list.
		 */
		item_next = rpcitem_tlist_head(&head->ri_compound_items);
		rpcitem_tlist_del(head);
		m0_tl_for (rpcitem, &head->ri_compound_items, item) {
			if (item != item_next)
				m0_list_del(&item->ri_unbound_link);
		} m0_tl_endfor;
	}
}

M0_INTERNAL m0_bcount_t m0_io_fop_byte_count(struct m0_io_fop *iofop)
{
	m0_bcount_t             count = 0;
	struct m0_rpc_bulk_buf *rbuf;

	M0_PRE(iofop != NULL);

	m0_tl_for (rpcbulk, &iofop->if_rbulk.rb_buflist, rbuf) {
		count += m0_vec_count(&rbuf->bb_zerovec.z_bvec.ov_vec);
	} m0_tl_endfor;

	return count;
}

M0_INTERNAL void m0_io_fop_release(struct m0_ref *ref)
{
        struct m0_io_fop *iofop;
        struct m0_fop    *fop;

        fop   = container_of(ref, struct m0_fop, f_ref);
        iofop = container_of(fop, struct m0_io_fop, if_fop);
        m0_io_fop_fini(iofop);
        m0_free(iofop);
}

#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
