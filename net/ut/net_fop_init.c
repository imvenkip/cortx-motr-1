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

static struct c2_fop_type *fops[] = {
	&c2_nettest_fopt
};

void nettest_fop_fini(void)
{
	c2_fop_type_fini_nr(fops, ARRAY_SIZE(fops));
}

int nettest_fop_init(void)
{
	int result;

	result = c2_fop_type_build_nr(fops, ARRAY_SIZE(fops));
	if (result != 0)
		nettest_fop_fini();
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
