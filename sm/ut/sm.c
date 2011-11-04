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

#include "addb/addb.h"
#include "sm/sm.h"

static struct c2_sm_group G;
static struct c2_addb_ctx actx;
static struct c2_sm       m;
struct c2_sm_ast          ast;

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
			.sd_flags     = C2_SDF_INITIAL,
			.sd_name      = "initial",
			.sd_in        = NULL,
			.sd_ex        = NULL,
			.sd_invariant = NULL,
			.sd_allowed   = (1 << S_TERMINAL)
		},
		[S_TERMINAL] = {
			.sd_flags     = C2_SDF_TERMINAL,
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
	C2_UT_ASSERT(m.sm_state == S_INITIAL);

	c2_sm_state_set(&m, S_TERMINAL);
	C2_UT_ASSERT(m.sm_state == S_TERMINAL);

	c2_sm_fini(&m);
	c2_sm_group_unlock(&G);
}

static bool x;

void ast_cb(struct c2_sm_group *g, struct c2_sm_ast *a)
{
	C2_UT_ASSERT(g == &G);
	C2_UT_ASSERT(a == &ast);
	C2_UT_ASSERT(a->sa_datum == &ast_cb);
	x = true;
}

/**
   Unit test for c2_sm_ast_post().
 */
static void ast_test(void)
{

	x = false;
	ast.sa_cb = &ast_cb;
	ast.sa_datum = &ast_cb;
	c2_sm_ast_post(&G, &ast);
	C2_UT_ASSERT(!x);
	c2_sm_group_lock(&G);
	C2_UT_ASSERT(x);
	c2_sm_group_unlock(&G);
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
			.sd_flags     = C2_SDF_INITIAL,
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
			.sd_flags     = C2_SDF_TERMINAL,
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
	C2_UT_ASSERT(t0.st_active);

	result = c2_sm_timedwait(&m, ~(1 << S_INITIAL), C2_TIME_NEVER);
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(m.sm_state == S_0);
	C2_UT_ASSERT(!t0.st_active);

	c2_sm_timeout_fini(&t0);

	/* check that state transition cancels the timeout */
	result = c2_sm_timeout(&m, &t1, c2_time_add(c2_time_now(), delta), S_1);
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(t1.st_active);

	c2_sm_state_set(&m, S_2);
	C2_UT_ASSERT(m.sm_state == S_2);
	C2_UT_ASSERT(!t1.st_active);

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

struct story {
	struct c2_sm cain;
	struct c2_sm abel;
};

enum { S_INITIAL, S_ITERATE, S_FRATRICIDE, S_TERMINAL, S_NR };

static void genesis_4_8(struct c2_sm *mach)
{
	struct story *s;

	s = container_of(mach, struct story, cain);
	c2_sm_fail(&s->abel, S_TERMINAL, -EINTR);
}

/**
   Unit test for multiple machines in the group.

   @dot
   digraph M {
           S_INITIAL -> S_ITERATE
           S_ITERATE -> S_ITERATE
           S_INITIAL -> S_FRATRICIDE [label="timeout"]
           S_ITERATE -> S_TERMINAL
           S_FRATRICIDE -> S_TERMINAL
   }
   @enddot
 */
static void group(void)
{
	const struct c2_sm_state_descr states[S_NR] = {
		[S_INITIAL] = {
			.sd_flags     = C2_SDF_INITIAL,
			.sd_name      = "initial",
			.sd_in        = NULL,
			.sd_ex        = NULL,
			.sd_invariant = NULL,
			.sd_allowed   = (1 << S_ITERATE)|(1 << S_FRATRICIDE)
		},
		[S_ITERATE] = {
			.sd_flags     = 0,
			.sd_name      = "loop here",
			.sd_in        = NULL,
			.sd_ex        = NULL,
			.sd_invariant = NULL,
			.sd_allowed   = (1 << S_ITERATE)|(1 << S_TERMINAL)
		},
		[S_FRATRICIDE] = {
			.sd_flags     = 0,
			.sd_name      = "Let's go out to the field",
			.sd_in        = &genesis_4_8,
			.sd_ex        = NULL,
			.sd_invariant = NULL,
			.sd_allowed   = 1 << S_TERMINAL
		},
		[S_TERMINAL] = {
			.sd_flags     = C2_SDF_TERMINAL|C2_SDF_FAILURE,
			.sd_name      = "terminal",
			.sd_in        = NULL,
			.sd_ex        = NULL,
			.sd_invariant = NULL,
			.sd_allowed   = 0
		}
	};
	const struct c2_sm_conf        conf = {
		.scf_name      = "test drive: group",
		.scf_nr_states = S_NR,
		.scf_state     = states
	};

	struct story         s;
	struct c2_sm_timeout to;
	c2_time_t            delta;
	int                  result;

	c2_time_set(&delta, 0, C2_TIME_ONE_BILLION/100);

	c2_sm_group_lock(&G);
	c2_sm_init(&s.cain, &conf, S_INITIAL, &G, &actx);
	c2_sm_init(&s.abel, &conf, S_INITIAL, &G, &actx);

	/* check that timeout works */
	result = c2_sm_timeout(&s.cain, &to,
			       c2_time_add(c2_time_now(), delta), S_FRATRICIDE);
	C2_UT_ASSERT(result == 0);

	while (s.abel.sm_rc == 0) {
		/* live, while you can */
		c2_sm_state_set(&s.abel, S_ITERATE);
		/* give providence a chance to run */
		c2_sm_asts_run(&G);
	}
	C2_UT_ASSERT(s.abel.sm_state == S_TERMINAL);
	C2_UT_ASSERT(s.cain.sm_state == S_FRATRICIDE);

	c2_sm_state_set(&s.cain, S_TERMINAL);

	c2_sm_timeout_fini(&to);

	c2_sm_fini(&s.abel);
	c2_sm_fini(&s.cain);
	c2_sm_group_unlock(&G);
}

const struct c2_test_suite sm_ut = {
	.ts_name = "sm-ut",
	.ts_init = init,
	.ts_fini = fini,
	.ts_tests = {
		{ "transition", transition },
		{ "ast",        ast_test },
		{ "timeout",    timeout },
		{ "group",      group },
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
