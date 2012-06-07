/* -*- C -*- */
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
 * Original author: Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 04/30/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "lib/cdefs.h"		/* NULL */

#include "net/test/user_space/node_config_u.h"

/*
static char *node_role	      = NULL;
static char *test_type	      = NULL;
static long  count	      = C2_NET_TEST_CONFIG_COUNT_DEFAULT;
static long  size	      = C2_NET_TEST_CONFIG_SIZE_DEFAULT;
static char *console	      = C2_NET_TEST_CONFIG_CONSOLE_DEFAULT;
static char *target[C2_NET_TEST_CONFIG_TARGETS_MAX];
static int   target_nr        = 0;
*/

int c2_net_test_node_config_init(struct c2_net_test_node_config *cfg)
{
	return -1;
}

void c2_net_test_node_config_fini(struct c2_net_test_node_config *cfg)
{
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
