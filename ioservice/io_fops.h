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
 * Original author: Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 03/21/2011
 */
#ifndef __COLIBRI_IOSERVICE_IO_FOPS_H__
#define __COLIBRI_IOSERVICE_IO_FOPS_H__

#include "fop/fop_base.h"
#include "fop/fop_format.h"
#include "lib/list.h"
#include "fop/fop.h"
#include "rpc/rpc2.h"

/**
   @page bulkclient-fspec Functional Specification for fop bulk client.

   - @ref bulkclient-fspec-ds
   - @ref bulkclient-fspec-sub
   - @ref bulkclient-fspec-cli
   - @ref bulkclient-fspec-usecases
   - @ref bulkclientDFS "Bulk IO client Detailed Functional Specification"

   @section bulkclient-fspec-ds Data Structures

   The io bulk client design includes data structures like
   - c2_io_fop An in-memory definition of io fop which binds the io fop
   with its network buffer.

   @section bulkclient-fspec-sub Subroutines

   @subsection bulkclient-fspec-sub-cons Constructors and Destructors

   - c2_io_fop_init() - Initializes the c2_io_fop structure.

   - c2_io_fop_fini() - Finalizes a c2_io_fop structure.

   @subsection bulkclient-fspec-sub-acc Accessors and Invariants

   - c2_fop_to_rpcbulk() - Retrieves struct c2_rpc_bulk from given c2_fop.

   @subsection bulkclient-fspec-sub-opi Operational Interfaces

   @section bulkclient-fspec-cli Command Usage
   Not Applicable.

   @section bulkclient-fspec-usecases Recipes

   Using bulk APIs on client side.
   - IO bulk client allocates memory for a c2_io_fop and invokes
   c2_io_fop_init() by providing fop type.
   - IO bulk client invokes c2_rpc_bulk_buf_databuf_add() till all pages
   or user buffers are added to c2_rpc_bulk structure and then invokes
   - Bulk client invokes c2_rpc_bulk_store() to store the network buffer
   memory descriptor/s to io fop wire format. The network buffer memory
   descriptor is retrieved after adding the network buffer to transfer
   machine belonging to c2_rpc_machine.
   - Bulk client invokes c2_rpc_post() to submit the fop to rpc layer.
   - The network buffers added by bulk client to c2_rpc_bulk structure
   are removed and deallocated by network buffer completion callbacks.
   Bulk client user need not remove or deallocate these network buffers
   by itself.
   The c2_io_fop structure can be populated and used like this.

   @code

   c2_io_fop_init(iofop, ftype);
   do {
	c2_rpc_bulk_buf_add(&iofop->if_rbulk, rbuf);
	..
	c2_rpc_bulk_buf_databuf_add(rbuf, buf, count, index);
	..
   } while (not_empty);
   ..
   c2_rpc_bulk_buf_store(rbuf, rpcitem, net_buf_desc);
   ..
   c2_rpc_post(rpc_item);
   c2_rpc_reply_timedwait(rpc_item);
   c2_io_fop_fini(iofop);

   @endcode

   Using bulk APIs on server side.
   - Colibri io server program (ioservice) creates a c2_rpc_bulk structure
   and invokes c2_rpc_bulk_init().
   - Ioservice invokes c2_rpc_bulk_buf_add() to attach buffers to the
   c2_rpc_bulk structure.
   - Typically, ioservice uses a pre-allocated and pre-registered pool of
   network buffers which are supposed to be used while doing bulk transfer.
   These buffers are provided while invoking c2_rpc_bulk_buf_add() API.
   - Ioservice invokes c2_rpc_bulk_load() to start the zero copy of data from
   sender.
   Since server side sends the reply fop, it does not need c2_io_fop
   structures since it deals with request IO fops.
   Ioservice typically works with a pre-allocated, pre-registered pool
   of network buffers. Buffers are requested as per need from the pool
   and passed to c2_rpc_bulk_buf_add() call as shown below.

   @see c2_rpc_bulk
   @code

   c2_rpc_bulk_init(rbulk);
   do {
	   c2_rpc_bulk_buf_add(rbulk->rbuf, segs_nr, netdom, nb, &out);
	   ..
	   ..
   } while(request_io_fop->io_buf_desc_list is not finished);

   c2_clink_init(&clink, NULL);
   c2_clink_add(&rbulk->rb_chan, &clink);
   c2_rpc_bulk_buf_load(rbulk, conn, &request_io_fop->desc_list);
   ..
   c2_chan_wait(&clink);
   ..
   send_reply_fop();
   c2_rpc_bulk_fini(rbulk);

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
   A magic constant to check sanity of struct c2_io_fop.
 */
