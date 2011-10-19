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
   @addtogroup io_fops
   In-memory definition of generic io fop and generic io segment.
 */
struct page;
struct c2_io_ioseg;

/**
 * The opcode from which IO service FOPS start.
 */
enum c2_io_service_opcodes {
	C2_IOSERVICE_READV_OPCODE = 15,
	C2_IOSERVICE_WRITEV_OPCODE = 16,
	C2_IOSERVICE_READV_REP_OPCODE = 17,
	C2_IOSERVICE_WRITEV_REP_OPCODE = 18,
};

/**
   Returns the number of fops registered by ioservice.
 */
int c2_ioservice_fops_nr(void);

/**
   Init and fini of ioservice fops code.
 */
int c2_ioservice_fop_init(void);
void c2_ioservice_fop_fini(void);

/**
   This data structure is used to associate an io fop with its
   rpc bulk data. It abstracts the c2_net_buffer and net layer APIs.
   Client side implementations use this structure to represent
   io fops and the associated rpc bulk structures.

   @todo Not complete yet. Need to build ops around c2_io_fop.
 */
struct c2_io_fop {
	/** Inline fop for a generic IO fop. */
	struct c2_fop		if_fop;
	/** Bulk structure containing zero vector for io fop. */
	struct c2_rpc_bulk	if_bulk;
};

/**
   Generic io segment that represents a contiguous stream of bytes
   along with io extent. This structure is typically used by io coalescing
   code from ioservice.
 */
struct io_zeroseg {
	/* Offset of target object to start io from. */
	c2_bindex_t		 is_off;
	/* Number of bytes in io segment. */
	c2_bcount_t		 is_count;
	/* Starting address of buffer. */
	void			*is_buf;
	/* Linkage to have such zero segments in a list. */
	struct c2_list_link	 is_linkage;
};

/**
   Allocate a zero segment.
   @retval Valid io_zeroseg object if success, NULL otherwise.
 */
struct io_zeroseg *io_zeroseg_alloc(void);

/**
   Deallocate a zero segment.
   @param zseg - Zero segment to be deallocated.
 */
void io_zeroseg_free(struct io_zeroseg *zseg);

/**
   Get the io segment indexed by index in array of io segments in zerovec.
   @note The incoming c2_0vec should be allocated and initialized.

   @param zvec The c2_0vec io vector from which io segment will be retrieved.
   @param index Index of io segments in array of io segments from zerovec.
   @param seg Out parameter to return io segment.
 */
void io_zerovec_seg_get(const struct c2_0vec *zvec, uint32_t index,
			struct io_zeroseg *seg);

/**
   Set the io segment referred by index into array of io segments from
   the zero vector.
   @note There is no data copy here. Just buffer pointers are copied since
   this API is supposed to be used in same address space.

   @note The incoming c2_0vec should be allocated and initialized.
   @param zvec The c2_0vec io vector whose io segment will be changed.
   @param seg Target segment for set.
 */
void io_zerovec_seg_set(struct c2_0vec *zvec, uint32_t index,
			const struct io_zeroseg *seg);

/**
   Allocate the io segments for the given c2_0vec structure.
   @note The incoming c2_0vec should be allocated and initialized.
   @param zvec The c2_0vec structure.
   @param segs_nr Number of io segments to be allocated.
 */
int io_zerovec_segs_alloc(struct c2_0vec *zvec, uint32_t segs_nr);

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

/** @} end of io_fops group */

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
