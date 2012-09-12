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
 * Original creation date: 09/03/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/* @todo remove */
#ifndef __KERNEL__
#include <stdio.h>		/* printf */
#endif

/* @todo debug only, remove it */
#ifndef __KERNEL__
#define LOGD(format, ...) printf(format, ##__VA_ARGS__)
#else
#define LOGD(format, ...) do {} while (0)
#endif

#include "lib/memory.h"		/* C2_ALLOC_PTR */
#include "lib/misc.h"		/* C2_SET0 */
#include "lib/errno.h"		/* ENOENT */

#include "net/test/service.h"

/**
   @defgroup NetTestServiceInternals Test Service
   @ingroup NetTestInternals

   @see
   @ref net-test

   @{
 */

/** Service state transition matrix. @see net-test-lspec-state */
static bool state_transition[C2_NET_TEST_SERVICE_NR][C2_NET_TEST_SERVICE_NR] = {
	[C2_NET_TEST_SERVICE_UNINITIALIZED] = {
		[C2_NET_TEST_SERVICE_UNINITIALIZED] = false,
		[C2_NET_TEST_SERVICE_READY]	    = true,
		[C2_NET_TEST_SERVICE_FINISHED]	    = false,
		[C2_NET_TEST_SERVICE_FAILED]	    = false,
	},
	[C2_NET_TEST_SERVICE_READY] = {
		[C2_NET_TEST_SERVICE_UNINITIALIZED] = true,
		[C2_NET_TEST_SERVICE_READY]	    = false,
		[C2_NET_TEST_SERVICE_FINISHED]	    = true,
		[C2_NET_TEST_SERVICE_FAILED]	    = true,
	},
	[C2_NET_TEST_SERVICE_FINISHED] = {
		[C2_NET_TEST_SERVICE_UNINITIALIZED] = true,
		[C2_NET_TEST_SERVICE_READY]	    = false,
		[C2_NET_TEST_SERVICE_FINISHED]	    = false,
		[C2_NET_TEST_SERVICE_FAILED]	    = false,
	},
	[C2_NET_TEST_SERVICE_FAILED] = {
		[C2_NET_TEST_SERVICE_UNINITIALIZED] = true,
		[C2_NET_TEST_SERVICE_READY]	    = false,
		[C2_NET_TEST_SERVICE_FINISHED]	    = false,
		[C2_NET_TEST_SERVICE_FAILED]	    = false,
	},
};

int c2_net_test_service_init(struct c2_net_test_service *svc,
			     struct c2_net_test_service_ops *ops)
{
	C2_PRE(svc != NULL);
	C2_PRE(ops != NULL);

	C2_SET0(svc);
	svc->nts_ops = ops;

	svc->nts_svc_ctx = svc->nts_ops->ntso_init(svc);
	if (svc->nts_svc_ctx != NULL)
		c2_net_test_service_state_change(svc,
				C2_NET_TEST_SERVICE_READY);

	C2_POST(ergo(svc->nts_svc_ctx != NULL,
		     c2_net_test_service_invariant(svc)));

	return svc->nts_errno;
}

void c2_net_test_service_fini(struct c2_net_test_service *svc)
{
	C2_PRE(c2_net_test_service_invariant(svc));
	C2_PRE(svc->nts_state != C2_NET_TEST_SERVICE_UNINITIALIZED);

	svc->nts_ops->ntso_fini(svc->nts_svc_ctx);
	c2_net_test_service_state_change(svc,
			C2_NET_TEST_SERVICE_UNINITIALIZED);
}

bool c2_net_test_service_invariant(struct c2_net_test_service *svc)
{
	if (svc == NULL)
		return false;
	if (svc->nts_ops == NULL)
		return false;
	return true;
}

int c2_net_test_service_step(struct c2_net_test_service *svc)
{
	C2_PRE(c2_net_test_service_invariant(svc));
	C2_PRE(svc->nts_state == C2_NET_TEST_SERVICE_READY);

	svc->nts_errno = svc->nts_ops->ntso_step(svc->nts_svc_ctx);
	if (svc->nts_errno != 0)
		c2_net_test_service_state_change(svc,
				C2_NET_TEST_SERVICE_FAILED);

	C2_POST(c2_net_test_service_invariant(svc));
	return svc->nts_errno;
}

int c2_net_test_service_cmd_handle(struct c2_net_test_service *svc,
				   struct c2_net_test_cmd *cmd,
				   struct c2_net_test_cmd *reply)
{
	struct c2_net_test_service_cmd_handler *handler;
	int					i;

	C2_PRE(c2_net_test_service_invariant(svc));
	C2_PRE(cmd != NULL);
	C2_PRE(reply != NULL);
	C2_PRE(svc->nts_state == C2_NET_TEST_SERVICE_READY);

	svc->nts_errno = -ENOENT;
	for (i = 0; i < svc->nts_ops->ntso_cmd_handler_nr; ++i) {
		handler = &svc->nts_ops->ntso_cmd_handler[i];
		if (handler->ntsch_type == cmd->ntc_type) {
			svc->nts_errno = handler->ntsch_handler(
					 svc->nts_svc_ctx, cmd, reply);
			break;
		}
	}

	C2_POST(c2_net_test_service_invariant(svc));
	return svc->nts_errno;
}

void c2_net_test_service_state_change(struct c2_net_test_service *svc,
				      enum c2_net_test_service_state state)
{
	C2_PRE(c2_net_test_service_invariant(svc));

	C2_ASSERT(state_transition[svc->nts_state][state]);
	svc->nts_state = state;

	C2_POST(c2_net_test_service_invariant(svc));
}

enum c2_net_test_service_state
c2_net_test_service_state_get(struct c2_net_test_service *svc)
{
	C2_PRE(c2_net_test_service_invariant(svc));

	return svc->nts_state;
}

/**
   @} end of NetTestServiceInternals group
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
