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
 * Original author: Manish Honap <manish_honap@xyratex.com>
 * Original creation date: 01/27/2012
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "addb/addb.h"
#include "lib/ut.h"

struct c2_addb_ctx addb_ut_ctx;

const struct c2_addb_ctx_type c2_addb_ut_ctx = {
	        .act_name = "ADDB-UT"
};

const struct c2_addb_loc c2_addb_ut_loc = {
	        .al_name = "ADDB-UT"
};

/*
  strlen("addb: ctx: ADDB-UT/0x8ff4fa0, loc: ADDB-UT, ev: trace/trace, rc: 0"
	 " name: A test ADDB message for C2_ADDB_TRACE event") = 116 + \0
 */

#define MESSAGE_LENGTH 117

void test_addb()
{
	int	 rc;
	int	 out_fd;
	FILE	*out_fp;
	char	 buffer[MESSAGE_LENGTH];

	const char *message = "A test ADDB message for C2_ADDB_TRACE event";


	/* save fd of stdout  */
	out_fd = dup(fileno(stdout));
	C2_UT_ASSERT(out_fd != -1);

	/* redirect stdout */
	out_fp = freopen("out_file", "a+", stdout);
	C2_UT_ASSERT(out_fp != NULL);

	c2_addb_ctx_init(&addb_ut_ctx, &c2_addb_ut_ctx, &c2_addb_global_ctx);

	c2_addb_choose_default_level(AEL_NONE);

	C2_ADDB_ADD(&addb_ut_ctx, &c2_addb_ut_loc, c2_addb_trace, (message));

	rewind(out_fp);

	fgets(buffer, MESSAGE_LENGTH, out_fp);

	C2_UT_ASSERT(strstr(buffer, message) != NULL);

	C2_UT_ASSERT((rc = fclose(out_fp)) == 0);

	/* restore stdout */
	out_fd = dup2(out_fd, 1);
	C2_UT_ASSERT(out_fd != -1);
	stdout = fdopen(out_fd, "a+");
	C2_UT_ASSERT(stdout != NULL);
}

const struct c2_test_suite addb_ut = {
        .ts_name  = "addb-ut",
        .ts_init  = NULL,
        .ts_fini  = NULL,
        .ts_tests = {
                { "ADDB", test_addb},
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
