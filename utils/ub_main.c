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
 * Original creation date: 07/19/2010
 */

#include <stdlib.h>             /* atoi */
#include <string.h>             /* strdup */

#include "lib/ub.h"
#include "lib/memory.h"
#include "lib/cdefs.h"          /* ARRAY_SIZE */
#include "lib/thread.h"         /* LAMBDA */
#include "lib/getopts.h"
#include "utils/common.h"

extern struct m0_ub_set m0_ad_ub;
extern struct m0_ub_set m0_adieu_ub;
extern struct m0_ub_set m0_atomic_ub;
extern struct m0_ub_set m0_bitmap_ub;
extern struct m0_ub_set m0_cob_ub;
extern struct m0_ub_set m0_db_ub;
extern struct m0_ub_set m0_emap_ub;
extern struct m0_ub_set m0_fol_ub;
extern struct m0_ub_set m0_fom_ub;
extern struct m0_ub_set m0_list_ub;
extern struct m0_ub_set m0_memory_ub;
extern struct m0_ub_set m0_parity_math_ub;
extern struct m0_ub_set m0_rm_ub;
extern struct m0_ub_set m0_thread_ub;
extern struct m0_ub_set m0_tlist_ub;
extern struct m0_ub_set m0_trace_ub;

#define UB_SANDBOX "./ub-sandbox"

struct ub_args {
	bool        ua_ub_list;
	uint32_t    ua_rounds;
	char       *ua_name;
	char       *ua_args;
};

static void ub_args_fini(struct ub_args *args)
{
	m0_free(args->ua_args);
	m0_free(args->ua_name);
}

static int ub_args_parse(int argc, char *argv[], struct ub_args *out)
{
	out->ua_rounds = 1;
	out->ua_name = NULL;
	out->ua_args = NULL;
	out->ua_ub_list = false;

	return M0_GETOPTS("ub", argc, argv,
		  M0_HELPARG('h'),
		  M0_NUMBERARG('r', "Number of rounds UB has to be run",
			       LAMBDA(void, (int64_t rounds) {
					       out->ua_rounds = rounds;
				       })),
		  M0_VOIDARG('l', "List available benchmark tests",
			     LAMBDA(void, (void) {
					     out->ua_ub_list = true;
				     })),
		  M0_STRINGARG('t', "Benchmark test name to run",
			       LAMBDA(void, (const char *str) {
					       out->ua_name = strdup(str);
				       })),
		  M0_STRINGARG('a', "Benchmark test args",
			       LAMBDA(void, (const char *str) {
					       out->ua_args = strdup(str);
				       }))
		);
}

static void ub_add(const struct ub_args *args)
{
	/* Note these tests are run in reverse order from the way
	   they are listed here */

	m0_ub_set_add(&m0_ad_ub);
	m0_ub_set_add(&m0_adieu_ub);
	m0_ub_set_add(&m0_atomic_ub);
	m0_ub_set_add(&m0_bitmap_ub);
	m0_ub_set_add(&m0_cob_ub);
	m0_ub_set_add(&m0_db_ub);
	m0_ub_set_add(&m0_emap_ub);
	m0_ub_set_add(&m0_fol_ub);
	m0_ub_set_add(&m0_fom_ub);
	m0_ub_set_add(&m0_list_ub);
	m0_ub_set_add(&m0_memory_ub);
	m0_ub_set_add(&m0_parity_math_ub);
	m0_ub_set_add(&m0_rm_ub);
	m0_ub_set_add(&m0_thread_ub);
	m0_ub_set_add(&m0_tlist_ub);
	m0_ub_set_add(&m0_trace_ub);
}

static void ub_run(const struct ub_args *args)
{
	if (args->ua_ub_list) {
		m0_ub_set_print();
		return;
	}

	if (args->ua_name != NULL) {
		if (m0_ub_set_select(args->ua_name) != 0)
			return;
	}

	m0_ub_run(args->ua_rounds);
}

int main(int argc, char *argv[])
{
	struct ub_args args;
	int            rc;

	rc = unit_start(UB_SANDBOX);
	if (rc != 0)
		goto unit;

	rc = ub_args_parse(argc, argv, &args);
	if (rc != 0)
		goto parse;

	ub_add(&args);
	ub_run(&args);

parse:
	ub_args_fini(&args);
unit:
	unit_end(UB_SANDBOX, false);
	return rc;
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
