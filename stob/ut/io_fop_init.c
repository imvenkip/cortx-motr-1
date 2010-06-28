#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/cdefs.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "fop/fop.h"
#include "fop/fop_format.h"
#include "fop/fop_format_def.h"
#include "io.ff"

#include "io_u.h"

/**
   @addtogroup stob
   @{
 */

static struct c2_fop_format_initdata *fmts[] = {
	C2_FOP_FORMAT_INITDATA(c2_fop_fid, u),
	C2_FOP_FORMAT_INITDATA(c2_stob_io_seg, u),
	C2_FOP_FORMAT_INITDATA(c2_stob_io_buf, u),
	C2_FOP_FORMAT_INITDATA(c2_stob_io_vec, u),
	C2_FOP_FORMAT_INITDATA(c2_stob_io_write_fop, u),
	C2_FOP_FORMAT_INITDATA(c2_stob_io_write_rep_fop, u),
	C2_FOP_FORMAT_INITDATA(c2_stob_io_read_fop, u),
	C2_FOP_FORMAT_INITDATA(c2_stob_io_read_rep_fop, u),
	C2_FOP_FORMAT_INITDATA(c2_stob_io_create_fop, u),
	C2_FOP_FORMAT_INITDATA(c2_stob_io_create_rep_fop, u)
};

int io_fop_init(void)
{
	int i;
	int result;

	for (result = 0, i = 0; i < ARRAY_SIZE(fmts) && result == 0; ++i)
		result = c2_fop_type_build(&fmts[i]);
	if (result != 0)
		io_fop_fini();
	return result;
}

void io_fop_fini(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fmts); ++i) {
		if (fmts[i]->ftf_out != NULL)
			c2_fop_field_type_fini(fmts[i]->ftf_out);
	}
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
