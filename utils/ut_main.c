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

#include <stdlib.h>  /* exit, srand, rand */
#include <unistd.h>  /* getpid */
#include <time.h>    /* time */
#include <err.h>     /* warn */

#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE  /* required for basename, see man basename(3) */
#endif
#include <string.h>  /* basename */

#include "ut/ut.h"
#include "lib/trace.h"
#include "lib/user_space/trace.h" /* m0_trace_set_print_context */
#include "lib/thread.h"           /* LAMBDA */
#include "lib/getopts.h"
#include "lib/finject.h"          /* m0_fi_print_info */
#include "lib/atomic.h"

#define UT_SANDBOX "./ut-sandbox"

/* Sort test suites in alphabetic order, please. */
extern struct m0_ut_suite libm0_ut; /* test lib first */
extern struct m0_ut_suite addb_ut;
extern struct m0_ut_suite balloc_ut;
extern struct m0_ut_suite be_ut;
extern struct m0_ut_suite buffer_pool_ut;
extern struct m0_ut_suite bulkio_client_ut;
extern struct m0_ut_suite bulkio_server_ut;
extern struct m0_ut_suite capa_ut;
extern struct m0_ut_suite cm_cp_ut;
extern struct m0_ut_suite cm_generic_ut;
extern struct m0_ut_suite cob_ut;
extern struct m0_ut_suite cobfoms_ut;
extern struct m0_ut_suite conf_ut;
extern struct m0_ut_suite confc_ut;
extern struct m0_ut_suite confstr_ut;
extern struct m0_ut_suite conn_ut;
extern struct m0_ut_suite console_ut;
extern struct m0_ut_suite di_ut;
extern struct m0_ut_suite db_cursor_ut;
extern struct m0_ut_suite db_ut;
extern struct m0_ut_suite dtm_dtx_ut;
extern struct m0_ut_suite dtm_nucleus_ut;
extern struct m0_ut_suite dtm_transmit_ut;
extern struct m0_ut_suite emap_ut;
extern struct m0_ut_suite fit_ut;
extern struct m0_ut_suite fol_ut;
extern struct m0_ut_suite frm_ut;
extern struct m0_ut_suite ha_state_ut;
extern struct m0_ut_suite ios_bufferpool_ut;
extern struct m0_ut_suite item_ut;
extern struct m0_ut_suite item_source_ut;
extern struct m0_ut_suite layout_ut;
extern struct m0_ut_suite m0_addb_ut;
extern struct m0_ut_suite m0_fop_lock_ut;
extern struct m0_ut_suite m0_fom_stats_ut;
extern struct m0_ut_suite m0_net_bulk_if_ut;
extern struct m0_ut_suite m0_net_bulk_mem_ut;
extern struct m0_ut_suite m0_net_lnet_ut;
extern struct m0_ut_suite m0_net_module_ut;
extern struct m0_ut_suite m0_net_test_ut;
extern struct m0_ut_suite m0_net_tm_prov_ut;
extern struct m0_ut_suite m0d_ut;
extern struct m0_ut_suite mdservice_ut;
extern struct m0_ut_suite module_ut;
extern struct m0_ut_suite packet_encdec_ut;
extern struct m0_ut_suite parity_math_ut;
extern struct m0_ut_suite poolmach_ut;
extern struct m0_ut_suite reqh_ut;
extern struct m0_ut_suite reqh_service_ut;
extern struct m0_ut_suite rm_ut;
extern struct m0_ut_suite rpc_mc_ut;
extern struct m0_ut_suite rpc_rcv_session_ut;
extern struct m0_ut_suite rpclib_ut;
extern struct m0_ut_suite session_ut;
extern struct m0_ut_suite sm_ut;
extern struct m0_ut_suite sns_cm_repair_ut;
extern struct m0_ut_suite snscm_net_ut;
extern struct m0_ut_suite snscm_storage_ut;
extern struct m0_ut_suite snscm_xform_ut;
extern struct m0_ut_suite stats_ut;
extern struct m0_ut_suite stob_ut;
extern struct m0_ut_suite udb_ut;
extern struct m0_ut_suite xcode_bufvec_fop_ut;
extern struct m0_ut_suite xcode_ff2c_ut;
extern struct m0_ut_suite xcode_ut;
extern struct m0_ut_suite sns_flock_ut;

