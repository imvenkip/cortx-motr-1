/* -*- C -*- */

#include <errno.h>
#include <stdio.h>

#include "fop/fop_format.h"
#include "lib/assert.h"

struct xdr_attr {
	const char *xa_type;
};

#define XDR_ATTR(type, xdr_type) [type] = {	\
	.xa_type = #xdr_type			\
}

static const struct xdr_attr xdr_attrs[FFT_NR] = {
	XDR_ATTR(FFT_ZERO, zero),
	XDR_ATTR(FFT_VOID, void),
	XDR_ATTR(FFT_BOOL, bool),
	XDR_ATTR(FFT_CHAR, uint8_t),
	XDR_ATTR(FFT_64, uint64_t),
	XDR_ATTR(FFT_32, uint32_t),
	XDR_ATTR(FFT_RECORD, struct),
	XDR_ATTR(FFT_UNION, union),
	XDR_ATTR(FFT_ARRAY, array),
	XDR_ATTR(FFT_BITMASK, notimplemented),
	XDR_ATTR(FFT_FID, struct c2_fid),
	XDR_ATTR(FFT_NAME, notimplemneted),
	XDR_ATTR(FFT_PATH, notimplemneted),
	XDR_ATTR(FFT_PRINCIPAL, notimplemneted),
	XDR_ATTR(FFT_CAPABILITY, notimplemneted),
	XDR_ATTR(FFT_TIMESTAMP, notimplemneted),
	XDR_ATTR(FFT_EPOCH, notimplemneted),
	XDR_ATTR(FFT_VERSION, notimplemneted),
	XDR_ATTR(FFT_OFFSET, c2_bindex_t),
	XDR_ATTR(FFT_COUNT, c2_bcount_t),
	XDR_ATTR(FFT_BUFFER, char<>),
	XDR_ATTR(FFT_RESOURCE, notimplemneted),
	XDR_ATTR(FFT_LOCK, notimplemneted),
	XDR_ATTR(FFT_NODE, notimplemneted),
	XDR_ATTR(FFT_FOP, notimplemneted),
	XDR_ATTR(FFT_REF, notimplemneted),
	XDR_ATTR(FFT_OTHER, notimplemneted)
};

static const struct c2_fop_field_format bindex_t_fmt[] = {
	C2_FOP_FIELD_FORMAT("c2_bindex_t", FFT_64)
};

static struct c2_fop_field_descr bindex_t = 
	{ .ffd_fmt = bindex_t_fmt, .ffd_nr = ARRAY_SIZE(bindex_t_fmt) };

static const struct c2_fop_field_format fid_fmt[] = {
	C2_FOP_FIELD_FORMAT("c2_fid", FFT_RECORD),
		C2_FOP_FIELD_FORMAT("f_seq", FFT_64),
		C2_FOP_FIELD_FORMAT("f_oid", FFT_64),
	C2_FOP_FIELD_FORMAT_END
};

static struct c2_fop_field_descr fid = 
	{ .ffd_fmt = fid_fmt, .ffd_nr = ARRAY_SIZE(fid_fmt) };


static const struct c2_fop_field_format iovec_fmt[] = {
	C2_FOP_FIELD_FORMAT("iovec", FFT_RECORD),
		C2_FOP_FIELD_FORMAT("iv_buf", FFT_ARRAY),
			C2_FOP_FIELD_FORMAT("buf", FFT_CHAR),
		C2_FOP_FIELD_FORMAT_END,
	C2_FOP_FIELD_FORMAT_END
};

static struct c2_fop_field_descr iovec = 
	{ .ffd_fmt = iovec_fmt, 
	  .ffd_nr = ARRAY_SIZE(iovec_fmt) };

static const struct c2_fop_field_format fop_write_fmt[] = {
	C2_FOP_FIELD_FORMAT("fop_write", FFT_RECORD),
		C2_FOP_FIELD_FORMAT("wr_fid", FFT_FID),
		C2_FOP_FIELD_FORMAT("wr_offset", FFT_OFFSET),
		C2_FOP_FIELD_REF("wr_iovec", iovec),
	C2_FOP_FIELD_FORMAT_END
};

static struct c2_fop_field_descr fop_write = 
		{ .ffd_fmt = fop_write_fmt, 
		  .ffd_nr = ARRAY_SIZE(fop_write_fmt) };


static struct c2_fop_field_descr *descr[] =
	{ &bindex_t, &fid, &iovec, &fop_write };

static const char ruler[C2_FOP_MAX_FIELD_DEPTH] = "\t\t\t\t\t\t\t\t";

static bool field_has_children(const struct c2_fop_field *field)
{
	return !c2_list_is_empty(&field->ff_child) && 
		ergo(field->ff_base != NULL,
		     field->ff_base->ffb_type != FFT_ARRAY);
}

static struct c2_fop_field *list2field(struct c2_list_link *link)
{
	return container_of(link, struct c2_fop_field, ff_sibling);
}

static bool isarray(const struct c2_fop_field *field)
{
	return field->ff_base->ffb_type == FFT_ARRAY;
}

static const char *typename(const struct c2_fop_field *field)
{
	const char *tname;

	if (field->ff_ref != NULL)
		tname = field->ff_ref->ff_name;
	else if (isarray(field))
		tname = typename(list2field(field->ff_child.l_head));
	else
		tname = xdr_attrs[field->ff_base->ffb_type].xa_type;
	return tname;
}

static enum c2_fop_field_cb_ret  
post_print_cb(struct c2_fop_field *field, unsigned depth, void *arg)
{
	if (field_has_children(field))
		printf("\n%*.*s}", depth, depth, ruler);
	printf(" %s%s;", depth == 0 ? "" : field->ff_name, 
	       isarray(field) ? "<>" : "");
	return FFC_CONTINUE;
}

static enum c2_fop_field_cb_ret 
pre_print_cb(struct c2_fop_field *field, unsigned depth, void *arg)
{
	bool goinside;

	goinside = field_has_children(field);

	printf("\n%*.*s%s%s %s%s", depth, depth, ruler, 
	       (depth == 0 && !goinside) ? "typedef " : "",
	       typename(field),
	       depth == 0 ? field->ff_name : "", 
	       goinside ? " {" : "");
	if (!goinside)
		post_print_cb(field, depth, arg);
	return goinside ? FFC_CONTINUE : FFC_SKIP;
}

int main(int argc, char **argv)
{
	int result;
	int i;

	for (i = 0; i < ARRAY_SIZE(descr); ++i) {
		result = c2_fop_field_format_parse(descr[i]);
		C2_ASSERT(result == 0 && descr[i]->ffd_field != NULL);
		c2_fop_field_traverse(descr[i]->ffd_field, 
				      &pre_print_cb, &post_print_cb, NULL);
	}

	printf("\n");
	return 0;
}

/** @} end of fop group */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
