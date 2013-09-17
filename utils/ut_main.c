/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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

#include <stdlib.h>  /* exit */
#include <CUnit/CUnit.h>

#include "ut/ut.h"
#include "lib/trace.h"
#include "lib/user_space/trace.h" /* m0_trace_set_print_context */
#include "lib/thread.h"           /* LAMBDA */
#include "lib/getopts.h"
#include "lib/finject.h"          /* m0_fi_print_info */
#include "lib/atomic.h"
#include "utils/common.h"

#define UT_SANDBOX "./ut-sandbox"

/* Sort test suites in alphabetic order, please. */
extern const struct m0_test_suite libm0_ut; /* test lib first */
extern const struct m0_test_suite ad_ut;
extern const struct m0_test_suite addb_ut;
extern const struct m0_test_suite adieu_ut;
extern const struct m0_test_suite balloc_ut;
extern const struct m0_test_suite be_ut;
extern const struct m0_test_suite buffer_pool_ut;
extern const struct m0_test_suite bulkio_client_ut;
extern const struct m0_test_suite bulkio_server_ut;
extern const struct m0_test_suite capa_ut;
extern const struct m0_test_suite cm_cp_ut;
extern const struct m0_test_suite cm_generic_ut;
extern const struct m0_test_suite cob_ut;
extern const struct m0_test_suite cobfoms_ut;
extern const struct m0_test_suite conf_ut;
extern const struct m0_test_suite confc_ut;
extern const struct m0_test_suite confstr_ut;
extern const struct m0_test_suite conn_ut;
extern const struct m0_test_suite console_ut;
extern const struct m0_test_suite db_cursor_ut;
extern const struct m0_test_suite db_ut;
extern const struct m0_test_suite emap_ut;
extern const struct m0_test_suite fit_ut;
extern const struct m0_test_suite fol_ut;
extern const struct m0_test_suite frm_ut;
extern const struct m0_test_suite ios_bufferpool_ut;
extern const struct m0_test_suite item_ut;
extern const struct m0_test_suite item_source_ut;
extern const struct m0_test_suite layout_ut;
extern const struct m0_test_suite m0_addb_ut;
extern const struct m0_test_suite m0_fop_lock_ut;
extern const struct m0_test_suite m0_fom_stats_ut;
extern const struct m0_test_suite m0_mgmt_conf_ut;
extern const struct m0_test_suite m0_mgmt_svc_ut;
extern const struct m0_test_suite m0_net_bulk_if_ut;
extern const struct m0_test_suite m0_net_bulk_mem_ut;
extern const struct m0_test_suite m0_net_lnet_ut;
extern const struct m0_test_suite m0_net_test_ut;
extern const struct m0_test_suite m0_net_tm_prov_ut;
extern const struct m0_test_suite m0d_ut;
extern const struct m0_test_suite mdservice_ut;
extern const struct m0_test_suite packet_encdec_ut;
extern const struct m0_test_suite parity_math_ut;
extern const struct m0_test_suite poolmach_ut;
extern const struct m0_test_suite reqh_ut;
extern const struct m0_test_suite reqh_service_ut;
extern const struct m0_test_suite rm_ut;
extern const struct m0_test_suite rpc_mc_ut;
extern const struct m0_test_suite rpc_rcv_session_ut;
extern const struct m0_test_suite rpc_service_ut;
extern const struct m0_test_suite rpclib_ut;
extern const struct m0_test_suite session_ut;
extern const struct m0_test_suite sm_ut;
extern const struct m0_test_suite sns_cm_repair_ut;
extern const struct m0_test_suite snscm_net_ut;
extern const struct m0_test_suite snscm_storage_ut;
extern const struct m0_test_suite snscm_xform_ut;
extern const struct m0_test_suite stobio_ut;
extern const struct m0_test_suite udb_ut;
extern const struct m0_test_suite xcode_bufvec_fop_ut;
extern const struct m0_test_suite xcode_ff2c_ut;
extern const struct m0_test_suite xcode_ut;

