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
 * Original author: Manish Honap <manish_honap@xyratex.com>
 * Original creation date: 01/27/2012
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "addb/addb.h"
#include "lib/ut.h"

struct m0_addb_ctx addb_ut_ctx;

const struct m0_addb_ctx_type m0_addb_ut_ctx = {
	.act_name = "ADDB-UT"
};

const struct m0_addb_loc m0_addb_ut_loc = {
	.al_name = "ADDB-UT"
};

static const char s_out_fname[] = "addb_ut_output_redirect";

#define MESSAGE_LENGTH sizeof("addb: ctx: ADDB-UT/0x8ff4fa0, loc: ADDB-UT, ev: \
			      trace/trace, rc: 0 name: A test ADDB message for \
			      M0_ADDB_TRACE event")

static void test_addb()
{
	char     buffer[MESSAGE_LENGTH];
	struct m0_ut_redirect   addb_ut_redirect;
	const char message[] = "A test ADDB message for M0_ADDB_TRACE event";
	char *fgets_rc;

	m0_stream_redirect(stdout, s_out_fname, &addb_ut_redirect);

	m0_addb_ctx_init(&addb_ut_ctx, &m0_addb_ut_ctx, &m0_addb_global_ctx);

	m0_addb_choose_default_level_console(AEL_NONE);

	M0_ADDB_ADD(&addb_ut_ctx, &m0_addb_ut_loc, m0_addb_trace, (message));

	rewind(stdout);

	fgets_rc = fgets(buffer, MESSAGE_LENGTH, stdout);
	M0_UT_ASSERT(fgets_rc != NULL);

	M0_UT_ASSERT(strstr(buffer, message) != NULL);

	m0_stream_restore(&addb_ut_redirect);

	m0_addb_choose_default_level_console(AEL_WARN);
}

const struct m0_test_suite addb_ut = {
        .ts_name  = "addb-ut",
        .ts_init  = NULL,
        .ts_fini  = NULL,
        .ts_tests = {
                { "addb", test_addb},
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
