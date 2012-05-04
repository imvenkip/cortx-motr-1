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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 05/08/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>     /* fprintf */
#include <unistd.h>    /* pause */
#include <signal.h>    /* sigaction */

#include "lib/errno.h"
#include "lib/memory.h"

#include "colibri/colibri_setup.h"
#include "colibri/init.h"
#include "net/bulk_sunrpc.h"
#include "net/lnet/lnet.h"
#include "reqh/reqh_service.h"

/**
   @addtogroup colibri_setup
   @{
 */

/**
   Represents various network transports supported
   by a particular node in a cluster.
 */
static struct c2_net_xprt *cs_xprts[] = {
	&c2_net_lnet_xprt,
	&c2_net_bulk_sunrpc_xprt,
};

/**
   Global colibri context.
 */
static struct c2_colibri colibri_ctx;

/**
   Signal handler registered so that pause()
   returns in order to trigger proper cleanup.
 */
static void cs_term_sig_handler(int signum)
{

}

/**
   Registers signal handler to catch SIGTERM, SIGINT and
   SIGQUIT signals and pause the colibri process.
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

	pause();
}

int main(int argc, char **argv)
{
	int     rc;

	errno = 0;
	rc = c2_init();
	if (rc != 0) {
		fprintf(stderr, "\n Failed to initialise Colibri \n");
		goto out;
	}

	rc = c2_cs_init(&colibri_ctx, cs_xprts, ARRAY_SIZE(cs_xprts), stderr);
	if (rc != 0) {
		fprintf(stderr, "\n Failed to initialise Colibri \n");
		goto cleanup2;
	}

        rc = c2_cs_setup_env(&colibri_ctx, argc, argv);
        if (rc != 0)
                goto cleanup1;

	rc = c2_cs_start(&colibri_ctx);

	if (rc == 0)
		cs_wait_for_termination();

cleanup1:
	c2_cs_fini(&colibri_ctx);
cleanup2:
	c2_fini();
out:
	errno = rc < 0 ? -rc : rc;
	return errno;
}

/** @} endgroup colibri_setup */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
