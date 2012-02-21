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

/**
   @page bulkclient-fspec Functional Specification for fop bulk client.
   <i>Mandatory. This page describes the external interfaces of the
   component. The section has mandatory sub-divisions created using the Doxygen
   @@section command.  It is required that there be Table of Contents at the
   top of the page that illustrates the sectioning of the page.</i>

   - @ref bulkclient-fspec-ds
   - @ref bulkclient-fspec-sub
   - @ref bulkclient-fspec-cli
   - @ref bulkclient-fspec-usecases
   - @ref bulkclientDFS "Bulk IO client Detailed Functional Specification"

   @section bulkclient-fspec-ds Data Structures
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and <i>brief</i> description of the
   major externally visible data structures defined by this component.  No
   details of the data structure are required here, just the salient
   points.</i>

   The io bulk client design includes data structures like
   - c2_io_fop An in-memory definition of io fop which binds the io fop
   with its network buffer.

   @section bulkclient-fspec-sub Subroutines
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and brief description of the
   externally visible programming interfaces.</i>

   @subsection bulkclient-fspec-sub-cons Constructors and Destructors

   - c2_io_fop_init() - Initializes the c2_io_fop structure.

   - c2_io_fop_fini() - Finalizes a c2_io_fop structure.

   @subsection bulkclient-fspec-sub-acc Accessors and Invariants

   - c2_fop_to_rpcbulk() - Retrieves struct c2_rpc_bulk from given c2_fop.

   @subsection bulkclient-fspec-sub-opi Operational Interfaces

   @section bulkclient-fspec-cli Command Usage
   <i>Mandatory for command line programs.  Components that provide programs
   would provide a specification of the command line invocation arguments.  In
   addition, the format of any any structured file consumed or produced by the
   interface must be described in this section.</i>
   Not Applicable.

   @section bulkclient-fspec-usecases Recipes
   <i>This section could briefly explain what sequence of interface calls or
   what program invocation flags are required to solve specific usage
   scenarios.  It would be very nice if these examples can be linked
   back to the HLD for the component.</i>

   Using bulk APIs on client side.
   - IO bulk client allocates memory for a c2_io_fop and invokes
   c2_io_fop_init() by providing fop type.
   - IO bulk client client invokes c2_rpc_bulk_buf_page_add() or
   c2_rpc_bulk_buf_usrbuf_add()  till all pages or user buffers are added
   to c2_rpc_bulk structure and then invokes c2_rpc_post() to
   submit the fop to rpc layer.
   - Bulk client invokes c2_rpc_bulk_store() to store the network buffer
   memory descriptor/s to io fop wire format. The network buffer memory
   descriptor is retrieved after adding the network buffer to transfer
   machine belonging to c2_rpcmachine.
   - The network buffers added by bulk client to c2_rpc_bulk structure
   are removed and deallocated by network buffer completion callbacks.
   Bulk client user need not remove or deallocate these network buffers
   by itself.

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
   The c2_io_fop structures can be populated and used like this.
   @see c2_rpc_bulk().

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
   c2_clink_add(rbulk->rb_chan, clink);
   c2_rpc_post(rpc_item);
   c2_chan_wait(clink);
   c2_io_fop_fini(iofop);

   @endcode
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
extern struct c2_fop_type_format c2_fop_io_buf_tfmt;
extern struct c2_fop_type_format c2_fop_io_seg_tfmt;
extern struct c2_fop_type_format c2_fop_io_vec_tfmt;
extern struct c2_fop_type_format c2_fop_cob_rw_tfmt;
extern struct c2_fop_type_format c2_fop_cob_rw_reply_tfmt;

extern struct c2_fop_type c2_fop_cob_readv_fopt;
extern struct c2_fop_type c2_fop_cob_writev_fopt;
extern struct c2_fop_type c2_fop_cob_readv_rep_fopt;
extern struct c2_fop_type c2_fop_cob_writev_rep_fopt;

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
