#include "net/net.h"
#include <stdio.h>
#include "lib/types.h"
#include "lib/vec.h"
#include "lib/assert.h"
#include "fop/fop.h"
#include "fop/fop_base.h"
#include "lib/errno.h"
#include "rpc/rpc_bufvec.h"


static void data_to_bufvec(struct c2_bufvec *src_buf, void **data,
			 size_t *len)
{
	C2_PRE(src_buf != NULL);
	C2_PRE(len != 0);
	C2_PRE(data != NULL);

	src_buf->ov_vec.v_nr = 1;
	src_buf->ov_vec.v_count = len;
	src_buf->ov_buf = data;
}

static int data_to_bufvec_copy(struct c2_bufvec_cursor *cur, void *data,
			size_t len)
{
	c2_bcount_t	 	 count;
	struct c2_bufvec_cursor  src_cur;
	struct c2_bufvec src_buf;

	C2_PRE(cur != NULL);
	C2_PRE(data != NULL);
	C2_PRE(len != 0);

	data_to_bufvec(&src_buf, &data, &len);
	c2_bufvec_cursor_init(&src_cur, &src_buf);
	count = c2_bufvec_cursor_copy(cur, &src_cur, len);
	if (count != len)
		return -EFAULT;
	return 0;
}

static int bufvec_to_data_copy(struct c2_bufvec_cursor *cur, void *data,
			size_t len)
{
	c2_bcount_t	 	 count;
	struct c2_bufvec_cursor  dcur;
	struct c2_bufvec	 dest_buf;

	C2_PRE(cur != NULL);
	C2_PRE(data != NULL);
	C2_PRE(len != 0);

	data_to_bufvec(&dest_buf, &data, &len);
	c2_bufvec_cursor_init(&dcur, &dest_buf);
	count = c2_bufvec_cursor_copy(&dcur, cur, len);
	if (count != len)
		return -EFAULT;
	return 0;
}

int bufvec_uint32_encode(struct c2_bufvec_cursor *cur, uint32_t *val)
{
	int		rc = 0;

	C2_PRE(cur != NULL);
	C2_PRE(val != NULL);

	rc = data_to_bufvec_copy(cur, val, sizeof *val);
	return rc;
}

int bufvec_uint32_decode(struct c2_bufvec_cursor *cur, uint32_t *val)
{
	int		rc = 0;

	C2_PRE(cur != NULL);
	C2_PRE(val != NULL);

	rc = bufvec_to_data_copy(cur, val, sizeof *val);
	return 0;
}

/** Encode decode a 4 byte value into c2_buf_vec */
int c2_bufvec_uint32(struct c2_bufvec_cursor *vc, uint32_t *val,
		     enum bufvec_what what)
{
	int rc;

	C2_PRE(vc != NULL);
	C2_PRE(val != NULL);

	if(what == BUFVEC_ENCODE)
		rc = bufvec_uint32_encode(vc, val);
	else if(what == BUFVEC_DECODE)
		rc = bufvec_uint32_decode(vc, val);
	else
	    rc = -ENOSYS;

	return rc;
}

int bufvec_uint64_encode(struct c2_bufvec_cursor *cur, uint64_t *val)
{
	int		rc = 0;

	C2_PRE(cur != NULL);
	C2_PRE(val != NULL);

	rc = data_to_bufvec_copy(cur, val, sizeof *val);
	return rc;
}

int bufvec_uint64_decode(struct c2_bufvec_cursor *cur, uint64_t *val)
{
	int		rc = 0;

	C2_PRE(cur != NULL);
	C2_PRE(val != NULL);

	rc = bufvec_to_data_copy(cur, val, sizeof *val);
	return 0;
}

int c2_bufvec_uint64(struct c2_bufvec_cursor *vc, uint64_t *val,
		     enum bufvec_what what)
{
	int rc;

	C2_PRE(vc != NULL);
	C2_PRE(val != NULL);

	if(what == BUFVEC_ENCODE)
		rc = bufvec_uint64_encode(vc, val);
	else if(what == BUFVEC_DECODE)
		rc = bufvec_uint64_decode(vc, val);
	else
		rc = -ENOSYS;
	return rc;
}


int bufvec_uchar_encode(struct c2_bufvec_cursor *cur, unsigned char *val)
{
	int		rc = 0;

	C2_PRE(cur != NULL);
	C2_PRE(val != NULL);

	rc = data_to_bufvec_copy(cur, val, sizeof *val);
	return rc;
}

int bufvec_uchar_decode(struct c2_bufvec_cursor *cur, unsigned char *val)
{
	int		rc = 0;

	C2_PRE(cur != NULL);
	C2_PRE(val != NULL);

	rc = bufvec_to_data_copy(cur, val, sizeof *val);
	return 0;
}

int c2_bufvec_uchar(struct c2_bufvec_cursor *vc, unsigned char* val,
		   enum bufvec_what what)
{
	int 		rc;

	C2_PRE(vc != NULL);
	C2_PRE(val != NULL);

	if(what == BUFVEC_ENCODE)
		rc = bufvec_uchar_encode(vc, val);
	else if (what == BUFVEC_DECODE)
		rc = bufvec_uchar_decode(vc, val);
	else
		rc = -ENOSYS;

	return rc;
}

int c2_bufvec_int(struct c2_bufvec_cursor *vc, int *val, enum bufvec_what what)
{
	int rc;

	C2_PRE(vc != NULL);
	C2_PRE(val != NULL);

	rc = c2_bufvec_uint32(vc, (uint32_t *)val, what);
	return rc;
}

int c2_bufvec_long_int(struct c2_bufvec_cursor *vc, long *val,
		       enum bufvec_what what)
{
	int rc;

	C2_PRE(vc != NULL);
	C2_PRE(val != NULL);

	rc = c2_bufvec_uint64(vc, (uint64_t *)val, what);
	return rc;
}

int c2_bufvec_float(struct c2_bufvec_cursor *vc, float *val,
		   enum bufvec_what what)
{
	int 		rc;
	uint32_t	*u_val;
	uint64_t	*l_val;

	C2_PRE(vc != NULL);
	C2_PRE(val != NULL);

	if (sizeof *val == sizeof *u_val) {
		u_val = (uint32_t *)val;
		rc = c2_bufvec_uint32(vc, u_val, what);
	}
	else if (sizeof *val == sizeof *l_val) {
		l_val = (uint64_t *)val;
		rc = c2_bufvec_uint64(vc, l_val, what);
	}
	else
		rc = -EFAULT;

	return rc;
}

int c2_bufvec_fop(struct c2_bufvec_cursor *vc, struct c2_fop *fop,
		  enum bufvec_what what)
{
	int 			rc;
	struct c2_fop_type	*ftype;

	C2_PRE(fop != NULL);
	C2_PRE(vc != NULL);

	ftype = fop->f_type;
	C2_ASSERT(ftype->ft_ops != NULL);

	if(what == BUFVEC_ENCODE)
		rc = ftype->ft_ops->fto_bufvec_encode(fop, vc);

	else if (what == BUFVEC_DECODE)
		rc = ftype->ft_ops->fto_bufvec_decode(fop, vc);
	else
		rc = -EFAULT;

	return rc;
}


