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

#include "fop/fop.h"
#include "lib/types.h"
#include "rpc/rpc.h"
#include "xcode/xcode_attr.h"
#include "net/net_otw_types.h"
#include "net/net_otw_types_xc.h"
#include "addb/addb.h"
#include "addb/addb_wire_xc.h"
#include "fid/fid.h"
#include "fid/fid_xc.h"

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

   @see m0_rpc_bulk
   @code

   m0_rpc_bulk_init(rbulk);
   do {
	   m0_rpc_bulk_buf_add(rbulk->rbuf, segs_nr, netdom, nb, &out);
	   ..
	   ..
   } while(request_io_fop->io_buf_desc_list is not finished);

   m0_clink_init(&clink, NULL);
   m0_clink_add_lock(&rbulk->rb_chan, &clink);
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
			       struct m0_fop_type *ftype,
			       void (*fop_release)(struct m0_ref *));

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

	return &fop->f_item.ri_rmachine->rm_tm;
}

M0_INTERNAL size_t m0_io_fop_size_get(struct m0_fop *fop);

M0_INTERNAL void m0_io_fop_release(struct m0_ref *ref);

/* Returns the number of bytes to be read/written. */
M0_INTERNAL m0_bcount_t m0_io_fop_byte_count(struct m0_io_fop *iofop);

/**
 * @defgroup io_fops FOPs for Data Operations
 *
 * This component contains the File Operation Packets (FOP) definitions
 * for following operations
 * - readv
 * - writev
 *
 * It describes the FOP formats along with brief description of the flow.
 *
 * Note: As authorization is carried on server, all request FOPs
 * contain uid and gid. For authentication, nid is included in every FOP.
 * This is to serve very primitive authentication for now.
 *
 * @{
 */

/**
 * @section IO FOP Definitions
 */

/**
 * Represents the extent information for an io segment. m0_ioseg typically
 * represents a user space data buffer or a kernel page.
 */
struct m0_ioseg {
	uint64_t ci_index;
	uint64_t ci_count;
} M0_XCA_RECORD;

/**
 * Represents an index vector with {index, count}  tuples for a target
 * device (typically a cob).
 */
struct m0_io_indexvec {
	uint32_t         ci_nr;
	struct m0_ioseg *ci_iosegs;
} M0_XCA_SEQUENCE;

/**
 * Represents sequence of index vector, one per network buffer.
 * As a result of io coalescing, there could be multiple network
 * buffers associated with an io fop. Hence a SEQUENCE of m0_io_indexvec
 * is needed, one per network buffer.
 */
struct m0_io_indexvec_seq {
	uint32_t               cis_nr;
	struct m0_io_indexvec *cis_ivecs;
} M0_XCA_SEQUENCE;

/**
 * Sequence of net buf desc that can be accommodated in single io fop.
 * As a result of io coalescing, there could be multiple network
 * buffers associated with an io fop. Hence a SEQUENCE of m0_net_buf_desc
 * is needed.
 */
struct m0_io_descs {
	uint32_t                id_nr;
	struct m0_net_buf_desc *id_descs;
} M0_XCA_SEQUENCE;

/**
 * Failure vector (pool machine state) version numbers.
 * This is declared as a record, not a sequence (which is more like an
 * array, such that to avoid dynamic memory allocation for every
 * request and reply fop. The size of this struct will be assert to
 * be the same of struct m0_pool_version_numbers.
 */
struct m0_fv_version {
	uint64_t fvv_read;
	uint64_t fvv_write;
} M0_XCA_RECORD;

struct m0_fv_event {
	uint32_t fve_type;
	uint32_t fve_index;
	uint32_t fve_state;
} M0_XCA_RECORD;

/**
 * Failure vector updates.
 * This update is represented as an array of chars. This is the serialized
 * representation of failure vector.
 */
struct m0_fv_updates {
	uint32_t            fvu_count;
	struct m0_fv_event *fvu_events;
} M0_XCA_SEQUENCE;

/**
 * A common sub structure to be referred by read and write reply fops.
 */
struct m0_fop_cob_rw_reply {
        /** Status code of operation. */
	uint32_t             rwr_rc;

	/** Number of bytes read or written. */
	uint64_t             rwr_count;

	/** latest version number */
	struct m0_fv_version rwr_fv_version;

	/**
	 * Failure vector updates. This field is valid  iff rc is some
	 * defined special code.
	 */
	struct m0_fv_updates rwr_fv_updates;
} M0_XCA_RECORD;

/**
 * Reply FOP for a readv request.
 */
struct m0_fop_cob_readv_rep {
	/** Common read/write reply. */
	struct m0_fop_cob_rw_reply c_rep;
} M0_XCA_RECORD;

/**
 * Reply FOP for writev FOPs.
 * The m0_fop_cob_writev_rep FOP is sent as a response by the
 * Data server to a client.
 * It contains the status code and number of bytes written.
 */
