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
 * Original author: Nikita Danilov
 * Original creation date: 05/24/2010
 */

#include "io_fop.h"

static bool_t xdr_c2_fid(XDR *xdrs, struct c2_fid *fid)
{
	 return 
		 xdr_u_longlong_t(xdrs, &fid->f_d1) &&
		 xdr_u_longlong_t(xdrs, &fid->f_d2);
}

static bool_t xdr_c2_stob_io_seg(XDR *xdrs, struct c2_stob_io_seg *seg)
{
	return
		xdr_u_longlong_t(xdrs, &seg->f_offset) &&
		xdr_u_int(xdrs, &seg->f_count);
}

static bool_t xdr_c2_stob_io_buf(XDR *xdrs, struct c2_stob_io_buf *buf)
{
        return xdr_bytes(xdrs, &buf->ib_value, &buf->ib_count, ~0);
}

bool_t xdr_c2_stob_io_write_fop(XDR *xdrs, struct c2_stob_io_write_fop *w)
{
	return
		xdr_c2_fid(xdrs, &w->siw_object) &&
		xdr_array(xdrs, (char **)&w->siw_vec.v_seg, 
			  &w->siw_vec.v_count, ~0,
			  sizeof (struct c2_stob_io_seg), 
			  (xdrproc_t) xdr_c2_stob_io_seg) &&
		xdr_array(xdrs, (char **)&w->siw_buf.b_buf, 
			  &w->siw_buf.b_count, ~0,
			  sizeof (struct c2_stob_io_buf), 
			  (xdrproc_t) xdr_c2_stob_io_buf);
}

bool_t xdr_c2_stob_io_write_rep_fop(XDR *xdrs, 
				    struct c2_stob_io_write_rep_fop *w)
{
	return xdr_u_int(xdrs, &w->siwr_rc) && xdr_u_int(xdrs, &w->siwr_count);
}

bool_t xdr_c2_stob_io_read_fop(XDR *xdrs, struct c2_stob_io_read_fop *r)
{
	return xdr_c2_fid(xdrs, &r->sir_object) &&
		xdr_array(xdrs, (char **)&r->sir_vec.v_seg, 
			  &r->sir_vec.v_count, ~0,
			  sizeof (struct c2_stob_io_seg), 
			  (xdrproc_t) xdr_c2_stob_io_seg);
}

bool_t xdr_c2_stob_io_read_rep_fop(XDR *xdrs, struct c2_stob_io_read_rep_fop *r)
{
	return
		xdr_u_int(xdrs, &r->sirr_rc) &&
		xdr_u_int(xdrs, &r->sirr_count) &&
		xdr_array(xdrs, (char **)&r->sirr_buf.b_buf, 
			  &r->sirr_buf.b_count, ~0,
			  sizeof (struct c2_stob_io_buf), 
			  (xdrproc_t) xdr_c2_stob_io_buf);
}

bool_t xdr_c2_stob_io_create_fop(XDR *xdrs, struct c2_stob_io_create_fop *fop)
{
	return xdr_c2_fid(xdrs, &fop->sic_object);
}

bool_t xdr_c2_stob_io_create_rep_fop(XDR *xdrs,
				     struct c2_stob_io_create_rep_fop *fop)
{
	return xdr_u_int(xdrs, &fop->sicr_rc);
}

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