void add_uts(void)
{
	/* sort test suites in alphabetic order */
	m0_ut_add(&libm0_ut); /* test lib first */
	m0_ut_add(&di_ut);
	m0_ut_add(&balloc_ut);
	m0_ut_add(&be_ut);
	m0_ut_add(&buffer_pool_ut);
	m0_ut_add(&bulkio_client_ut);
	m0_ut_add(&bulkio_server_ut);
	m0_ut_add(&capa_ut);
	m0_ut_add(&cm_cp_ut);
	m0_ut_add(&cm_generic_ut);
	m0_ut_add(&cob_ut);
	m0_ut_add(&cobfoms_ut);
	m0_ut_add(&conf_ut);
	m0_ut_add(&confc_ut);
	m0_ut_add(&confstr_ut);
	m0_ut_add(&conn_ut);
	m0_ut_add(&db_cursor_ut);
	m0_ut_add(&db_ut);
	m0_ut_add(&dtm_nucleus_ut);
	m0_ut_add(&dtm_transmit_ut);
	m0_ut_add(&dtm_dtx_ut);
	m0_ut_add(&emap_ut);
	m0_ut_add(&fit_ut);
	m0_ut_add(&fol_ut);
	m0_ut_add(&frm_ut);
	m0_ut_add(&ha_state_ut);
	m0_ut_add(&ios_bufferpool_ut);
	m0_ut_add(&item_ut);
	m0_ut_add(&item_source_ut);
	m0_ut_add(&layout_ut);
	m0_ut_add(&m0_addb_ut);
	m0_ut_add(&m0_fop_lock_ut);
	m0_ut_add(&m0_fom_stats_ut);
	m0_ut_add(&m0_net_bulk_if_ut);
	m0_ut_add(&m0_net_bulk_mem_ut);
	m0_ut_add(&m0_net_lnet_ut);
	m0_ut_add(&m0_net_module_ut);
	m0_ut_add(&m0_net_test_ut);
	m0_ut_add(&m0_net_tm_prov_ut);
	m0_ut_add(&m0d_ut);
        m0_ut_add(&mdservice_ut);
	m0_ut_add(&module_ut);
	m0_ut_add(&packet_encdec_ut);
	m0_ut_add(&parity_math_ut);
	m0_ut_add(&poolmach_ut);
	m0_ut_add(&reqh_ut);
	m0_ut_add(&reqh_service_ut);
	m0_ut_add(&rm_ut);
	m0_ut_add(&rpc_mc_ut);
	m0_ut_add(&rpc_rcv_session_ut);
	m0_ut_add(&rpclib_ut);
	m0_ut_add(&session_ut);
	m0_ut_add(&sm_ut);
	m0_ut_add(&snscm_xform_ut);
	m0_ut_add(&snscm_storage_ut);
	/*
	 * XXX TODO: re-write using FOM to handle file lock resource through RM.
	 * m0_ut_add(&sns_cm_repair_ut);
	 */
	m0_ut_add(&snscm_net_ut);
	m0_ut_add(&sns_flock_ut);
	m0_ut_add(&stats_ut);
	m0_ut_add(&stob_ut);
	m0_ut_add(&udb_ut);
	m0_ut_add(&xcode_bufvec_fop_ut);
	m0_ut_add(&xcode_ff2c_ut);
	m0_ut_add(&xcode_ut);
	/* These tests have redirection of messages. */
	m0_ut_add(&console_ut);
}

