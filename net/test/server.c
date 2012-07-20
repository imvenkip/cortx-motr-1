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
 * Original creation date: 03/22/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/** @todo remove */
#include "lib/errno.h"	/* ENOSYS */
#include "net/test/server.h"

/**
   @defgroup NetTestServerInternals Test Server
   @ingroup NetTestInternals

   @see
   @ref net-test

   @{
 */

int c2_net_test_server_init(struct c2_net_test_server_ctx *ctx,
			    struct c2_net_test_server_cfg *cfg)
{
	return -ENOSYS;
}

void c2_net_test_server_fini(struct c2_net_test_server_ctx *ctx)
{
}

int c2_net_test_server_start(struct c2_net_test_server_ctx *ctx)
{
	return -ENOSYS;
}

void c2_net_test_server_stop(struct c2_net_test_server_ctx *ctx)
{
}

/**
   @} end of NetTestServerInternals group
 */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
