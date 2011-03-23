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

extern struct c2_fop_type_format c2_fop_file_fid_tfmt;
extern struct c2_fop_type_format c2_fop_io_addr_tfmt;
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