int main(int argc, char *argv[])
{
	int   rc                   = EXIT_SUCCESS;
	bool  list_ut              = false;
	bool  with_tests           = false;
	bool  list_owners          = false;
	bool  finject_stats_before = false;
	bool  finject_stats_after  = false;
	bool  parse_trace          = false;
	int   seed                 = -1;

	const char *fault_point         = NULL;
	const char *fp_file_name        = NULL;
	const char *trace_mask          = NULL;
	const char *trace_level         = NULL;
	const char *trace_print_context = NULL;

	struct m0_ut_cfg cfg = {
		.uc_sandbox      = UT_SANDBOX,
	};

	/* add options in alphabetic order, M0_HELPARG should be first */
	rc = M0_GETOPTS(basename(argv[0]), argc, argv,
		    M0_HELPARG('h'),
		    M0_STRINGARG('e', "trace level: level[+][,level[+]]"
				 " where level is one of call|debug|info|"
				 "notice|warn|error|fatal",
				LAMBDA(void, (const char *str) {
					trace_level = str;
				})),
		    M0_STRINGARG('f', "fault point to enable func:tag:type"
				      "[:integer[:integer]]",
				      LAMBDA(void, (const char *str) {
					 fault_point = str;
				      })
				),
		    M0_STRINGARG('F', "yaml file, which contains a list"
				      " of fault points to enable",
				      LAMBDA(void, (const char *str) {
					 fp_file_name = str;
				      })
				),
		    M0_FORMATARG('H', "shuffle test suites before execution. "
				 "The argument is a seed value. "
				 "0 to shuffle randomly", "%u", &seed),
		    M0_FLAGARG('k', "keep the sandbox directory",
				&cfg.uc_keep_sandbox),
		    M0_FLAGARG('l', "list available test suites",
				&list_ut),
		    M0_VOIDARG('L', "list available test suites with"
				    " their tests",
				LAMBDA(void, (void) {
						list_ut = true;
						with_tests = true;
				})),
		    M0_STRINGARG('m', "trace mask, either numeric (HEX/DEC) or"
			         " comma-separated list of subsystem names"
				 " (use ! at the beginning to invert)",
				LAMBDA(void, (const char *str) {
					trace_mask = str;
				})),
		    M0_VOIDARG('M', "print available trace subsystems",
				LAMBDA(void, (void) {
					m0_trace_print_subsystems();
					exit(EXIT_SUCCESS);
				})),
		    M0_FLAGARG('o', "list test owners",
				&list_owners),
		    M0_STRINGARG('p', "trace print context, values:"
				 " none, func, short, full",
				LAMBDA(void, (const char *str) {
					trace_print_context = str;
				})),
		    M0_FLAGARG('s', "report fault injection stats before UT",
				&finject_stats_before),
		    M0_FLAGARG('S', "report fault injection stats after UT",
				&finject_stats_after),
		    M0_STRINGARG('t', "test list 'suite[:test][,suite"
				      "[:test]]'",
				      LAMBDA(void, (const char *str) {
					    cfg.uc_run_list = str;
				      })
				),
		    M0_FLAGARG('T', "parse trace log produced earlier"
			       " (trace data is read from STDIN)",
				&parse_trace),
		    M0_STRINGARG('x', "exclude list 'suite[:test][,suite"
				      "[:test]]'",
				      LAMBDA(void, (const char *str) {
					 cfg.uc_exclude_list = str;
				      })
				),
		    M0_FLAGARG('y', "use YAML format for output",
				&cfg.uc_yaml_output),
		    );
	if (rc != 0)
		return rc;

	rc = m0_ut_init(&cfg);
	if (rc != 0)
		return rc;

	rc = m0_trace_set_immediate_mask(trace_mask) ?:
		 m0_trace_set_level(trace_level);
	if (rc != 0)
		goto out;

	rc = m0_trace_set_print_context(trace_print_context);
	if (rc != 0) {
		warn("Error: invalid value for -p option");
		goto out;
	}

	if (parse_trace) {
		rc = m0_trace_parse(stdin, stdout, false, false, NULL);
		goto out;
	}

	/* enable fault points as early as possible */
	if (fault_point != NULL) {
		rc = m0_ut_enable_fault_point(fault_point);
		if (rc != 0)
			goto out;
	}

	if (fp_file_name != NULL) {
		rc = m0_ut_enable_fault_points_from_file(fp_file_name);
		if (rc != 0)
			goto out;
	}

	if (finject_stats_before) {
		m0_fi_print_info();
		printf("\n");
	}

	/* check conflicting options */
	if ((cfg.uc_run_list != NULL && cfg.uc_exclude_list != NULL) ||
	    (list_ut && (cfg.uc_run_list != NULL ||
			 cfg.uc_exclude_list != NULL || list_owners)))
	{
		fprintf(stderr, "Error: conflicting options: only one of the"
				" -l -L -o -t -x option can be used at the same"
				" time.\n");
		rc = EXIT_FAILURE;
		goto out;
	}

	add_uts();
	if (seed != -1) {
		if (seed == 0) {
			seed = time(NULL) ^ (getpid() << 17);
			printf("Seed: %u.\n", seed);
		}
		m0_ut_shuffle(seed);
	}

	if (list_ut)
		m0_ut_list(with_tests);
	else if (list_owners)
		m0_ut_list_owners();
	else
		rc = m0_ut_run();

	if (finject_stats_after) {
		printf("\n");
		m0_fi_print_info();
	}
out:
	m0_ut_fini();
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
