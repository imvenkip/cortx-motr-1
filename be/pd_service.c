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
 * Original author: Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>
 * Original creation date: 17-Jun-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include "be/engine.h"
#include "be/pd_service.h"
#include "lib/errno.h"
#include "lib/memory.h"

/**
 * @addtogroup be
 * @{
 */

/* ------------------------------------------------------------------
 * TX service
 * ------------------------------------------------------------------ */

/** Transaction service. */
struct pd_service {
	struct m0_reqh_service ts_reqh;
};

static int pds_allocate(struct m0_reqh_service           **out,
			const struct m0_reqh_service_type *stype);

static const struct m0_reqh_service_type_ops pds_stype_ops = {
	.rsto_service_allocate = pds_allocate
};

struct m0_reqh_service_type m0_be_pds_stype = {
	.rst_name  = "be-pd-service",
	.rst_ops   = &pds_stype_ops,
	.rst_level = M0_BE_PD_SVC_LEVEL,
};

M0_INTERNAL int m0_be_pds_register(void)
{
	return m0_reqh_service_type_register(&m0_be_pds_stype);
}

M0_INTERNAL void m0_be_pds_unregister(void)
{
	m0_reqh_service_type_unregister(&m0_be_pds_stype);
}

static int  pds_start(struct m0_reqh_service *service);
static void pds_stop(struct m0_reqh_service *service);
static void pds_fini(struct m0_reqh_service *service);

static const struct m0_reqh_service_ops pds_ops = {
	.rso_start = pds_start,
	.rso_stop  = pds_stop,
	.rso_fini  = pds_fini
};

/** Allocates and initialises transaction service. */
static int pds_allocate(struct m0_reqh_service           **service,
			const struct m0_reqh_service_type *stype)
{
	struct pd_service *s;

	M0_ENTRY();
	M0_PRE(stype == &m0_be_pds_stype);

	M0_ALLOC_PTR(s);
	if (s == NULL)
		return M0_RC(-ENOMEM);

	*service = &s->ts_reqh;
	(*service)->rs_ops = &pds_ops;

	return M0_RC(0);
}

/** Finalises and deallocates transaction service. */
static void pds_fini(struct m0_reqh_service *service)
{
	M0_ENTRY();
	m0_free(container_of(service, struct pd_service, ts_reqh));
	M0_LEAVE();
}

static int pds_start(struct m0_reqh_service *service)
{
	M0_ENTRY();
	return M0_RC(0);
}

static void pds_stop(struct m0_reqh_service *service)
{
	M0_ENTRY();
	M0_LEAVE();
}

M0_INTERNAL int  m0_be_pd_service_init(struct m0_reqh_service **service,
				       struct m0_reqh *reqh)
{
	return m0_reqh_service_setup(service, &m0_be_pds_stype,
				     reqh, NULL, NULL);
}
M0_INTERNAL void m0_be_pd_service_fini(struct m0_reqh_service *service)
{
	m0_reqh_service_quit(service);
}

/** @} end of be group */

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
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
