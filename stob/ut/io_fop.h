#ifndef _IO_FOP_H_
#define _IO_FOP_H_

#include <rpc/rpc.h>

enum {
	PORT = 10001
};


enum c2_stob_io_fop_opcode {
	SIF_READ  = 0x4001,
	SIF_WRITE = 0x4002,
	SIF_CREAT = 0x4003
};

struct c2_fid {
	u_quad_t f_d1;
	u_quad_t f_d2;
};

struct c2_stob_io_seg {
	u_quad_t f_offset;
	u_int    f_count;
};

struct c2_stob_io_buf {
	u_int ib_count;
	char *ib_value;
};

struct c2_stob_io_write_fop {
	struct c2_fid siw_object;
	struct {
		u_int                  v_count;
		struct c2_stob_io_seg *v_seg;
	}             siw_vec;
	struct {
		u_int                  b_count;
		struct c2_stob_io_buf *b_buf;
	}             siw_buf;
};

struct c2_stob_io_write_rep_fop {
	u_int siwr_rc;
	u_int siwr_count;
};

struct c2_stob_io_read_fop {
	struct c2_fid sir_object;
	struct {
		u_int                  v_count;
		struct c2_stob_io_seg *v_seg;
	}             sir_vec;
};

struct c2_stob_io_read_rep_fop {
	u_int sirr_rc;
	u_int sirr_count;
	struct {
		u_int                  b_count;
		struct c2_stob_io_buf *b_buf;
	} sirr_buf;
};

struct c2_stob_io_create_fop {
	struct c2_fid sic_object;
};

struct c2_stob_io_create_rep_fop {
	u_int sicr_rc;
};

bool_t xdr_c2_stob_io_write_fop(XDR *xdrs, struct c2_stob_io_write_fop *w);
bool_t xdr_c2_stob_io_write_rep_fop(XDR *xdrs, 
				    struct c2_stob_io_write_rep_fop *w);
bool_t xdr_c2_stob_io_read_fop(XDR *xdrs, struct c2_stob_io_read_fop *r);
bool_t xdr_c2_stob_io_read_rep_fop(XDR *xdrs, 
				   struct c2_stob_io_read_rep_fop *r);
bool_t xdr_c2_stob_io_create_fop(XDR *xdr, struct c2_stob_io_create_fop *fop);
bool_t xdr_c2_stob_io_create_rep_fop(XDR *xdr, 
				     struct c2_stob_io_create_rep_fop *fop);

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
