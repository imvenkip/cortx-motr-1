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

#pragma once

#ifndef __MERO_IOSERVICE_IO_FOPS_H__
#define __MERO_IOSERVICE_IO_FOPS_H__

#include "lib/list.h"
#include "fop/fop.h"
#include "rpc/rpc.h"

/**
   @page bulkclient-fspec Functional Specification for fop bulk client.

   - @ref bulkclient-fspec-ds
   - @ref bulkclient-fspec-sub
   - @ref bulkclient-fspec-cli
   - @ref bulkclient-fspec-usecases
   - @ref bulkclientDFS "Bulk IO client Detailed Functional Specification"

   @section bulkclient-fspec-ds Data Structures

   The io bulk client design includes data structures like
   - m0_io_fop An in-memory definition of io fop which binds the io fop
   with its network buffer.

   @section bulkclient-fspec-sub Subroutines

   @subsection bulkclient-fspec-sub-cons Constructors and Destructors

   - m0_io_fop_init() - Initializes the m0_io_fop structure.

   - m0_io_fop_fini() - Finalizes a m0_io_fop structure.

   @subsection bulkclient-fspec-sub-acc Accessors and Invariants

   - m0_fop_to_rpcbulk() - Retrieves struct m0_rpc_bulk from given m0_fop.

   @subsection bulkclient-fspec-sub-opi Operational Interfaces

   @section bulkclient-fspec-cli Command Usage
   Not Applicable.

   @section bulkclient-fspec-usecases Recipes

   Using bulk APIs on client side.
   - IO bulk client allocates memory for a m0_io_fop and invokes
   m0_io_fop_init() by providing fop type.
   - IO bulk client invokes m0_rpc_bulk_buf_databuf_add() till all pages
   or user buffers are added to m0_rpc_bulk structure and then invokes
   - Bulk client invokes m0_rpc_bulk_store() to store the network buffer
   memory descriptor/s to io fop wire format. The network buffer memory
   descriptor is retrieved after adding the network buffer to transfer
   machine belonging to m0_rpc_machine.
   - Bulk client invokes m0_rpc_post() to submit the fop to rpc layer.
   - The network buffers added by bulk client to m0_rpc_bulk structure
   are removed and deallocated by network buffer completion callbacks.
   Bulk client user need not remove or deallocate these network buffers
   by itself.
   The m0_io_fop structure can be populated and used like this.

   @code

   m0_io_fop_init(iofop, ftype);
   do {
	m0_rpc_bulk_buf_add(&iofop->if_rbulk, rbuf);
	..
	m0_rpc_bulk_buf_databuf_add(rbuf, buf, count, index);
	..
   } while (not_empty);
   ..
   m0_rpc_bulk_buf_store(rbuf, rpcitem, net_buf_desc);
   ..
   m0_rpc_post(rpc_item);
   m0_rpc_reply_timedwait(rpc_item);
   m0_io_fop_fini(iofop);

   @endcode

   Using bulk APIs on server side.
   - Mero io server program (ioservice) creates a m0_rpc_bulk structure
   and invokes m0_rpc_bulk_init().
   - Ioservice invokes m0_rpc_bulk_buf_add() to attach buffers to the
   m0_rpc_bulk structure.
   - Typically, ioservice uses a pre-allocated and pre-registered pool of
   network buffers which are supposed to be used while doing bulk transfer.
   These buffers are provided while invoking m0_rpc_bulk_buf_add() API.
   - Ioservice invokes m0_rpc_bulk_load() to start the zero copy of data from
   sender.
   Since server side sends the reply fop, it does not need m0_io_fop
   structures since it deals with request IO fops.
   Ioservice typically works with a pre-allocated, pre-registered pool
   of network buffers. Buffers are requested as per need from the pool
   and passed to m0_rpc_bulk_buf_add() call as shown below.

   @see m0_rpc_bulk
   @code

   m0_rpc_bulk_init(rbulk);
   do {
	   m0_rpc_bulk_buf_add(rbulk->rbuf, segs_nr, netdom, nb, &out);
	   ..
	   ..
   } while(request_io_fop->io_buf_desc_list is not finished);

   m0_clink_init(&clink, NULL);
   m0_clink_add(&rbulk->rb_chan, &clink);
   m0_rpc_bulk_buf_load(rbulk, conn, &request_io_fop->desc_list);
   ..
   m0_chan_wait(&clink);
   ..
   send_reply_fop();
   m0_rpc_bulk_fini(rbulk);

   @endcode

   @see @ref bulkclientDFS "Bulk IO client Detailed Functional Specification"
 */

