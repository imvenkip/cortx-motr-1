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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation date: 10/19/2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_ADDB

/* This file is designed to be included by addb/ut/addb_ut.c */

#include "addb/addb_svc.h"

#include "mero/setup.h"
#include "net/lnet/lnet.h"

/*
 ****************************************************************************
 * Test the ADDB service
 ****************************************************************************
 */

#define LOG_FILE_NAME "as_ut.errlog"

static char *addb_ut_svc[] = { "m0d", "-T", "linux",
			       "-D", "as_db", "-S", "as_stob",
			       "-A", "linuxstob:as_addb_stob",
			       "-w", "10",
			       "-e", "lnet:0@lo:12345:34:1",
			       "-s", M0_ADDB_SVC_NAME};
static struct m0_net_xprt *as_xprts[] = {
        &m0_net_lnet_xprt,
};
static struct m0_mero   sctx2;
static FILE               *lfile;

static void server_stop(void)
{
	m0_cs_fini(&sctx2);
	fclose(lfile);
}

static int server_start(void)
{
	int rc;

	M0_SET0(&sctx2);
	lfile = fopen(LOG_FILE_NAME, "w+");
	M0_UT_ASSERT(lfile != NULL);

        rc = m0_cs_init(&sctx2, as_xprts, ARRAY_SIZE(as_xprts), lfile, true);
        if (rc != 0)
		return rc;

        rc = m0_cs_setup_env(&sctx2, ARRAY_SIZE(addb_ut_svc), addb_ut_svc);
	if (rc == 0)
		rc = m0_cs_start(&sctx2);
        if (rc != 0)
		server_stop();

	return rc;
}

static int addb_ut_svc_rspa_called;
static void addb_ut_svc_rspa(struct m0_reqh_service *service)
{
	++addb_ut_svc_rspa_called;
}

static struct m0_reqh_service_ops addb_ut_svc_service_ops;

void addb_ut_svc_test(void)
{
	m0_time_t             saved_period;
	struct addb_post_fom *pfom;
	struct m0_fom        *fom;

	/* Skip rec_post UT hooks, only for validation of posting rec's data */
	addb_rec_post_ut_data_enabled = false;
	/*
	 * Test: Service type should be registered during initialization.
	 */
	M0_UT_ASSERT(m0_reqh_service_type_find(M0_ADDB_SVC_NAME) ==
		     &m0_addb_svc_type);

	/*
	 * Test: Start and stop the service, no FOMs
	 */
	addb_svc_start_pfom = false;
	M0_UT_ASSERT(the_addb_svc == NULL);
	M0_UT_ASSERT(server_start() == 0);
	M0_UT_ASSERT(the_addb_svc != NULL);
	server_stop();
	M0_UT_ASSERT(the_addb_svc == NULL);

	/*
	 * Test: Run the posting fom and test that we can stop it.
	 *       Also test that it loops on the minimum sleep period
	 *       without doing a post.
	 */
	addb_svc_start_pfom = true;
	M0_UT_ASSERT(!the_addb_pfom_started);
	M0_UT_ASSERT(addb_ut_svc_rspa_called == 0);
	M0_UT_ASSERT(server_start() == 0);
	M0_UT_ASSERT(the_addb_svc != NULL);
	addb_ut_svc_service_ops = *the_addb_svc->as_reqhs.rs_ops;
	addb_ut_svc_service_ops.rso_stats_post_addb = addb_ut_svc_rspa;
	the_addb_svc->as_reqhs.rs_ops = &addb_ut_svc_service_ops;
	pfom = &the_addb_svc->as_pfom;
	fom = &pfom->pf_fom;

	/* wait for the fom to start */
	m0_mutex_lock(&the_addb_svc->as_reqhs.rs_mutex);
	while (!the_addb_pfom_started)
		m0_cond_wait(&the_addb_svc->as_cond);
	m0_mutex_unlock(&the_addb_svc->as_reqhs.rs_mutex);

	/* explicitly terminate the fom. */
	addb_pfom_stop(the_addb_svc);

	/* restart the fom */
	M0_LOG(M0_DEBUG, "UT: resetting pfom");
	M0_SET0(pfom);
	addb_pfom_start(the_addb_svc);

	/* wait for the fom to start */
	m0_mutex_lock(&the_addb_svc->as_reqhs.rs_mutex);
	while (!the_addb_pfom_started)
		m0_cond_wait(&the_addb_svc->as_cond);
	m0_mutex_unlock(&the_addb_svc->as_reqhs.rs_mutex);

	/* terminate with service */
	server_stop();
	M0_UT_ASSERT(!the_addb_pfom_started);
	M0_UT_ASSERT(the_addb_svc == NULL);
	M0_UT_ASSERT(addb_ut_svc_rspa_called == 0);

	/*
	 * Test: Adjust the period of the posting fom and check that it
	 *       "ticks".
	 *       Check that stopping the request handler terminates the FOM.
	 */
	M0_LOG(M0_DEBUG, "UT: testing ticks");
	saved_period = addb_pfom_period;
#undef MS
#define MS(ms) (ms) * 1000000ULL
	addb_pfom_period = M0_MKTIME(5, 0);
	M0_UT_ASSERT(!the_addb_pfom_started);
	M0_UT_ASSERT(addb_ut_svc_rspa_called == 0);
	M0_UT_ASSERT(server_start() == 0);
	M0_UT_ASSERT(the_addb_svc != NULL);
	addb_ut_svc_service_ops = *the_addb_svc->as_reqhs.rs_ops;
	addb_ut_svc_service_ops.rso_stats_post_addb = addb_ut_svc_rspa;
	the_addb_svc->as_reqhs.rs_ops = &addb_ut_svc_service_ops;
	pfom = &the_addb_svc->as_pfom;
	fom = &pfom->pf_fom;
	m0_nanosleep(MS(210), NULL);
	M0_UT_ASSERT(the_addb_pfom_started);
	M0_UT_ASSERT(addb_ut_svc_rspa_called == fom->fo_transitions / 4);
	server_stop();
	M0_UT_ASSERT(!the_addb_pfom_started);
	M0_UT_ASSERT(the_addb_svc == NULL);
#undef MS
	addb_pfom_period = saved_period;
	/* Reset to default */
	addb_rec_post_ut_data_enabled = true;
}

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