void add_uts(void)
{
	/* sort test suites in alphabetic order */
	m0_ut_add(&libm0_ut); /* test lib first */
	m0_ut_add(&ad_ut);
	m0_ut_add(&adieu_ut);
	m0_ut_add(&balloc_ut);
	m0_ut_add(&be_ut);
	m0_ut_add(&buffer_pool_ut);
	//m0_ut_add(&bulkio_client_ut);
	// m0_ut_add(&bulkio_server_ut); /* ad_rec_part_undo_redo_op() */
	m0_ut_add(&capa_ut);
	//m0_ut_add(&cm_cp_ut);
	//m0_ut_add(&cm_generic_ut);
	m0_ut_add(&cob_ut);
	//m0_ut_add(&conf_ut);
	//m0_ut_add(&confc_ut);
	//m0_ut_add(&confstr_ut); /* db: panic: pair->dp_rec.db_i.db_dbt.b_nob <= rec.b_nob cursor_get() (db/db.c:612) */
	m0_ut_add(&conn_ut);
	//m0_ut_add(&db_cursor_ut);
	//m0_ut_add(&db_ut);
	//m0_ut_add(&emap_ut);
	m0_ut_add(&fit_ut);
	m0_ut_add(&fol_ut);
	m0_ut_add(&frm_ut);
	//m0_ut_add(&ios_bufferpool_ut);
	m0_ut_add(&item_ut);
	m0_ut_add(&item_source_ut);
	//m0_ut_add(&layout_ut);
	m0_ut_add(&m0_addb_ut);
	m0_ut_add(&m0_fop_lock_ut);
	m0_ut_add(&m0_fom_stats_ut);
	m0_ut_add(&m0_mgmt_conf_ut);
	m0_ut_add(&m0_mgmt_svc_ut);
	m0_ut_add(&m0_net_bulk_if_ut);
	m0_ut_add(&m0_net_bulk_mem_ut);
	m0_ut_add(&m0_net_lnet_ut);
	m0_ut_add(&m0_net_test_ut);
	m0_ut_add(&m0_net_tm_prov_ut);
	m0_ut_add(&m0d_ut);
	m0_ut_add(&packet_encdec_ut);
	//m0_ut_add(&parity_math_ut);
	//m0_ut_add(&poolmach_ut);
	m0_ut_add(&reqh_ut);
	m0_ut_add(&reqh_service_ut);
	m0_ut_add(&rm_ut);
	m0_ut_add(&rpc_mc_ut);
	m0_ut_add(&rpc_rcv_session_ut);
	m0_ut_add(&rpc_service_ut);
	m0_ut_add(&rpclib_ut);
	m0_ut_add(&session_ut);
	m0_ut_add(&sm_ut);
	// m0_ut_add(&snscm_net_ut); /* m0_db_tx_abort() */
	// m0_ut_add(&snscm_storage_ut);
	// m0_ut_add(&snscm_xform_ut);
	// m0_ut_add(&sns_cm_repair_ut); /* m0_db_tx_abort() */
	m0_ut_add(&stobio_ut);
	//m0_ut_add(&udb_ut);
	m0_ut_add(&xcode_bufvec_fop_ut);
	m0_ut_add(&xcode_ff2c_ut);
	m0_ut_add(&xcode_ut);
	//m0_ut_add(&cobfoms_ut);
	m0_ut_add(&mdservice_ut);
	/* These tests have redirection of messages. */
	m0_ut_add(&console_ut);
}

