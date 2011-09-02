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

struct c2_fop_io_seg;
struct c2_fop_segment;
struct c2_fop_segment_seq;

/**
 * The opcode from which IO service FOPS start.
 */
enum c2_io_service_opcodes {
	C2_IO_SERVICE_READV_OPCODE = 15,
	C2_IO_SERVICE_WRITEV_OPCODE = 16,
	C2_IO_SERVICE_READV_REP_OPCODE = 17,
	C2_IO_SERVICE_WRITEV_REP_OPCODE = 18,
};

/**
   Return the number of fops registered by ioservice.
 */
int c2_ioservice_fops_nr(void);

/**
   A generic IO segment pointing either to read or write segments. This
   is needed to have generic IO coalescing code.
 */
struct c2_io_ioseg {
	/** IO segment for read or write request fop. */
	struct c2_fop_io_seg	*rw_seg;
        /** Linkage to the list of such structures. */
        struct c2_list_link	 io_linkage;
};

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
