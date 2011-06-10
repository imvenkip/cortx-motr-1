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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 05/24/2010
 */

#ifndef _IO_FOP_H_
#define _IO_FOP_H_

#include "fop/fop.h"

extern struct c2_fop_type c2_io_write_fopt;
extern struct c2_fop_type c2_io_read_fopt;
extern struct c2_fop_type c2_io_create_fopt;
extern struct c2_fop_type c2_io_quit_fopt;
extern struct c2_fop_type c2_io_write_rep_fopt;
extern struct c2_fop_type c2_io_read_rep_fopt;
extern struct c2_fop_type c2_io_create_rep_fopt;
//extern struct c2_fop_type c2_fop_cob_readv_fopt;
//extern struct c2_fop_type c2_fop_cob_writev_fopt;

extern struct c2_fop_type_format c2_fop_file_fid_tfmt;
extern struct c2_fop_type_format c2_fop_io_buf_tfmt;
extern struct c2_fop_type_format c2_fop_io_seg_tfmt;
extern struct c2_fop_type_format c2_fop_io_vec_tfmt;
extern struct c2_fop_type_format c2_fop_cob_readv_tfmt;
extern struct c2_fop_type_format c2_fop_cob_writev_tfmt;
extern struct c2_fop_type_format c2_fop_segment_tfmt;
extern struct c2_fop_type_format c2_fop_cob_writev_rep_tfmt;
extern struct c2_fop_type_format c2_fop_cob_readv_rep_tfmt;

int io_fop_init(void);
void io_fop_fini(void);

#endif /* !_IO_FOP_H_ */
/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
