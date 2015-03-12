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
 * Original author: Mikhail Antropov <mikhail.v.antropov@seagate.com>

 * Original creation date: 02/24/2015
 */


#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SPIEL
#include "lib/trace.h"

#include "lib/assert.h"                 /* M0_ASSERT */
#include "spiel/spiel.h"
#include "ut/ut.h"
#include "rpc/rpclib.h"
#include "spiel/ut/spiel_ut_common.h"
#include "ut/file_helpers.h"            /* M0_UT_CONF_PROFILE */

static void spiel_start_stop(void)
{
	int                       rc;
	struct m0_spiel_ut_reqh   spl_reqh;
	struct m0_spiel           spiel;
	struct m0_rpc_server_ctx  confd_srv;
	const char               *confd_eps[] = { "0@lo:12345:35:1", NULL };
	const char               *profile = M0_UT_CONF_PROFILE;
	const char               *client_ep = "0@lo:12345:34:1";

	rc = m0_spiel__ut_reqh_init(&spl_reqh, client_ep);
	M0_ASSERT(rc == 0);

	rc = m0_spiel__ut_confd_start(&confd_srv, confd_eps[0],
				      M0_UT_CONF_PATH("conf-str.txt"));
	M0_ASSERT(rc == 0);
	rc = m0_spiel_start(&spiel, &spl_reqh.sur_reqh, confd_eps, profile);
	M0_UT_ASSERT(rc == 0);
	m0_spiel_stop(&spiel);
	m0_spiel__ut_confd_stop(&confd_srv);
	m0_spiel__ut_reqh_fini(&spl_reqh);
}

const struct m0_ut_suite spiel_ut = {
	.ts_name = "spiel-ut",
	.ts_tests = {
		{ "spiel-start-stop", spiel_start_stop },
		{ NULL, NULL },
	},
};

#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
