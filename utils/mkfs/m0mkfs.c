/* -*- C -*- */
/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Yuriy Umanets <yuriy_umanets@xyratex.com>
 * Original creation date: 20 Mar 2014
 */

#include <stdio.h>            /* fprintf */
#include <unistd.h>           /* pause */
#include <signal.h>           /* sigaction */
#include <sys/time.h>
#include <sys/resource.h>

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"         /* M0_SET0 */

#include "mero/setup.h"
#include "mero/init.h"
#include "mero/version.h"
#include "module/instance.h"  /* m0 */
#include "net/lnet/lnet.h"
#include "reqh/reqh_service.h"

/**
   @addtogroup m0mkfs
   @{
 */

/**
   Represents various network transports supported
   by a particular node in a cluster.
 */
static struct m0_net_xprt *cs_xprts[] = {
	&m0_net_lnet_xprt
};

M0_INTERNAL int main(int argc, char **argv)
{
	int              rc;
	struct m0_mero   mero_ctx;
	static struct m0 instance;
	struct rlimit    rlim = {10240, 10240};

	if (argc > 1 &&
	    (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0)) {
		m0_build_info_print();
		exit(EXIT_SUCCESS);
	}

	rc = setrlimit(RLIMIT_NOFILE, &rlim);
	if (rc != 0) {
		fprintf(stderr, "\n Failed to setrlimit\n");
		goto out;
	}
	errno = 0;
	M0_SET0(&mero_ctx);
	mero_ctx.cc_mkfs = true;
	rc = m0_init(&instance);
	if (rc != 0) {
		fprintf(stderr, "\n Failed to initialise Mero \n");
		goto out;
	}

	rc = m0_cs_init(&mero_ctx, cs_xprts, ARRAY_SIZE(cs_xprts), stderr);
	if (rc != 0) {
		fprintf(stderr, "\n Failed to initialise Mero \n");
		goto cleanup;
	}

        rc = m0_cs_setup_env(&mero_ctx, argc, argv);
	m0_cs_fini(&mero_ctx);
cleanup:
	m0_fini();
out:
	errno = rc < 0 ? -rc : rc;
	return errno;
}

/** @} endgroup m0mkfs */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
