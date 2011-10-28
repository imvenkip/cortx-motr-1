/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 28-Oct-2011
 */


#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/ut.h"
#include "lib/ub.h"
#include "lib/time.h"
#include "lib/thread.h"             /* C2_LAMBDA */

#include "addb/addb.h"
#include "sm/sm.h"

static struct c2_sm_group G;
static struct c2_addb_ctx actx;
static struct c2_sm       m;

static int init(void) {
	c2_sm_group_init(&G);
	return 0;
}

static int fini(void) {
	c2_sm_group_fini(&G);
	return 0;
}

/**
   Unit test for c2_sm_state_set().

   Performs a state transition for a very simple state machine:
   @dot
   digraph M {
           S_INITIAL -> S_TERMINAL
   }
   @enddot
 */
static void transition(void)
{
	enum { S_INITIAL, S_TERMINAL, S_NR };
	const struct c2_sm_state_descr states[S_NR] = {
		[S_INITIAL] = {
			.sd_flags     = 0,
			.sd_name      = "initial",
			.sd_in        = NULL,
			.sd_ex        = NULL,
			.sd_invariant = NULL,
			.sd_allowed   = (1 << S_TERMINAL)
		},
		[S_TERMINAL] = {
			.sd_flags     = SDF_TERMINAL,
			.sd_name      = "terminal",
			.sd_in        = NULL,
			.sd_ex        = NULL,
			.sd_invariant = NULL,
			.sd_allowed   = 0
		}
	};
	const struct c2_sm_conf        conf = {
		.scf_name      = "test drive: transition",
		.scf_nr_states = S_NR,
		.scf_state     = states
	};

	c2_sm_group_lock(&G);
	c2_sm_init(&m, &conf, S_INITIAL, &G, &actx);

	c2_sm_state_set(&m, S_TERMINAL);
	C2_UT_ASSERT(m.sm_state == S_TERMINAL);

	c2_sm_fini(&m);
	c2_sm_group_unlock(&G);
}

/**
   Unit test for c2_sm_ast_post().
 */
static void ast(void)
{
	struct c2_sm_ast *ast;
	bool              x = false;

	c2_sm_group_lock(&G);
	ast = c2_sm_ast_get(&G);
	C2_UT_ASSERT(ast != NULL);

	ast->sa_cb = LAMBDA(void, (struct c2_sm_group *g, struct c2_sm_ast *a) {
			C2_UT_ASSERT(g == &G);
			C2_UT_ASSERT(a == ast);
			x = true;
		} );
	c2_sm_ast_post(&G, ast);
	C2_UT_ASSERT(!x);
	c2_sm_group_unlock(&G);
	C2_UT_ASSERT(x);
}

/**
   Unit test for c2_sm_timeout().

   @dot
   digraph M {
           S_INITIAL -> S_0 [label="timeout t0"]
           S_0 -> S_1 [label="timeout t1"]
           S_0 -> S_2
           S_1 -> S_TERMINAL
           S_2 -> S_TERMINAL
   }
   @enddot
 */
static void timeout(void)
{
	enum { S_INITIAL, S_0, S_1, S_2, S_TERMINAL, S_NR };
	const struct c2_sm_state_descr states[S_NR] = {
		[S_INITIAL] = {
			.sd_flags     = 0,
			.sd_name      = "initial",
			.sd_in        = NULL,
			.sd_ex        = NULL,
			.sd_invariant = NULL,
			.sd_allowed   = (1 << S_0)
		},
		[S_0] = {
			.sd_flags     = 0,
			.sd_name      = "0",
			.sd_in        = NULL,
			.sd_ex        = NULL,
			.sd_invariant = NULL,
			.sd_allowed   = (1 << S_1)|(1 << S_2)
		},
		[S_1] = {
			.sd_flags     = 0,
			.sd_name      = "0",
			.sd_in        = NULL,
			.sd_ex        = NULL,
			.sd_invariant = NULL,
			.sd_allowed   = 1 << S_TERMINAL
		},
		[S_2] = {
			.sd_flags     = 0,
			.sd_name      = "0",
			.sd_in        = NULL,
			.sd_ex        = NULL,
			.sd_invariant = NULL,
			.sd_allowed   = 1 << S_TERMINAL
		},
		[S_TERMINAL] = {
			.sd_flags     = SDF_TERMINAL,
			.sd_name      = "terminal",
			.sd_in        = NULL,
			.sd_ex        = NULL,
			.sd_invariant = NULL,
			.sd_allowed   = 0
		}
	};
	const struct c2_sm_conf        conf = {
		.scf_name      = "test drive: timeout",
		.scf_nr_states = S_NR,
		.scf_state     = states
	};
	struct c2_sm_timeout t0;
	struct c2_sm_timeout t1;
	c2_time_t            delta;
	int                  result;

	c2_time_set(&delta, 0, C2_TIME_ONE_BILLION/100);

	c2_sm_group_lock(&G);
	c2_sm_init(&m, &conf, S_INITIAL, &G, &actx);

	/* check that timeout works */
	result = c2_sm_timeout(&m, &t0, c2_time_add(c2_time_now(), delta), S_0);
	C2_UT_ASSERT(result == 0);

	result = c2_sm_timedwait(&m, ~(1 << S_INITIAL), C2_TIME_NEVER);
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(m.sm_state == S_0);

	c2_sm_timeout_fini(&t0);

	/* check that state transition cancels the timeout */
	result = c2_sm_timeout(&m, &t1, c2_time_add(c2_time_now(), delta), S_1);
	C2_UT_ASSERT(result == 0);

	c2_sm_state_set(&m, S_2);
	C2_UT_ASSERT(m.sm_state == S_2);

	result = c2_sm_timedwait(&m, ~(1 << S_2),
				 c2_time_add(c2_time_now(),
					     c2_time_add(delta, delta)));
	C2_UT_ASSERT(result == -ETIMEDOUT);
	C2_UT_ASSERT(m.sm_state == S_2);

	c2_sm_timeout_fini(&t1);

	c2_sm_state_set(&m, S_TERMINAL);
	C2_UT_ASSERT(m.sm_state == S_TERMINAL);

	c2_sm_fini(&m);
	c2_sm_group_unlock(&G);
}

const struct c2_test_suite sm_ut = {
	.ts_name = "sm-ut",
	.ts_init = init,
	.ts_fini = fini,
	.ts_tests = {
		{ "transition", transition },
		{ "ast",        ast },
		{ "timeout",    timeout },
		{ NULL, NULL }
	}
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
