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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>    /* pause */
#include <errno.h>     /* errno */
#include <signal.h>

#include "lib/errno.h"
#include "lib/memory.h"

#include "net/net.h"
#include "net/bulk_sunrpc.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "colibri/colibri_setup.h"

/**
   @addtogroup colibri_setup
   @{
 */

/*
   Basic test code representing configuration of a service
 */
void mds_start(struct c2_reqh_service *service,
                        struct c2_reqh *reqh);
void mds_stop(struct c2_reqh_service *service);
int mds_init(struct c2_reqh_service **service,
                struct c2_reqh_service_type *stype);
void mds_fini(struct c2_reqh_service *service);

struct c2_reqh_service_type mds_type;

struct c2_reqh_service_type_ops mds_type_ops = {
        .rsto_service_init = mds_init
};

struct c2_reqh_service_ops mds_ops = {
        .rso_start = mds_start,
        .rso_stop = mds_stop,
        .rso_fini = mds_fini
};

int mds_init(struct c2_reqh_service **service,
                        struct c2_reqh_service_type *stype)
{
        struct c2_reqh_service      *serv;
        int                          rc;

        C2_PRE(service != NULL && stype != NULL);

        printf("\n Initialising mds service \n");
        C2_ALLOC_PTR(serv);
        if (serv == NULL)
                return -ENOMEM;

        serv->rs_type = stype;
        serv->rs_ops = &mds_ops;

        rc = c2_reqh_service_init(serv, stype->rst_name);

        if (rc != 0) {
                *service = NULL;
                c2_free(serv);
        } else
                *service = serv;

        return rc;
}

void mds_start(struct c2_reqh_service *service, struct c2_reqh *reqh)
{
        C2_PRE(service != NULL);

        printf("\n Starting mds.. \n");
        /*
           Can perform service specific initialisation of
           objects like fops.
         */
        c2_reqh_service_start(service, reqh);
}

void mds_stop(struct c2_reqh_service *service)
{
        C2_PRE(service != NULL);

        printf("\n Stopping mds.. \n");
        /*
           Can finalise service specific objects like
           fops.
         */
        c2_reqh_service_stop(service);
}

void mds_fini(struct c2_reqh_service *service)
{
        printf("\n finalizing service \n");
        c2_reqh_service_fini(service);
        c2_free(service);
}
/* Test code ends */


/**
   Represents various network transports supported
   by a particular node in a cluster.
 */
static struct c2_net_xprt *cs_xprts [] = {
	&c2_net_bulk_sunrpc_xprt
};

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
static void cs_wait_for_termination()
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
	rc = c2_cs_init(&colibri_ctx, cs_xprts, ARRAY_SIZE(cs_xprts),stdout);
	if (rc != 0) {
		fputs("\n Failed to initialise Colibri \n", stderr);
		goto out;
	}

	/*
	   This is for test purpose, should be invoked from
	   corresponding module.
	 */
	c2_reqh_service_type_init(&mds_type, &mds_type_ops, "mds");

        rc = c2_cs_setup_env(&colibri_ctx, argc, argv);
        if (rc != 0)
                goto out;

	rc = c2_cs_start(&colibri_ctx);

	if (rc == 0)
		cs_wait_for_termination();

        /* For test purpose */
        c2_reqh_service_type_fini(&mds_type);

	c2_cs_fini(&colibri_ctx);

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
