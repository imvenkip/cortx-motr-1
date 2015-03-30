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
 * Original author: Egor Nikulenkov <egor.nikulenkov@seagate.com>

 * Original creation date: 24-Feb-2015
 */


#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SPIEL
#include "lib/trace.h"

#include "lib/memory.h"                 /* M0_ALLOC_PTR */
#include "lib/errno.h"
#include "lib/string.h"
#include "lib/finject.h"
#include "ut/ut.h"
#include "spiel/spiel.h"
#include "spiel/ut/spiel_ut_common.h"
#include "ut/file_helpers.h"            /* M0_UT_CONF_PROFILE */

 #include "net/lnet/lnet.h"  /* m0_net_lnet_xprt */


struct m0_spiel_ut_reqh  *spl_reqh;


static void spiel_start_stop(void)
{
	int              rc;
	struct m0_spiel  spiel;
	const char      *confd_eps[] = { "0@lo:12345:35:1", NULL };
	const char      *profile = M0_UT_CONF_PROFILE;

	rc = m0_spiel_start(&spiel, &spl_reqh->sur_reqh, confd_eps, profile);
	M0_UT_ASSERT(rc == 0);

	m0_spiel_stop(&spiel);
}


static int spiel_ut_init()
{
	int         rc;
	const char *ep = "0@lo:12345:35:1";
	const char *client_ep = "0@lo:12345:34:1";

	M0_ALLOC_PTR(spl_reqh);

	rc = m0_spiel__ut_reqh_init(spl_reqh, client_ep);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel__ut_confd_start(&spl_reqh->sur_confd_srv, ep,
				      M0_UT_CONF_PATH("conf-str.txt"));
	M0_UT_ASSERT(rc == 0);

	return 0;
}

static int spiel_ut_fini()
{
	m0_spiel__ut_confd_stop(&spl_reqh->sur_confd_srv);
	m0_spiel__ut_reqh_fini(spl_reqh);

	m0_free(spl_reqh);

	return 0;
}


const struct m0_ut_suite spiel_ut = {
	.ts_name = "spiel-ut",
	.ts_init = spiel_ut_init,
	.ts_fini = spiel_ut_fini,
	.ts_tests = {
		{ "spiel-start-stop", spiel_start_stop },
		{ NULL, NULL },
	},
};
M0_EXPORTED(spiel_conf_ut);


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