int main(int argc, char *argv[])
{
	int   result               = EXIT_SUCCESS;
	bool  list_ut              = false;
	bool  with_tests           = false;
	bool  list_owners          = false;
	bool  use_yaml_format      = false;
	bool  keep_sandbox         = false;
	bool  finject_stats_before = false;
	bool  finject_stats_after  = false;
	bool  parse_trace          = false;
	char *test_list_str        = NULL;
	char *exclude_list_str     = NULL;

	const char *fault_point         = NULL;
	const char *fp_file_name        = NULL;
	const char *trace_mask          = NULL;
	const char *trace_level         = NULL;
	const char *trace_print_context = NULL;

	struct m0_list  test_list;
	struct m0_list  exclude_list;

	struct m0_ut_run_cfg cfg = {
		.urc_mode              = M0_UT_BASIC_MODE,
		.urc_abort_cu_assert   = true,
		.urc_report_exec_time  = true,
		.urc_test_list         = &test_list,
		.urc_exclude_list      = &exclude_list,
	};

	result = unit_start(UT_SANDBOX);
	if (result != 0)
		return result;

	result = M0_GETOPTS("ut", argc, argv,
		    M0_HELPARG('h'),
		    M0_FLAGARG('T', "parse trace log produced earlier"
			       " (trace data is read from STDIN)",
				&parse_trace),
		    M0_STRINGARG('m', "trace mask, either numeric (HEX/DEC) or"
			         " comma-separated list of subsystem names"
				 " (use ! at the beginning to invert)",
				LAMBDA(void, (const char *str) {
					trace_mask = strdup(str);
				})
				),
		    M0_VOIDARG('M', "print available trace subsystems",
				LAMBDA(void, (void) {
					m0_trace_print_subsystems();
					exit(EXIT_SUCCESS);
				})),
		    M0_STRINGARG('p', "trace print context, values:"
				 " none, func, short, full",
				LAMBDA(void, (const char *str) {
					trace_print_context = str;
				})
				),
		    M0_STRINGARG('e', "trace level: level[+][,level[+]]"
				 " where level is one of call|debug|info|"
				 "notice|warn|error|fatal",
				LAMBDA(void, (const char *str) {
					trace_level = str;
				})
				),
		    M0_FLAGARG('k', "keep the sandbox directory",
				&keep_sandbox),
		    M0_VOIDARG('i', "CUnit interactive console",
				LAMBDA(void, (void) {
					cfg.urc_mode = M0_UT_ICONSOLE_MODE;
				})),
		    M0_VOIDARG('a', "automated CUnit with xml output",
				LAMBDA(void, (void) {
					cfg.urc_mode = M0_UT_AUTOMATED_MODE;
				})),
		    M0_FLAGARG('l', "list available test suites",
				&list_ut),
		    M0_VOIDARG('L', "list available test suites with"
				    " their tests",
				LAMBDA(void, (void) {
						list_ut = true;
						with_tests = true;
				})),
		    M0_FLAGARG('o', "list test owners",
				&list_owners),
		    M0_VOIDARG('O', "list test owners in YAML format",
				LAMBDA(void, (void) {
						list_owners = true;
						use_yaml_format = true;
				})),
		    M0_STRINGARG('t', "test list 'suite[:test][,suite"
				      "[:test]]'",
				      LAMBDA(void, (const char *str) {
					    test_list_str = strdup(str);
				      })
				),
		    M0_STRINGARG('x', "exclude list 'suite[:test][,suite"
				      "[:test]]'",
				      LAMBDA(void, (const char *str) {
					 exclude_list_str = strdup(str);
				      })
				),
		    M0_VOIDARG('A', "don't abort program on CU_ASSERT"
				    " failure",
				LAMBDA(void, (void) {
					cfg.urc_abort_cu_assert = false;
				})),
		    M0_VOIDARG('P', "don't report test execution time",
				LAMBDA(void, (void) {
					cfg.urc_report_exec_time = false;
				})),
		    M0_STRINGARG('f', "fault point to enable func:tag:type"
				      "[:integer[:integer]]",
				      LAMBDA(void, (const char *str) {
					 fault_point = strdup(str);
				      })
				),
		    M0_STRINGARG('F', "yaml file, which contains a list"
				      " of fault points to enable",
				      LAMBDA(void, (const char *str) {
					 fp_file_name = strdup(str);
				      })
				),
		    M0_FLAGARG('s', "report fault injection stats before UT",
				&finject_stats_before),
		    M0_FLAGARG('S', "report fault injection stats after UT",
				&finject_stats_after),
		    );
	if (result != 0)
		goto out;

	result = m0_trace_set_immediate_mask(trace_mask);
	if (result != 0)
		goto out;

	result = m0_trace_set_level(trace_level);
	if (result != 0)
		goto out;

	result = m0_trace_set_print_context(trace_print_context);
	if (result != 0) {
		fprintf(stderr, "Error: invalid value for -p option,"
				" allowed are: 0, 1, 2\n");
		goto out;
	}

	if (parse_trace) {
		result = m0_trace_parse(stdin, stdout, false, false, NULL);
		goto out;
	}

	/* enable fault points as early as possible */
	if (fault_point != NULL) {
		result = enable_fault_point(fault_point);
		if (result != 0)
			goto out;
	}

	if (fp_file_name != NULL) {
		result = enable_fault_points_from_file(fp_file_name);
		if (result != 0)
			goto out;
	}

	if (finject_stats_before) {
		m0_fi_print_info();
		printf("\n");
	}

	/* check conflicting options */
	if ((cfg.urc_mode != M0_UT_BASIC_MODE && (list_ut || list_owners ||
	     test_list_str != NULL || exclude_list_str != NULL)) ||
	     (list_ut && (test_list_str != NULL || exclude_list_str != NULL)) ||
	     (list_ut && list_owners))
	{
		fprintf(stderr, "Error: conflicting options: only one of the"
				" -i -I -a -l -L -o -t -x option can be used at"
				" the same time\n");
		result = EXIT_FAILURE;
		goto out;
	}

	m0_list_init(&test_list);
	m0_list_init(&exclude_list);

	if (test_list_str != NULL)
		parse_test_list(test_list_str, &test_list);
	if (exclude_list_str != NULL)
		parse_test_list(exclude_list_str, &exclude_list);

	add_uts();

	if (list_ut)
		m0_ut_list(with_tests);
	else if (list_owners)
		m0_ut_owners_list(use_yaml_format);
	else
		m0_ut_run(&cfg);

	if (finject_stats_after) {
		printf("\n");
		m0_fi_print_info();
	}

	if (test_list_str != NULL)
		free(test_list_str);
	if (exclude_list_str != NULL)
		free(exclude_list_str);

	free_test_list(&test_list);
	free_test_list(&exclude_list);

	m0_list_fini(&test_list);
	m0_list_fini(&exclude_list);
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