enum {
	C2_IO_FOP_MAGIC = 0x34832752309bdfeaULL,
};

/**
   This data structure is used to associate an io fop with its
   rpc bulk data. It abstracts the c2_net_buffer and net layer APIs.
   Client side implementations use this structure to represent
   io fops and the associated rpc bulk structures.
   @see c2_rpc_bulk().
 */
struct c2_io_fop {
	/** Inline fop for a generic IO fop. */
	struct c2_fop		if_fop;
	/** Rpc bulk structure containing zero vector for io fop. */
	struct c2_rpc_bulk	if_rbulk;
	/** Magic constant for IO fop. */
	uint64_t		if_magic;
};

/**
   Initializes a c2_io_fop structure.
   @param ftype Type of fop to be initialized.
   @pre iofop != NULL.
   @post io_fop_invariant(iofop)
 */
int c2_io_fop_init(struct c2_io_fop *iofop, struct c2_fop_type *ftype);

/**
   Finalizes a c2_io_fop structure.
   @pre iofop != NULL.
 */
void c2_io_fop_fini(struct c2_io_fop *iofop);

/**
   Retrieves a c2_rpc_bulk structure from given c2_fop.
   @pre fop != NULL.
 */
struct c2_rpc_bulk *c2_fop_to_rpcbulk(const struct c2_fop *fop);

/**
   Allocates memory for net buf descriptors array and index vector array
   and populate the array of index vectors.
   @pre fop != NULL.
 */
int c2_io_fop_prepare(struct c2_fop *fop);

/**
   Deallocates memory for sequence of net buf desc and sequence of index
   vector from io fop wire format.
 */
void c2_io_fop_destroy(struct c2_fop *fop);

bool c2_is_read_fop(const struct c2_fop *fop);
bool c2_is_write_fop(const struct c2_fop *fop);
bool c2_is_io_fop(const struct c2_fop *fop);
struct c2_fop_cob_rw *io_rw_get(struct c2_fop *fop);
struct c2_fop_cob_rw_reply *io_rw_rep_get(struct c2_fop *fop);
bool c2_is_cob_create_fop(const struct c2_fop *fop);
bool c2_is_cob_delete_fop(const struct c2_fop *fop);
bool c2_is_cob_create_delete_fop(const struct c2_fop *fop);
struct c2_fop_cob_common *c2_cobfop_common_get(struct c2_fop *fop);

/**
   @} bulkclientDFS end group
*/

/**
   In-memory definition of generic io fop and generic io segment.
 */
struct page;
struct c2_io_ioseg;

/**
   Init and fini of ioservice fops code.
 */
int c2_ioservice_fop_init(void);
void c2_ioservice_fop_fini(void);

/**
 * FOP definitions and corresponding fop type formats
 * exported by ioservice.
 */
extern struct c2_fop_type_format c2_fop_cob_writev_tfmt;
extern struct c2_fop_type_format c2_fop_cob_readv_tfmt;
extern struct c2_fop_type_format c2_fop_cob_writev_rep_tfmt;
extern struct c2_fop_type_format c2_fop_cob_readv_rep_tfmt;
extern struct c2_fop_type_format c2_fop_file_fid_tfmt;
extern struct c2_fop_type_format c2_fop_cob_rw_tfmt;
extern struct c2_fop_type_format c2_fop_cob_rw_reply_tfmt;

extern struct c2_fop_type c2_fop_cob_readv_fopt;
extern struct c2_fop_type c2_fop_cob_writev_fopt;
extern struct c2_fop_type c2_fop_cob_readv_rep_fopt;
extern struct c2_fop_type c2_fop_cob_writev_rep_fopt;
extern struct c2_fop_type c2_fop_cob_create_fopt;
extern struct c2_fop_type c2_fop_cob_delete_fopt;
extern struct c2_fop_type c2_fop_cob_op_reply_fopt;
extern const struct c2_fom_type c2_io_fom_cob_rw_mopt;

struct c2_fop_cob_rw *io_rw_get(struct c2_fop *fop);
struct c2_fop_cob_rw_reply *io_rw_rep_get(struct c2_fop *fop);

static inline struct c2_net_transfer_mc *io_fop_tm_get(
		const struct c2_fop *fop)
{
	C2_PRE(fop != NULL);

	return &fop->f_item.ri_session->s_conn->c_rpc_machine->rm_tm;
}


/* __COLIBRI_IOSERVICE_IO_FOPS_H__ */
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
