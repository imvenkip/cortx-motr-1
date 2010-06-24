#include "lib/assert.h"
#include "fop/fop_format.h"

#define DEF C2_FOP_FORMAT
#define _  C2_FOP_FIELD

#define ENTRY test
DEF(fid, FFA_RECORD,
    _(f_seq, C2_FOP_TYPE_FORMAT_U64),
    _(f_oid, C2_FOP_TYPE_FORMAT_U64));

DEF(optfid, FFA_UNION,
    _(b_present, C2_FOP_TYPE_FORMAT_BYTE),
    _(b_fid,     fid),
    _(b_none,    C2_FOP_TYPE_FORMAT_VOID));

DEF(fid_array, FFA_SEQUENCE,
    _(a_fid, fid));

DEF(fid_typedef, FFA_TYPEDEF,
    _(, optfid));

DEF(c2_stob_io_seg, FFA_RECORD,
    _(f_offset, C2_FOP_TYPE_FORMAT_U64),
    _(f_count, C2_FOP_TYPE_FORMAT_U32));

DEF(c2_stob_io_buf, FFA_SEQUENCE,
    _(ib_value, C2_FOP_TYPE_FORMAT_BYTE));

DEF(c2_stob_io_vec, FFA_SEQUENCE,
    _(v_seg, c2_stob_io_seg));

DEF(c2_stob_io_write_fop, FFA_RECORD,
    _(siw_object, fid),
    _(siw_vec, c2_stob_io_vec),
    _(siw_buf, c2_stob_io_buf));


static struct c2_fop_type_format *fmt[] = {
        &fid,
        &optfid,
        &fid_array,
        &fid_typedef,
        &c2_stob_io_seg,
        &c2_stob_io_buf,
        &c2_stob_io_vec,
        &c2_stob_io_write_fop,
        NULL
};

void ENTRY(void)
{
	int result;
	struct c2_fop_type_format **f;

	for (f = fmt; *f != NULL; ++f) {
		result = c2_fop_type_format_parse(*f);
		C2_ASSERT(result == 0);
	}

	for (f = fmt; *f != NULL; ++f)
		c2_fop_type_format_c((*f)->ftf_out);
}
