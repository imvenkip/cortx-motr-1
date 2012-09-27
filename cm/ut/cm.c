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
 * Original author: Subhash Arya <subhash_arya@xyratex.com>
 * Original creation date: 09/25/2012
 */

#include "lib/finject.h"
#include "lib/memory.h"
#include "lib/ut.h"
#include "reqh/reqh.h"
#include "reqh/reqh_service.h"
#include "cm/cm.h"
#include "cm/cp.h"

static struct c2_reqh   reqh;
static struct c2_cm_cp  cp;
static struct c2_cm     cm_ut;
struct c2_reqh_service *service;

static int cm_ut_service_start(struct c2_reqh_service *service)
{
	struct  c2_cm *cm;
	int	       rc;

	cm = container_of(service, struct c2_cm, cm_service);
	rc = c2_cm_setup(cm);
	return rc;
}

static void cm_ut_service_stop(struct c2_reqh_service *service)
{
	struct c2_cm *cm = container_of(service, struct c2_cm, cm_service);
	c2_cm_fini(cm);
}

static void cm_ut_service_fini(struct c2_reqh_service *service)
{

}

static const struct c2_reqh_service_ops cm_ut_service_ops = {
	.rso_start = cm_ut_service_start,
	.rso_stop  = cm_ut_service_stop,
	.rso_fini  = cm_ut_service_fini
};

static void cm_cp_ut_free(struct c2_cm_cp *cp)
{

}

static const struct c2_cm_cp_ops cm_cp_ut_ops = {
	.co_free = cm_cp_ut_free
};

static struct c2_cm_cp* cm_ut_cp_alloc(struct c2_cm *cm)
{
	cp.c_ops = &cm_cp_ut_ops;
	return &cp;
}

static int cm_ut_setup(struct c2_cm *cm)
{
	return 0;
}

static int cm_ut_start(struct c2_cm *cm)
{
	return 0;
}

static int cm_ut_stop(struct c2_cm *cm)
{
	return 0;
}


static int cm_ut_data_next(struct c2_cm *cm, struct c2_cm_cp *cp)
{
	return -ENODATA;
}

static void cm_ut_fini(struct c2_cm *cm)
{

}

static const struct c2_cm_ops cm_ut_ops = {
	.cmo_setup     = cm_ut_setup,
	.cmo_start     = cm_ut_start,
	.cmo_stop      = cm_ut_stop,
	.cmo_cp_alloc  = cm_ut_cp_alloc,
	.cmo_data_next = cm_ut_data_next,
	.cmo_fini      = cm_ut_fini
};

static int cm_ut_service_allocate(struct c2_reqh_service_type *stype,
				  struct c2_reqh_service **service)
{
	struct c2_cm_type	*cm_type;
	struct c2_cm		*cm;
	int			 rc;

	cm = &cm_ut;
	cm_type = container_of(stype, struct c2_cm_type, ct_stype);
	*service = &cm->cm_service;
	(*service)->rs_type = stype;
	(*service)->rs_ops = &cm_ut_service_ops;
	(*service)->rs_state = C2_RST_INITIALISING;
	rc = c2_cm_init(cm, cm_type, &cm_ut_ops);
	return rc;
}

static const struct c2_reqh_service_type_ops cm_ut_service_type_ops = {
	.rsto_service_allocate = cm_ut_service_allocate,
};

C2_CM_TYPE_DECLARE(cm_ut, &cm_ut_service_type_ops, "cm_ut");

static int ut_init(void)
{
	int	rc;
	c2_reqh_init(&reqh, NULL, (void*)1, (void*)1, (void*)1);
	rc = c2_cm_type_register(&cm_ut_cmt);
	C2_ASSERT(rc == 0);

	return 0;
}

static int ut_fini(void)
{
	c2_cm_type_deregister(&cm_ut_cmt);
        c2_reqh_fini(&reqh);

        return 0;
}

static void cm_ut_service_alloc_init()
{
	int	rc;
	/* Internally calls c2_cm_init(). */
	rc = c2_reqh_service_allocate(&cm_ut_cmt.ct_stype, &service);
	C2_UT_ASSERT(rc == 0);

	c2_reqh_service_init(service, &reqh);
}

static void cm_ut_service_cleanup()
{
	c2_reqh_service_stop(service);
	c2_reqh_service_fini(service);
}

static void cm_setup_ut(void)
{
	int			rc;

	cm_ut_service_alloc_init();

	/* Internally calls c2_cm_setup(). */
	rc = c2_reqh_service_start(service);
	C2_UT_ASSERT(rc == 0);

	/* Checks if the restructuring process is started successfully. */
	rc = c2_cm_start(&cm_ut);
	C2_UT_ASSERT(rc == 0);
	sleep(1);

	rc = c2_cm_stop(&cm_ut);
	C2_UT_ASSERT(rc == 0);
	cm_ut_service_cleanup();
}

static void cm_init_failure_ut(void)
{
	int			rc;
	struct c2_reqh_service *service = NULL;

	c2_fi_enable_once("c2_cm_init", "init_failure");
	rc = c2_reqh_service_allocate(&cm_ut_cmt.ct_stype, &service);
	C2_UT_ASSERT(rc != 0);

}

static void cm_setup_failure_ut(void)
{
	int			rc;

	cm_ut_service_alloc_init();
	c2_fi_enable_once("c2_cm_setup", "setup_failure_2");
	rc = c2_reqh_service_start(service);
	C2_UT_ASSERT(rc != 0);

	c2_reqh_service_fini(service);
}

const struct c2_test_suite cm_generic_ut = {
        .ts_name = "cm-ut",
        .ts_init = &ut_init,
        .ts_fini = &ut_fini,
        .ts_tests = {
                { "cm_setup_ut", cm_setup_ut },
		{ "cm_setup_failure_ut", cm_setup_failure_ut },
		{ "cm_init_failure_ut", cm_init_failure_ut },
		{ NULL, NULL }
        }
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
