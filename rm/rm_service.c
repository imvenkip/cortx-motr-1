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
 * Original author: Manish Honap <manish_honap@xyratex.com>
 * Original creation date: 13-Feb-2013
 */

/**
   @addtogroup rm_service
   @{
 */

#undef M0_ADDB_CT_CREATE_DEFINITION
#define M0_ADDB_CT_CREATE_DEFINITION
#include "rm/rmservice_addb.h"

#undef M0_TRACE_SUBSYSTEM
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RM
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/bob.h"

#include "mero/magic.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "rm/rm_service.h"
#include "rm/rm_fops.h"
#include "rm/ut/rings.h"
#include "rm/ut/rings.c"

static struct m0_addb_ctx m0_rms_mod_ctx;

static int rms_allocate(struct m0_reqh_service **service,
			struct m0_reqh_service_type *stype, const char *arg);
static void rms_fini(struct m0_reqh_service *service);

static int rms_start(struct m0_reqh_service *service);
static void rms_stop(struct m0_reqh_service *service);

/**
   RM Service type operations.
 */
static const struct m0_reqh_service_type_ops rms_type_ops = {
	.rsto_service_allocate = rms_allocate
};

/**
   RM Service operations.
 */
static const struct m0_reqh_service_ops rms_ops = {
	.rso_start = rms_start,
	.rso_stop = rms_stop,
	.rso_fini = rms_fini
};

M0_REQH_SERVICE_TYPE_DEFINE(m0_rm_svct, &rms_type_ops, "rmservice",
			    &m0_addb_ct_rms_serv);

static const struct m0_bob_type rms_bob = {
	.bt_name = "rm service",
	.bt_magix_offset = offsetof(struct m0_reqh_rm_service, rms_magic),
	.bt_magix = M0_RM_SERVICE_MAGIC,
	.bt_check = NULL
};

M0_BOB_DEFINE(M0_INTERNAL, &rms_bob, m0_reqh_rm_service);

/**
   Register resource manager service
 */
M0_INTERNAL int m0_rms_register(void)
{
	M0_ENTRY();

	m0_addb_ctx_type_register(&m0_addb_ct_rms_mod);
	m0_addb_ctx_type_register(&m0_addb_ct_rms_serv);
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &m0_rms_mod_ctx,
			 &m0_addb_ct_rms_mod, &m0_addb_proc_ctx);
	m0_reqh_service_type_register(&m0_rm_svct);

	/**
	 * @todo Contact confd and take list of resource types for this resource
	 * manager.
	 */

	M0_RETURN(m0_rm_fop_init());
}

/**
   Unregister resource manager service
 */
M0_INTERNAL void m0_rms_unregister(void)
{
	M0_ENTRY();

	m0_rm_fop_fini();
	m0_reqh_service_type_unregister(&m0_rm_svct);
	m0_addb_ctx_fini(&m0_rms_mod_ctx);

	M0_LEAVE();
}

static int rms_allocate(struct m0_reqh_service      **service,
			struct m0_reqh_service_type  *stype,
			const char                   *arg)
{
	struct m0_reqh_rm_service *rms;

	M0_ENTRY();

	M0_PRE(service != NULL && stype != NULL);

	M0_ALLOC_PTR_ADDB(rms, &m0_addb_gmc, M0_RMS_ADDB_LOC_ALLOCATE,
			  &m0_rms_mod_ctx);
	if (rms == NULL)
		M0_RETURN(-ENOMEM);

	m0_reqh_rm_service_bob_init(rms);

	*service = &rms->rms_svc;
	(*service)->rs_ops = &rms_ops;

	M0_RETURN(0);
}

static void rms_fini(struct m0_reqh_service *service)
{
	struct m0_reqh_rm_service *rms;

	M0_ENTRY();
	M0_PRE(service != NULL);

	rms = bob_of(service, struct m0_reqh_rm_service, rms_svc, &rms_bob);
	m0_reqh_rm_service_bob_fini(rms);
	m0_free(rms);

	M0_LEAVE();
}

static int rms_start(struct m0_reqh_service *service)
{
	int                        rc;
	struct m0_reqh_rm_service *rms;

	M0_PRE(service != NULL);
	M0_ENTRY();

	rms = bob_of(service, struct m0_reqh_rm_service, rms_svc, &rms_bob);
	m0_rm_domain_init(&rms->rms_dom);

	rc = m0_rm_type_register(&rms->rms_dom, &rings_resource_type);
	rings_resource_type.rt_ops = &rings_rtype_ops;
	M0_RETURN(rc);
}

static void rms_stop(struct m0_reqh_service *service)
{
	struct m0_reqh_rm_service *rms;

	M0_PRE(service != NULL);
	M0_ENTRY();

	rms = bob_of(service, struct m0_reqh_rm_service, rms_svc, &rms_bob);

	m0_rm_type_deregister(&rings_resource_type);
	m0_rm_domain_fini(&rms->rms_dom);

	M0_LEAVE();
}

M0_INTERNAL int m0_rm_svc_owner_create(struct m0_reqh_service *service,
				       struct m0_rm_owner    **owner,
				       struct m0_buf          *resbuf)
{
	int                         rc;
	uint64_t                    rtype_id;
	struct m0_rm_resource      *resource;
	struct m0_rm_resource_type *rtype;
	struct m0_reqh_rm_service  *rms;
	struct m0_bufvec_cursor     cursor;
	struct m0_bufvec            datum_buf =
					M0_BUFVEC_INIT_BUF(&resbuf->b_addr,
							   &resbuf->b_nob);

	M0_PRE(service != NULL);
	M0_PRE(*owner != NULL);
	M0_PRE(resbuf != NULL);
	M0_ENTRY();

	rms = bob_of(service, struct m0_reqh_rm_service, rms_svc, &rms_bob);

	m0_bufvec_cursor_init(&cursor, &datum_buf);
	rc = m0_bufvec_cursor_copyfrom(&cursor, &rtype_id, sizeof rtype_id);
	if (rc < 0)
		return rc;

	rtype = m0_rm_resource_type_lookup(&rms->rms_dom, rtype_id);
	if (rtype == NULL)
		return -EINVAL;
	M0_ASSERT(rtype->rt_ops != NULL);
	rc = rtype->rt_ops->rto_decode(&cursor, &resource);
	if (rc == 0)
		m0_rm_owner_init(*owner, resource, NULL);

	M0_RETURN(rc);
}

/** @} end of rm_service group */

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
