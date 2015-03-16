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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 05/08/2011
 */

#include <stdio.h>            /* fprintf */
#include <unistd.h>           /* pause */
#include <signal.h>           /* sigaction */
#include <sys/time.h>
#include <sys/resource.h>

#ifdef HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

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
   @addtogroup m0d
   @{
 */

/**
   Represents various network transports supported
   by a particular node in a cluster.
 */
static struct m0_net_xprt *cs_xprts[] = {
	&m0_net_lnet_xprt
};

static volatile sig_atomic_t gotsignal = 0;

/**
   Signal handler registered so that pause()
   returns in order to trigger proper cleanup.
 */
static void cs_term_sig_handler(int signum)
{
	gotsignal = 1;
}

/**
   Registers signal handler to catch SIGTERM, SIGINT and
   SIGQUIT signals and pause the mero process.
 */
static void cs_wait_for_termination(void)
{
	struct sigaction        term_act;

	term_act.sa_handler = cs_term_sig_handler;
	sigemptyset(&term_act.sa_mask);
	term_act.sa_flags = 0;
	sigaction(SIGTERM, &term_act, NULL);
	sigaction(SIGINT,  &term_act, NULL);
	sigaction(SIGQUIT, &term_act, NULL);

	printf("Press CTRL+C to quit.\n");
	fflush(stdout);
	do {
		pause();
	} while (!gotsignal);
}

M0_INTERNAL int main(int argc, char **argv)
{
	static struct m0 instance;

	int            rc;
	struct m0_mero mero_ctx;
	struct rlimit rlim = {10240, 10240};

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
	rc = m0_init(&instance);
	if (rc != 0) {
		fprintf(stderr, "\n Failed to initialise Mero \n");
		goto out;
	}

	rc = m0_cs_init(&mero_ctx, cs_xprts, ARRAY_SIZE(cs_xprts), stderr, false);
	if (rc != 0) {
		fprintf(stderr, "\n Failed to initialise Mero \n");
		goto cleanup2;
	}

	rc = m0_cs_setup_env(&mero_ctx, argc, argv);
	if (rc != 0)
		goto cleanup1;

	rc = m0_cs_start(&mero_ctx);

	if (rc == 0) {
#ifdef HAVE_SYSTEMD
		rc = sd_notify(0, "READY=1");
		if (rc < 0)
			fprintf(stderr, "systemd READY notification failed,"
					" rc=%d\n", rc);
		else if (rc == 0)
			fprintf(stderr, "systemd notifications not allowed\n");
		else
			fprintf(stderr, "systemd READY notification successfull\n");
#endif
		cs_wait_for_termination();
	}

cleanup1:
	m0_cs_fini(&mero_ctx);
cleanup2:
	m0_fini();
out:
	errno = rc < 0 ? -rc : rc;
	return errno;
}

/** @} endgroup m0d */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
