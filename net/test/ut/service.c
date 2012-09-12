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

#include "lib/ut.h"			/* C2_UT_ASSERT */
#include "lib/misc.h"			/* C2_SET0 */

#include "net/test/service.h"

enum {
	SERVICE_ITERATIONS_NR	= 0x1000,
};

static struct c2_net_test_cmd service_cmd;
static struct c2_net_test_cmd service_reply;
static bool		      service_init_called;
static bool		      service_fini_called;
static bool		      service_step_called;
static bool		      service_cmd_called[C2_NET_TEST_CMD_NR];
static int		      service_cmd_errno;

static bool *service_func_called[] = {
	&service_init_called,
	&service_fini_called,
	&service_step_called
};

static int service_ut_cmd(struct c2_net_test_node_ctx *node_ctx,
			  const struct c2_net_test_cmd *cmd,
			  struct c2_net_test_cmd *reply,
			  enum c2_net_test_cmd_type cmd_type)
{
	service_cmd_called[cmd_type] = true;
	C2_UT_ASSERT(node_ctx == &service_ut_node_ctx);
	C2_UT_ASSERT(cmd == &service_cmd);
	C2_UT_ASSERT(reply == &service_reply);
	return service_cmd_errno;
}

static int service_ut_cmd_init(struct c2_net_test_node_ctx *node_ctx,
			       const struct c2_net_test_cmd *cmd,
			       struct c2_net_test_cmd *reply)
{
	return service_ut_cmd(node_ctx, cmd, reply, C2_NET_TEST_CMD_INIT);
}

static int service_ut_cmd_start(struct c2_net_test_node_ctx *node_ctx,
				const struct c2_net_test_cmd *cmd,
				struct c2_net_test_cmd *reply)
{
	return service_ut_cmd(node_ctx, cmd, reply, C2_NET_TEST_CMD_START);
}

static int service_ut_cmd_stop(struct c2_net_test_node_ctx *node_ctx,
			       const struct c2_net_test_cmd *cmd,
			       struct c2_net_test_cmd *reply)
{
	return service_ut_cmd(node_ctx, cmd, reply, C2_NET_TEST_CMD_STOP);
}

static int service_ut_cmd_status(struct c2_net_test_node_ctx *node_ctx,
				 const struct c2_net_test_cmd *cmd,
				 struct c2_net_test_cmd *reply)
{
	return service_ut_cmd(node_ctx, cmd, reply, C2_NET_TEST_CMD_STATUS);
}

static int service_ut_init(struct c2_net_test_node_ctx *node_ctx)
{
	service_init_called = true;
	return 0;
}

static void service_ut_fini(struct c2_net_test_node_ctx *node_ctx)
{
	service_fini_called = true;
}

static int service_ut_step(struct c2_net_test_node_ctx *node_ctx)
{
	service_step_called = true;
	return 0;
}

static struct c2_net_test_service_cmd_handler service_ut_cmd_handler[] = {
	{
		.ntsch_type    = C2_NET_TEST_CMD_INIT,
		.ntsch_handler = service_ut_cmd_init,
	},
	{
		.ntsch_type    = C2_NET_TEST_CMD_START,
		.ntsch_handler = service_ut_cmd_start,
	},
	{
		.ntsch_type    = C2_NET_TEST_CMD_STOP,
		.ntsch_handler = service_ut_cmd_stop,
	},
	{
		.ntsch_type    = C2_NET_TEST_CMD_STATUS,
		.ntsch_handler = service_ut_cmd_status,
	},
};

static struct c2_net_test_service_ops service_ut_ops = {
	.ntso_init	     = service_ut_init,
	.ntso_fini	     = service_ut_fini,
	.ntso_step	     = service_ut_step,
	.ntso_cmd_handler    = service_ut_cmd_handler,
	.ntso_cmd_handler_nr = ARRAY_SIZE(service_ut_cmd_handler),
};

static void service_ut_checks(struct c2_net_test_service *svc,
			      enum c2_net_test_service_state state)
{
	enum c2_net_test_service_state svc_state;
	bool			       rc_bool;

	rc_bool = c2_net_test_service_invariant(svc);
	C2_UT_ASSERT(rc_bool);
	svc_state = c2_net_test_service_state_get(svc);
	C2_UT_ASSERT(svc_state == state);
}

static void service_ut_check_reset(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(service_func_called); ++i)
		*service_func_called[i] = false;
	C2_SET_ARR0(service_cmd_called);
}

static void service_ut_check_called(bool *func_bool)
{
	size_t func_nr = ARRAY_SIZE(service_func_called);
	size_t cmd_nr  = ARRAY_SIZE(service_cmd_called);
	bool   called;
	bool  *called_i;
	int    called_nr = 0;
	int    i;

	C2_PRE(func_bool != NULL);

	for (i = 0; i < func_nr + cmd_nr; ++i) {
		called_i = i < func_nr ? service_func_called[i] :
					 &service_cmd_called[i - func_nr];
		called = func_bool == called_i;
		C2_UT_ASSERT(equi(called, *called_i));
		called_nr += *called_i;
	}
	C2_UT_ASSERT(called_nr == 1);
}

static bool service_can_handle(enum c2_net_test_cmd_type cmd_type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(service_ut_cmd_handler); ++i) {
		if (service_ut_cmd_handler[i].ntsch_type == cmd_type)
			return true;
	}
	return false;
}

void c2_net_test_service_ut(void)
{
	struct c2_net_test_service svc;
	enum c2_net_test_cmd_type  cmd_type;
	int			   rc;
	uint64_t		   seed = 42;
	int			   i;
	int			   cmd_max;
	int			   cmd_index;

	C2_SET0(&service_ut_node_ctx);

	/* test c2_net_test_service_init() */
	service_ut_check_reset();
	rc = c2_net_test_service_init(&svc, &service_ut_node_ctx,
				      &service_ut_ops);
	C2_UT_ASSERT(rc == 0);
	service_ut_checks(&svc, C2_NET_TEST_SERVICE_READY);
	service_ut_check_called(&service_init_called);

	/* test c2_net_test_service_step()/c2_net_test_service_cmd_handle() */
	cmd_max = ARRAY_SIZE(service_cmd_called) + 1;
	for (i = 0; i < SERVICE_ITERATIONS_NR; ++i)
	{
		cmd_index = c2_rnd(cmd_max, &seed);
		if (cmd_index == cmd_max - 1) {
			/* step */
			service_ut_check_reset();
			rc = c2_net_test_service_step(&svc);
			C2_UT_ASSERT(rc == 0);
			service_ut_check_called(&service_step_called);
			service_ut_checks(&svc, C2_NET_TEST_SERVICE_READY);
		} else {
			/* command */
			cmd_type = cmd_index;
			service_cmd_errno = service_can_handle(cmd_type) ?
					    -c2_rnd(64, &seed) : -ENOENT;
			service_ut_check_reset();
			service_cmd.ntc_type = cmd_type;
			rc = c2_net_test_service_cmd_handle(&svc, &service_cmd,
							    &service_reply);
			C2_UT_ASSERT(rc == service_cmd_errno);
			service_ut_checks(&svc, C2_NET_TEST_SERVICE_READY);
		}
	}

	/* test C2_NET_TEST_SERVICE_FAILED state */
	c2_net_test_service_state_change(&svc, C2_NET_TEST_SERVICE_FAILED);
	service_ut_checks(&svc, C2_NET_TEST_SERVICE_FAILED);

	/* test c2_net_test_service_fini() */
	service_ut_check_reset();
	c2_net_test_service_fini(&svc);
	service_ut_check_called(&service_fini_called);
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
