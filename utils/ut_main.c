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
#include "utils/common.h"

/* sort test suites in alphabetic order */
extern const struct c2_test_suite libc2_ut; /* test lib first */
extern const struct c2_test_suite adieu_ut;
extern const struct c2_test_suite ad_ut;
extern const struct c2_test_suite bulkio_ut;
extern const struct c2_test_suite capa_ut;
extern const struct c2_test_suite cob_ut;
extern const struct c2_test_suite console_ut;
extern const struct c2_test_suite db_ut;
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
extern const struct c2_test_suite reqh_ut;
extern const struct c2_test_suite rpc_onwire_ut;
extern const struct c2_test_suite xcode_bufvec_ut;
extern const struct c2_test_suite colibri_setup_ut;
extern const struct c2_test_suite rpclib_ut;
extern const struct c2_test_suite cfm_ut;
extern const struct c2_test_suite yaml2db_ut;
extern const struct c2_test_suite buffer_pool_ut;

#define UT_SANDBOX "./ut-sandbox"

int main(int argc, char *argv[])
{
	int  result;
	bool keep   = false;

	result = C2_GETOPTS("ut", argc, argv,
			    C2_VOIDARG('T', "parse trace log produced earlier",
				       LAMBDA(void, (void) {
						       c2_trace_parse();
						       exit(0);
					       })),
			    C2_FLAGARG('k', "keep the sandbox directory",
				       &keep));
	if (result != 0)
		return result;

	if (unit_start(UT_SANDBOX) == 0) {
		/* sort test suites in alphabetic order */
	        c2_ut_add(&bulkio_ut);
		/*
	        c2_ut_add(&libc2_ut);
		c2_ut_add(&ad_ut);
		c2_ut_add(&adieu_ut);
		c2_ut_add(&buffer_pool_ut);
	        c2_ut_add(&bulkio_ut);
		c2_ut_add(&capa_ut);
		c2_ut_add(&cfm_ut);
		c2_ut_add(&cob_ut);
		c2_ut_add(&colibri_setup_ut);
		c2_ut_add(&console_ut);
		c2_ut_add(&db_ut);
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
		c2_ut_add(&sm_ut);
		c2_ut_add(&stobio_ut);
		c2_ut_add(&udb_ut);
		c2_ut_add(&xcode_bufvec_fop_ut);
		c2_ut_add(&xcode_bufvec_ut);
		c2_ut_add(&yaml2db_ut);
		*/
		c2_ut_run("c2ut.log");
		if (!keep)
			unit_end(UT_SANDBOX);
	}

	return 0;
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
