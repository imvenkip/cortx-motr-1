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

#include <stdlib.h>        /* exit */
#include <CUnit/CUnit.h>

#include "lib/ut.h"
#include "lib/trace.h"
#include "lib/thread.h"    /* LAMBDA */
#include "lib/getopts.h"
#include "lib/memory.h"
#include "utils/common.h"

/* sort test suites in alphabetic order */
extern const struct c2_test_suite libc2_ut; /* test lib first */
extern const struct c2_test_suite adieu_ut;
extern const struct c2_test_suite ad_ut;
extern const struct c2_test_suite addb_ut;
extern const struct c2_test_suite bulkio_server_ut;
extern const struct c2_test_suite bulkio_client_ut;
extern const struct c2_test_suite cobfoms_ut;
extern const struct c2_test_suite capa_ut;
extern const struct c2_test_suite cob_ut;
extern const struct c2_test_suite console_ut;
extern const struct c2_test_suite db_ut;
extern const struct c2_test_suite db_cursor_ut;
extern const struct c2_test_suite emap_ut;
extern const struct c2_test_suite fit_ut;
extern const struct c2_test_suite fol_ut;
extern const struct c2_test_suite fop_ut;
extern const struct c2_test_suite c2_net_bulk_if_ut;
extern const struct c2_test_suite c2_net_bulk_mem_ut;
extern const struct c2_test_suite c2_net_bulk_sunrpc_ut;
extern const struct c2_test_suite parity_math_ut;
extern const struct c2_test_suite sm_ut;
extern const struct c2_test_suite stobio_ut;
extern const struct c2_test_suite udb_ut;
extern const struct c2_test_suite xcode_bufvec_fop_ut;
extern const struct c2_test_suite xcode_bufvec_ut;
extern const struct c2_test_suite xcode_ff2c_ut;
extern const struct c2_test_suite xcode_ut;
extern const struct c2_test_suite reqh_ut;
extern const struct c2_test_suite rpc_onwire_ut;
extern const struct c2_test_suite colibri_setup_ut;
extern const struct c2_test_suite rpclib_ut;
extern const struct c2_test_suite cfm_ut;
extern const struct c2_test_suite yaml2db_ut;
extern const struct c2_test_suite buffer_pool_ut;
extern const struct c2_test_suite addb_ut;
extern const struct c2_test_suite balloc_ut;
extern const struct c2_test_suite rpc_service_ut;

#define UT_SANDBOX "./ut-sandbox"

void add_uts(void)
{
	/* sort test suites in alphabetic order */
	c2_ut_add(&libc2_ut);
	c2_ut_add(&ad_ut);
	c2_ut_add(&adieu_ut);
	c2_ut_add(&balloc_ut);
	c2_ut_add(&buffer_pool_ut);
        c2_ut_add(&bulkio_server_ut);
        c2_ut_add(&bulkio_client_ut);
	c2_ut_add(&capa_ut);
	c2_ut_add(&cfm_ut);
	c2_ut_add(&cob_ut);
        c2_ut_add(&cobfoms_ut);
	c2_ut_add(&colibri_setup_ut);
	c2_ut_add(&db_ut);
	c2_ut_add(&db_cursor_ut);
	c2_ut_add(&emap_ut);
	c2_ut_add(&fit_ut);
	c2_ut_add(&fol_ut);
	c2_ut_add(&fop_ut);
	c2_ut_add(&c2_net_bulk_if_ut);
	c2_ut_add(&c2_net_bulk_mem_ut);
	c2_ut_add(&c2_net_bulk_sunrpc_ut);
	c2_ut_add(&parity_math_ut);
	c2_ut_add(&reqh_ut);
	c2_ut_add(&rpclib_ut);
	c2_ut_add(&rpc_onwire_ut);
	c2_ut_add(&rpc_service_ut);
	c2_ut_add(&sm_ut);
	c2_ut_add(&stobio_ut);
	c2_ut_add(&udb_ut);
	c2_ut_add(&xcode_bufvec_fop_ut);
	c2_ut_add(&xcode_bufvec_ut);
	c2_ut_add(&xcode_ut);
	c2_ut_add(&xcode_ff2c_ut);
	/* These tests have redirection of messages. */
	c2_ut_add(&addb_ut);
	c2_ut_add(&console_ut);
	c2_ut_add(&yaml2db_ut);
}

