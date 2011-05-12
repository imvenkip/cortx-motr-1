#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/cdefs.h"
#include "fop/fop.h"
#include "fop/fom.h"
#include "fop/fop_iterator.h"
#include "ioservice/io_foms.h"

#include "stob/ut/io_fop.h"
#include "fop/fop_format_def.h"
#include "ioservice/io_fops.h"

#ifdef __KERNEL__
# include "io_k.h"
# include "io_fops_k.h"
# include "addb/linux_kernel/addb_k.h"
# define write_handler NULL
# define read_handler NULL
# define create_handler NULL
# define quit_handler NULL

#else

int create_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);
int read_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);
int write_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);
int quit_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);

#include "io_u.h"
#include "ioservice/io_fops_u.h"
# include "addb/addb_u.h"
#endif

#include "stob/ut/io.ff"
#include "addb/addb.ff"

/**
   @addtogroup stob
   @{
 */

static struct c2_fop_type_ops write_ops = {
	.fto_execute = write_handler,
};

static struct c2_fop_type_ops read_ops = {
	.fto_execute = read_handler,
};

static struct c2_fop_type_ops create_ops = {
	.fto_execute = create_handler,
};

static struct c2_fop_type_ops quit_ops = {
	.fto_execute = quit_handler,
};

C2_FOP_TYPE_DECLARE(c2_io_write,      "write",  10, &write_ops);
C2_FOP_TYPE_DECLARE(c2_io_read,       "read",   11, &read_ops);
C2_FOP_TYPE_DECLARE(c2_io_create,     "create", 12, &create_ops);
C2_FOP_TYPE_DECLARE(c2_io_quit,       "quit",   13, &quit_ops);

C2_FOP_TYPE_DECLARE(c2_io_write_rep,  "write reply",  21, NULL);
C2_FOP_TYPE_DECLARE(c2_io_read_rep,   "read reply",   22, NULL);
C2_FOP_TYPE_DECLARE(c2_io_create_rep, "create reply", 23, NULL);

struct c2_fop_type c2_addb_record_fopt = {
	.ft_code = (14),
	.ft_name = "addb",
	.ft_fmt  = &c2_addb_record_header_tfmt,
	.ft_ops  = NULL
};

struct c2_fop_type c2_addb_reply_fopt = {
	.ft_code = (24),
	.ft_name = "addb reply",
	.ft_fmt  = &c2_addb_reply_tfmt,
	.ft_ops  = NULL
};

static struct c2_fop_type *fops[] = {
	&c2_io_write_fopt,
	&c2_io_read_fopt,
	&c2_io_create_fopt,
	&c2_io_quit_fopt,
	&c2_addb_record_fopt,

	&c2_io_write_rep_fopt,
	&c2_io_read_rep_fopt,
	&c2_io_create_rep_fopt,

	&c2_addb_reply_fopt,

	&c2_fop_cob_readv_fopt,
	&c2_fop_cob_writev_fopt,
	&c2_fop_cob_writev_rep_fopt,
	&c2_fop_cob_readv_rep_fopt,
};

static struct c2_fop_type_format *fmts[] = {
	&c2_fop_fid_tfmt,
	&c2_io_seg_tfmt,
	&c2_io_buf_tfmt,
	&c2_io_vec_tfmt,

	&c2_mem_buf_tfmt,

	&c2_fop_file_fid_tfmt,
	&c2_fop_io_buf_tfmt,
	&c2_fop_io_seg_tfmt,
	&c2_fop_io_vec_tfmt,
	&c2_fop_segment_tfmt,
	&c2_fop_segment_seq_tfmt,
};

void io_fop_fini(void)
{
	c2_fop_object_fini();
	c2_fop_type_fini_nr(fops, ARRAY_SIZE(fops));
	c2_fop_type_format_fini_nr(fmts, ARRAY_SIZE(fmts));
}

int io_fop_init(void)
{
	int result;

	result = c2_fop_type_format_parse_nr(fmts, ARRAY_SIZE(fmts));
	if (result == 0) {
		result = c2_fop_type_build_nr(fops, ARRAY_SIZE(fops));
		if (result == 0)
			c2_fop_object_init(&c2_fop_fid_tfmt);
	}
	if (result != 0)
		io_fop_fini();
	return result;
}

/** @} end group stob */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