struct m0_fop_cob_writev_rep {
	/** Common read/write reply structure. */
	struct m0_fop_cob_rw_reply c_rep;
} M0_XCA_RECORD;

/**
 * Common structure for read and write request fops.
 */
struct m0_fop_cob_rw {
	/** Client known failure vector version number */
	struct m0_fv_version      crw_version;

	/** File identifier of read/write request. */
	struct m0_fid             crw_fid;

	/**
	 * Net buf descriptors representing the m0_net_buffer containing
	 * the IO buffers.
	 */
	struct m0_io_descs        crw_desc;

	/**
	 * Index vectors representing the extent information for the
	 * IO request.
	 */
	struct m0_io_indexvec_seq crw_ivecs;

	/**
	 * ADDB context identifier of the operation
	 * exported by client and imported by server
	 */
	struct m0_addb_uint64_seq crw_addb_ctx_id;

	/** Miscellaneous flags. */
	uint64_t                  crw_flags;
} M0_XCA_RECORD;

/**
 * This fop is representation of a read component object request.
 * The m0_fop_cob_readv FOP is sent by client to the Data server.
 * The FOP supplies the stob id on which the read has to be performed.
 * The IO vector signifies the region within Component Object on which IO
 * has to be performed.
 * On completion, the reply FOP (m0_fop_cob_readv_rep) is created
 * and sent to the client.
 */
struct m0_fop_cob_readv {
	/** Common definition of read/write fops. */
	struct m0_fop_cob_rw c_rwv;
} M0_XCA_RECORD;

/**
 * The m0_fop_cob_writev FOP is used to send write requests by a
 * client to a Data server.
 * The FOP supplies the stob id on which the write has to be performed.
 * The IO vector signifies the region within Component Object on which IO
 * has to be performed.
 * On completion, the reply FOP (m0_fop_cob_writev_rep) is created
 * and sent to the client.
 */
struct m0_fop_cob_writev {
	/** Common definition of read/write fops. */
	struct m0_fop_cob_rw c_rwv;
} M0_XCA_RECORD;

struct m0_test_ios_fop {
	uint64_t               if_st;
	struct m0_net_buf_desc if_nbd;
} M0_XCA_RECORD;

/* @todo : new ADDB interfaces implementation will replace this requirment.
 *struct m0_test_io_addb {
 *	uint64_t              if_st;
 *	struct m0_addb_record if_addb;
 *} M0_XCA_RECORD;
 */

struct m0_fop_cob_common {
	/** Client known failure vector version number */
	struct m0_fv_version c_version;

	/**
	 * Fid of global file.
	 */
	struct m0_fid        c_gobfid;

	/**
	 * Fid of component object.
	 */
	struct m0_fid        c_cobfid;

	/** Unique cob index in pool. */
	uint32_t             c_cob_idx;
} M0_XCA_RECORD;

/**
 * On-wire representation of "cob create" request.
 * Cob create fops are sent to data servers when a new global file
 * is created.
 */
struct m0_fop_cob_create {
	struct m0_fop_cob_common cc_common;
} M0_XCA_RECORD;

/**
 * On-wire representation of "cob delete" request.
 * Cob delete fops are sent to data servers when a global file
 * is deleted.
 * Cob is located based on stob-id. Currently, cob-id is same
 * as stob-id.
 */
struct m0_fop_cob_delete {
	struct m0_fop_cob_common cd_common;
} M0_XCA_RECORD;

/**
 * On-wire representation of reply for both "cob create" and "cob delete"
 * requests.
 */
struct m0_fop_cob_op_reply {
	uint32_t             cor_rc;

	/** latest version number */
	struct m0_fv_version cor_fv_version;

	/**
	 * Failure vector updates. This field is valid  iff rc is some
	 * defined special code.
	 */
	struct m0_fv_updates cor_fv_updates;
} M0_XCA_RECORD;

/**
 * Fop to notify the failure vector version number if changed.
 * This fop itself contains the latest failure vector version number.
 */
struct m0_fop_fv_notification {
	/** the pool id of this failure vector */
	uint64_t             fvn_poolid;

	/** its latest failure vector version */
	struct m0_fv_version fvn_version;
} M0_XCA_RECORD;

struct m0_io_write_rec_part {
	struct m0_fop_cob_writev     wrp_write;
	struct m0_fid		     wrp_fid;
	struct m0_fop_cob_writev_rep wrp_write_rep;
} M0_XCA_RECORD;

struct m0_io_create_rec_part {
	struct m0_fop_cob_create   crp_create;
	struct m0_fop_cob_op_reply crp_create_rep;
} M0_XCA_RECORD;

struct m0_io_delete_rec_part {
	struct m0_fop_cob_delete   drp_delete;
	struct m0_fop_cob_op_reply drp_delete_rep;
} M0_XCA_RECORD;

extern struct m0_fol_rec_part_type m0_io_write_rec_part_type;
extern struct m0_fol_rec_part_type m0_io_create_rec_part_type;
extern struct m0_fol_rec_part_type m0_io_delete_rec_part_type;

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
