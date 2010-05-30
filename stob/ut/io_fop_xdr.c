/* -*- C -*- */

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
#if 0
	return xdr_array(xdrs, &buf->ib_value, &buf->ib_count, ~0,
			 sizeof (char), (xdrproc_t) xdr_char);
#endif
        printf("value = %p, count = %d\n", buf->ib_value, buf->ib_count);
        memset(buf->ib_value, 0, buf->ib_count);
        strcpy(buf->ib_value, "helloworodl\n\0");
        return xdr_bytes(xdrs, &buf->ib_value, &buf->ib_count, ~0);
}

bool_t xdr_c2_stob_io_write_fop(XDR *xdrs, struct c2_stob_io_write_fop *w)
{
        int ret = 0;

	ret = xdr_c2_fid(xdrs, &w->siw_object);
	ret = xdr_array(xdrs, (char **)&w->siw_vec.v_seg, 
			  &w->siw_vec.v_count, ~0,
			  sizeof (struct c2_stob_io_seg), 
			  (xdrproc_t) xdr_c2_stob_io_seg);
	ret = xdr_array(xdrs, (char **)&w->siw_buf.b_buf, 
			  &w->siw_buf.b_count, ~0,
			  sizeof (struct c2_stob_io_buf), 
			  (xdrproc_t) xdr_c2_stob_io_buf);
        return ret;
}

bool_t xdr_c2_stob_io_write_rep_fop(XDR *xdrs, 
				    struct c2_stob_io_write_rep_fop *w)
{
	return xdr_u_int(xdrs, &w->siwr_rc) && xdr_u_int(xdrs, &w->siwr_count);
}

bool_t xdr_c2_stob_io_read_fop(XDR *xdrs, struct c2_stob_io_read_fop *r)
{
        u_quad_t offset;
        unsigned int count;
        bool_t ret;
	ret =	xdr_c2_fid(xdrs, &r->sir_object) &&
#if 0
		xdr_array(xdrs, (char **)&r->sir_vec.v_seg, 
			  &r->sir_vec.v_count, ~0,
			  sizeof (struct c2_stob_io_seg), 
			  (xdrproc_t) xdr_c2_stob_io_seg);
#else
                xdr_u_int(xdrs, &r->sir_vec.v_count) &&
                xdr_u_longlong_t(xdrs, &offset) &&
                xdr_u_int(xdrs, &count);
        r->sir_vec.v_seg = malloc(sizeof(struct c2_stob_io_seg));
        r->sir_vec.v_seg->f_offset = offset;
        r->sir_vec.v_seg->f_count = count;
#endif
        printf("ret = %d\n", ret);
        printf("decode read fop, %d, %p\n", r->sir_vec.v_count, r->sir_vec.v_seg);
        if (r->sir_vec.v_seg)
                printf("xxx %ld/%d\n",
                        (long int)r->sir_vec.v_seg->f_offset,
                        (int)r->sir_vec.v_seg->f_count);
        return ret;
}

bool_t xdr_c2_stob_io_read_rep_fop(XDR *xdrs, struct c2_stob_io_read_rep_fop *r)
{
        printf("read rc %d count %d\n", r->sirr_rc, r->sirr_count);
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
