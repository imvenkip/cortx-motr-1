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
 * Original author: Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 03/21/2011
 */
#ifndef __COLIBRI_IOSERVICE_IO_FOPS_H__
#define __COLIBRI_IOSERVICE_IO_FOPS_H__

#include "fop/fop.h"
#include "fop/fop_format.h"
#include "lib/memory.h"

#ifdef __KERNEL__
#include "io_fops_k.h"
#else
#include "io_fops_u.h"
#endif

struct c2_fom;
struct c2_fom_type;

/**
 * The opcode from which IO service FOPS start.
 */
enum c2_io_service_opcodes {
	C2_IO_SERVICE_READV_OPCODE = 15,
	C2_IO_SERVICE_WRITEV_OPCODE,
	C2_IO_SERVICE_CREATE_OPCODE,
	C2_IO_SERVICE_CREATE_REP_OPCODE,
	C2_IO_SERVICE_WRITEV_REP_OPCODE,
	C2_IO_SERVICE_READV_REP_OPCODE,
};

/**
   A wrapper structure to have a list of fops
   participating in IO coalescing.
 */
struct c2_io_fop_member {
	/** Linkage to the list of fops. */
	struct c2_list_link	 fop_linkage;
	/** Actual fop object. */
	struct c2_fop		*fop;
};

/**
   Member structure of a list containing read IO segments.
 */
struct c2_io_read_segment {
        /** Linkage to the list of such structures. */
        struct c2_list_link             rs_linkage;
        /** The read IO segment. */
        struct c2_fop_segment           rs_seg;
};

/**
   Member structure of a list containing write IO segments.
 */
struct c2_io_write_segment {
        /** Linkage to the list of such structures. */
        struct c2_list_link             ws_linkage;
        /** The write IO segment. */
        struct c2_fop_io_seg            ws_seg;
};

/**
   A IO vector pointer union to keep track of original IO vector of a
   resultant IO rpc item happened due to IO coalescing. Once the reply
   of such operations comes back, original IO vector is restored in the
   resultant fop.
 */
union c2_io_iovec {
	/** IO vector for write request and read reply operation. */
	struct c2_fop_io_vec		*write_vec;
	/** IO vector for read request operation. */
	struct c2_fop_segment_seq	*read_vec;
};

/**
   A generic IO segment pointing either to read or write segments. This
   is needed to have generic IO coalescing code.
 */
union c2_io_ioseg {
	/** IO segment for read request fop. */
	struct c2_fop_segment	*read_seg;
	/** IO segment for write request OR read reply fop. */
	struct c2_fop_io_seg	*ioseg;
};

/**
 * Bunch of externs needed for stob/ut/io_fop_init.c code.
 */
extern const struct c2_fop_type_ops c2_io_cob_readv_ops;
extern const struct c2_fop_type_ops c2_io_cob_writev_ops;
extern const struct c2_fop_type_ops c2_io_rwv_rep_ops;

/**
 * FOP definitions and corresponding fop type formats
 * exported by ioservice.
 */
extern struct c2_fop_type_format c2_fop_cob_writev_tfmt;
extern struct c2_fop_type_format c2_fop_cob_readv_tfmt;
extern struct c2_fop_type_format c2_fop_cob_writev_rep_tfmt;
extern struct c2_fop_type_format c2_fop_cob_readv_rep_tfmt;
extern struct c2_fop_type_format c2_fop_file_create_tfmt;
extern struct c2_fop_type_format c2_fop_file_create_rep_tfmt;

extern struct c2_fop_type c2_fop_cob_readv_fopt;
extern struct c2_fop_type c2_fop_cob_writev_fopt;
extern struct c2_fop_type c2_fop_cob_readv_rep_fopt;
extern struct c2_fop_type c2_fop_cob_writev_rep_fopt;
extern struct c2_fop_type c2_fop_file_create_fopt;
extern struct c2_fop_type c2_fop_file_create_rep_fopt;

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