/**
   @defgroup bulkclientDFS Detailed Functional Specification for io bulk client.
   @{

   The Detailed Functional Specification can be broken down in 2 major
   subcomponents.

   - @ref bulkclientDFSiofop
   - @ref bulkclientDFSrpcbulk
*/

/**
   @section bulkclientDFSiofop Generic io fop.
 */

/**
   This data structure is used to associate an io fop with its
   rpc bulk data. It abstracts the m0_net_buffer and net layer APIs.
   Client side implementations use this structure to represent
   io fops and the associated rpc bulk structures.
   @see m0_rpc_bulk().
 */
struct m0_io_fop {
	/** Inline fop for a generic IO fop. */
	struct m0_fop		if_fop;
	int                     if_bulk_inited;
	/** Rpc bulk structure containing zero vector for io fop. */
	struct m0_rpc_bulk	if_rbulk;
	/** Magic constant for IO fop. */
	uint64_t		if_magic;
};

/**
   Initializes a m0_io_fop structure.
   @param ftype Type of fop to be initialized.
   @pre iofop != NULL.
   @post io_fop_invariant(iofop)
 */
M0_INTERNAL int m0_io_fop_init(struct m0_io_fop *iofop,
			       struct m0_fop_type *ftype);

/**
   Finalizes a m0_io_fop structure.
   @pre iofop != NULL.
 */
M0_INTERNAL void m0_io_fop_fini(struct m0_io_fop *iofop);

/**
   Retrieves a m0_rpc_bulk structure from given m0_fop.
   @pre fop != NULL.
 */
M0_INTERNAL struct m0_rpc_bulk *m0_fop_to_rpcbulk(const struct m0_fop *fop);

/**
   Allocates memory for net buf descriptors array and index vector array
   and populate the array of index vectors.
   @pre fop != NULL.
 */
M0_INTERNAL int m0_io_fop_prepare(struct m0_fop *fop);

/**
   Deallocates memory for sequence of net buf desc and sequence of index
   vector from io fop wire format.
 */
M0_INTERNAL void m0_io_fop_destroy(struct m0_fop *fop);

M0_INTERNAL bool m0_is_read_fop(const struct m0_fop *fop);
M0_INTERNAL bool m0_is_write_fop(const struct m0_fop *fop);
M0_INTERNAL bool m0_is_io_fop(const struct m0_fop *fop);
M0_INTERNAL struct m0_fop_cob_rw *io_rw_get(struct m0_fop *fop);
M0_INTERNAL struct m0_fop_cob_rw_reply *io_rw_rep_get(struct m0_fop *fop);
M0_INTERNAL bool m0_is_cob_create_fop(const struct m0_fop *fop);
M0_INTERNAL bool m0_is_cob_delete_fop(const struct m0_fop *fop);
M0_INTERNAL bool m0_is_cob_create_delete_fop(const struct m0_fop *fop);
M0_INTERNAL struct m0_fop_cob_common *m0_cobfop_common_get(struct m0_fop *fop);

/**
   @} bulkclientDFS end group
*/

/**
   In-memory definition of generic io fop and generic io segment.
 */
struct page;
struct m0_io_ioseg;

/**
   Init and fini of ioservice fops code.
 */
M0_INTERNAL int m0_ioservice_fop_init(void);
M0_INTERNAL void m0_ioservice_fop_fini(void);

extern struct m0_fop_type m0_fop_cob_readv_fopt;
extern struct m0_fop_type m0_fop_cob_writev_fopt;
extern struct m0_fop_type m0_fop_cob_readv_rep_fopt;
extern struct m0_fop_type m0_fop_cob_writev_rep_fopt;
extern struct m0_fop_type m0_fop_cob_create_fopt;
extern struct m0_fop_type m0_fop_cob_delete_fopt;
extern struct m0_fop_type m0_fop_cob_op_reply_fopt;
extern struct m0_fop_type m0_fop_fv_notification_fopt;

extern struct m0_fom_type m0_io_fom_cob_rw_fomt;

M0_INTERNAL struct m0_fop_cob_rw *io_rw_get(struct m0_fop *fop);
M0_INTERNAL struct m0_fop_cob_rw_reply *io_rw_rep_get(struct m0_fop *fop);

static inline struct m0_net_transfer_mc *io_fop_tm_get(const struct m0_fop *fop)
{
	M0_PRE(fop != NULL);

	return &(item_machine(&fop->f_item)->rm_tm);
}

M0_INTERNAL size_t m0_io_fop_size_get(struct m0_fop *fop);

M0_INTERNAL void m0_io_item_free(struct m0_rpc_item *item);

/* Returns the number of bytes to be read/written. */
M0_INTERNAL m0_bcount_t m0_io_fop_byte_count(struct m0_io_fop *iofop);

/* __MERO_IOSERVICE_IO_FOPS_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
