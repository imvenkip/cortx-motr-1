/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 12/05/2010
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "lib/cdefs.h"
#include "fop/fop.h"
#include "rpc/rpc_opcodes.h"
#include "net/ut/net.h"
#include "net/ut/net_fop.h"

int nettest_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);

static struct c2_fop_type_ops nettest_ops = {
	.fto_execute = nettest_handler,
};

C2_FOP_TYPE_DECLARE_XC(c2_nettest, "nettest", &nettest_ops,
		       C2_NET_TEST_OPCODE, 0);

static struct c2_fop_type *net_ut_fops[] = {
	&c2_nettest_fopt,
	&c2_addb_record_fopt,
	&c2_addb_reply_fopt
};

void nettest_fop_fini(void)
{
	c2_fop_type_fini_nr(net_ut_fops, ARRAY_SIZE(net_ut_fops));
	c2_xc_net_fini();
}

int nettest_fop_init(void)
{
	int result;

	c2_xc_net_init();
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
