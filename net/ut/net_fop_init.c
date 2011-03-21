#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/cdefs.h"
#include "fop/fop.h"
#include "fop/fop_format_def.h"

#include "net_u.h"
#include "net_fop.h"
#include "net.ff"

int nettest_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);

static struct c2_fop_type_ops nettest_ops = {
	.fto_execute = nettest_handler,
};

C2_FOP_TYPE_DECLARE(c2_nettest, "nettest", 13, &nettest_ops);
extern struct c2_fop_type c2_addb_record_fopt; /* opcode = 14 */
extern struct c2_fop_type c2_addb_reply_fopt;
extern struct c2_fop_type_format c2_mem_buf_tfmt;
extern struct c2_fop_type_format c2_addb_record_header_tfmt;


static struct c2_fop_type *net_ut_fops[] = {
	&c2_nettest_fopt,
	&c2_addb_record_fopt,
	&c2_addb_reply_fopt
};

static struct c2_fop_type_format *net_ut_fmts[] = {
	&c2_mem_buf_tfmt,
	&c2_addb_record_header_tfmt
};


void nettest_fop_fini(void)
{
	c2_fop_type_fini_nr(net_ut_fops, ARRAY_SIZE(net_ut_fops));
	c2_fop_type_format_fini_nr(net_ut_fmts, ARRAY_SIZE(net_ut_fmts));
}

int nettest_fop_init(void)
{
	int result;

	result = c2_fop_type_format_parse_nr(net_ut_fmts, ARRAY_SIZE(net_ut_fmts));
	C2_ASSERT(result == 0);
	result = c2_fop_type_build_nr(net_ut_fops, ARRAY_SIZE(net_ut_fops));
	C2_ASSERT(result == 0);

	return result;
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