int parse_test_list(char *str, struct c2_list *list)
{
	char *token;
	char *subtoken;
	char *saveptr = NULL;
	struct c2_test_suite_entry *ts_entry;

	while (true) {
		token = strtok_r(str, ",", &saveptr);
		if (token == NULL)
			break;

		subtoken = strchr(token, ':');
		if (subtoken != NULL)
			*subtoken++ = '\0';

		C2_ALLOC_PTR(ts_entry);
		if (ts_entry == NULL)
			return -ENOMEM;

		ts_entry->tse_suite_name = token;
		/* subtoken can be NULL if no test was specified */
		ts_entry->tse_test_name = subtoken;

		c2_list_link_init(&ts_entry->tse_linkage);
		c2_list_add_tail(list, &ts_entry->tse_linkage);

		/* str should be NULL for subsequent strtok_r(3) calls */
		str = NULL;
	}

	return 0;
}

void free_test_list(struct c2_list *list)
{
	struct c2_test_suite_entry *entry;
	struct c2_test_suite_entry *n;
	c2_list_for_each_entry_safe(list, entry, n,
			struct c2_test_suite_entry, tse_linkage)
	{
		c2_list_del(&entry->tse_linkage);
		c2_free(entry);
	}
}

int main(int argc, char *argv[])
{
	int  result              = EXIT_SUCCESS;
	bool list_ut             = false;
	bool with_tests          = false;
	bool keep_sandbox        = false;
	char *test_list_str      = NULL;
	char *exclude_list_str   = NULL;
	struct c2_list test_list;
	struct c2_list exclude_list;

	struct c2_ut_run_cfg cfg = {
		.urc_mode              = C2_UT_BASIC_MODE,
		.urc_abort_cu_assert   = true,
		.urc_report_exec_time  = true,
		.urc_test_list         = &test_list,
		.urc_exclude_list      = &exclude_list,
	};

	result = unit_start(UT_SANDBOX);
	if (result != 0)
		return result;

	result = C2_GETOPTS("ut", argc, argv,
		    C2_HELPARG('h'),
		    C2_VOIDARG('T', "parse trace log produced earlier",
				LAMBDA(void, (void) {
						exit(c2_trace_parse());
				})),
		    C2_FLAGARG('k', "keep the sandbox directory",
				&keep_sandbox),
		    C2_VOIDARG('i', "CUnit interactive console",
				LAMBDA(void, (void) {
					cfg.urc_mode = C2_UT_ICONSOLE_MODE;
				})),
		    C2_VOIDARG('a', "automated CUnit with xml output",
				LAMBDA(void, (void) {
					cfg.urc_mode = C2_UT_AUTOMATED_MODE;
				})),
		    C2_FLAGARG('l', "list available test suites",
				&list_ut),
		    C2_VOIDARG('L', "list available test suites with"
				    " their tests",
				LAMBDA(void, (void) {
						list_ut = true;
						with_tests = true;
				})),
		    C2_STRINGARG('t', "test list 'suite[:test][,suite"
				      "[:test]]'",
				      LAMBDA(void, (const char *str) {
					    test_list_str = strdup(str);
				      })
				),
		    C2_STRINGARG('x', "exclude list 'suite[:test][,suite"
				      "[:test]]'",
				      LAMBDA(void, (const char *str) {
					 exclude_list_str = strdup(str);
				      })
				),
		    C2_VOIDARG('A', "don't abort program on CU_ASSERT"
				    " failure",
				LAMBDA(void, (void) {
					cfg.urc_abort_cu_assert = false;
				})),
		    C2_VOIDARG('P', "don't report test execution time",
				LAMBDA(void, (void) {
					cfg.urc_report_exec_time = false;
				})),
		    );
	if (result != 0)
		goto out;

	/* check conflicting options */
	if ((cfg.urc_mode != C2_UT_BASIC_MODE && (list_ut ||
	     test_list_str != NULL || exclude_list_str != NULL)) ||
	     (list_ut && (test_list_str != NULL || exclude_list_str != NULL)))
	{
		fprintf(stderr, "Error: conflicting options: only one of the"
				" -i -I -a -l -L -t -x option can be used at"
				" the same time\n");
		result = EXIT_FAILURE;
		goto out;
	}

	c2_list_init(&test_list);
	c2_list_init(&exclude_list);

	if (test_list_str != NULL)
		parse_test_list(test_list_str, &test_list);
	if (exclude_list_str != NULL)
		parse_test_list(exclude_list_str, &exclude_list);

	add_uts();

	if (list_ut)
		c2_ut_list(with_tests);
	else
		c2_ut_run(&cfg);

	if (test_list_str != NULL)
		free(test_list_str);
	if (exclude_list_str != NULL)
		free(exclude_list_str);

	free_test_list(&test_list);
	free_test_list(&exclude_list);

	c2_list_fini(&test_list);
	c2_list_fini(&exclude_list);
out:
	unit_end(UT_SANDBOX, keep_sandbox);
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
