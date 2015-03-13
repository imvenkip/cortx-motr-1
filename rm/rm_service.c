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
#undef M0_ADDB_RT_CREATE_DEFINITION
#define M0_ADDB_RT_CREATE_DEFINITION
#include "rm/rm_addb.h"

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RM
#include "lib/trace.h"

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/bob.h"
#include "mero/magic.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "rm/rm.h"
#include "rm/rm_service.h"
#include "rm/rm_internal.h"
#include "rm/rm_fops.h"
#include "file/file.h"

static int rms_allocate(struct m0_reqh_service **service,
			const struct m0_reqh_service_type *stype);
static void rms_fini(struct m0_reqh_service *service);

static int rms_start(struct m0_reqh_service *service);
static void rms_stop(struct m0_reqh_service *service);
static void rms_stats_post_addb(struct m0_reqh_service *service);

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
	.rso_start           = rms_start,
	.rso_start_async     = m0_reqh_service_async_start_simple,
	.rso_stop            = rms_stop,
	.rso_fini            = rms_fini,
	.rso_stats_post_addb = rms_stats_post_addb
};

M0_REQH_SERVICE_TYPE_DEFINE(m0_rms_type, &rms_type_ops, "rmservice",
			    &m0_addb_ct_rms_serv, 2, M0_CST_RMS);

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

	m0_addb_ctx_type_register(&m0_addb_ct_rm_mod);
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &m0_rm_addb_ctx,
			 &m0_addb_ct_rm_mod, &m0_addb_proc_ctx);
	m0_addb_ctx_type_register(&m0_addb_ct_rms_serv);

	m0_addb_rec_type_register(&m0_addb_rt_rm_local_rate);
	m0_addb_rec_type_register(&m0_addb_rt_rm_borrow_rate);
	m0_addb_rec_type_register(&m0_addb_rt_rm_revoke_rate);
	m0_addb_rec_type_register(&m0_addb_rt_rm_local_times);
	m0_addb_rec_type_register(&m0_addb_rt_rm_borrow_times);
	m0_addb_rec_type_register(&m0_addb_rt_rm_revoke_times);
	m0_addb_rec_type_register(&m0_addb_rt_rm_credit_times);

	m0_reqh_service_type_register(&m0_rms_type);
	/**
	 * @todo Contact confd and take list of resource types for this resource
	 * manager.
	 */

	return M0_RC(m0_rm_fop_init());
}

/**
   Unregister resource manager service
 */
M0_INTERNAL void m0_rms_unregister(void)
{
	M0_ENTRY();

	m0_rm_fop_fini();
	m0_reqh_service_type_unregister(&m0_rms_type);
	m0_addb_ctx_fini(&m0_rm_addb_ctx);

	M0_LEAVE();
}

static int rms_allocate(struct m0_reqh_service **service,
			const struct m0_reqh_service_type *stype)
{
	struct m0_reqh_rm_service *rms;

	M0_ENTRY();

	M0_PRE(service != NULL && stype != NULL);

	M0_ADDB2_IN(M0_RM_ADDB2_SERVICE_ALLOC, M0_ALLOC_PTR(rms));
	if (rms != NULL) {
		m0_reqh_rm_service_bob_init(rms);
		*service = &rms->rms_svc;
		(*service)->rs_ops = &rms_ops;
		return M0_RC(0);
	} else
		return M0_ERR(-ENOMEM);
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
	struct m0_reqh_rm_service *rms;

	M0_PRE(service != NULL);
	M0_ENTRY();

	rms = bob_of(service, struct m0_reqh_rm_service, rms_svc, &rms_bob);

	m0_rm_domain_init(&rms->rms_dom);

	rms->rms_flock_rt.rt_name = "File Lock Resource Type";

	/** Register various resource types */
	m0_file_lock_type_register(&rms->rms_dom, &rms->rms_flock_rt);

	return M0_RC(0);
}

static void rms_resources_free(struct m0_rm_resource_type *rtype)
{
	struct m0_rm_resource *resource;
	struct m0_rm_remote   *rem;
	struct m0_rm_owner    *owner;

	M0_PRE(rtype != NULL);

	m0_tl_for(res, &rtype->rt_resources, resource) {
		m0_tl_for(m0_owners, &resource->r_local, owner) {
			m0_rm_owner_windup(owner);
			m0_rm_owner_timedwait(owner, ROS_FINAL, M0_TIME_NEVER);
			m0_rm_owner_fini(owner);
			m0_free(owner);
		} m0_tl_endfor;

		m0_tl_teardown(m0_remotes, &resource->r_remote, rem) {
			m0_rm_remote_fini(rem);
			m0_free(rem);
		}
		m0_rm_resource_del(resource);
		m0_rm_resource_free(resource);
	} m0_tl_endfor;
}

static void rms_stop(struct m0_reqh_service *service)
{
	struct m0_reqh_rm_service *rms;

	M0_PRE(service != NULL);
	M0_ENTRY();

	rms = bob_of(service, struct m0_reqh_rm_service, rms_svc, &rms_bob);

	rms_resources_free(&rms->rms_flock_rt);
	m0_file_lock_type_deregister(&rms->rms_flock_rt);
	m0_rm_domain_fini(&rms->rms_dom);

	M0_LEAVE();
}

static struct m0_rm_owner *rmsvc_owner(const struct m0_rm_resource *res)
{
	M0_PRE(res != NULL);
	M0_PRE(m0_owners_tlist_length(&res->r_local) <= 1);

	return m0_owners_tlist_head(&res->r_local);
}

M0_INTERNAL int m0_rm_svc_owner_create(struct m0_reqh_service *service,
				       struct m0_rm_owner    **out,
				       struct m0_buf          *resbuf)
{
	int                         rc;
	struct m0_reqh_rm_service  *rms;
	uint64_t                    rtype_id;
	struct m0_rm_resource      *resource;
	struct m0_rm_owner         *owner;
	struct m0_rm_resource_type *rtype;
	struct m0_bufvec_cursor     cursor;
	struct m0_rm_credit        *ow_cr = NULL;
	struct m0_bufvec            datum_buf =
		M0_BUFVEC_INIT_BUF(&resbuf->b_addr,
				   &resbuf->b_nob);

	M0_PRE(service != NULL);

	M0_ENTRY();

	rms = bob_of(service, struct m0_reqh_rm_service, rms_svc, &rms_bob);

	/* Find resource type id */
	m0_bufvec_cursor_init(&cursor, &datum_buf);
	rc = m0_bufvec_cursor_copyfrom(&cursor, &rtype_id, sizeof rtype_id);
	if (rc < 0)
		return M0_RC(rc);

	rtype = m0_rm_resource_type_lookup(&rms->rms_dom, rtype_id);
	if (rtype == NULL)
		return M0_ERR(-EINVAL);
	M0_ASSERT(rtype->rt_ops != NULL);
	rc = rtype->rt_ops->rto_decode(&cursor, &resource);
	if (rc == 0) {
		struct m0_rm_resource *resadd;

		resadd = m0_rm_resource_find(rtype, resource);
		if (resadd == NULL) {
			resource->r_type = rtype;
			m0_rm_resource_add(rtype, resource);
		} else {
			m0_rm_resource_free(resource);
			resource = resadd;
		}
		owner = rmsvc_owner(resource);
		if (owner == NULL) {
			M0_ADDB2_IN(M0_RM_ADDB2_RMSVC_OWNER_ALLOC,
				    M0_ALLOC_PTR(owner));
			M0_ADDB2_IN(M0_RM_ADDB2_CREDIT_ALLOC,
				    M0_ALLOC_PTR(ow_cr));
			if (owner != NULL && ow_cr != NULL) {
				/*
				 * RM service does not belong to any group at
				 * the moment. If we change this assumption,
				 * we need to introduce function to source
				 * the group id.
				 */
				m0_rm_owner_init(owner, &m0_rm_no_group,
						 resource, NULL);
				m0_rm_credit_init(ow_cr, owner);
				ow_cr->cr_ops->cro_initial_capital(ow_cr);
				rc = m0_rm_owner_selfadd(owner, ow_cr);
				m0_rm_credit_fini(ow_cr);
				m0_free(ow_cr);
				if (rc != 0)
					goto err_add;
			} else {
				rc = M0_ERR(-ENOMEM);
				goto err_alloc;
			}
		}
		*out = owner;
	}
	return M0_RC(rc);

err_add:
	m0_rm_owner_fini(owner);
err_alloc:
	m0_free(ow_cr);
	m0_free(owner);
	m0_rm_resource_del(resource);
	return M0_ERR(rc);
}

static void rms_stats_post_addb(struct m0_reqh_service *service)
{
	int                        i;
	int                        j;
	struct m0_reqh_rm_service *rms;
	struct m0_rm_domain       *dom;

	M0_PRE(service != NULL);
	M0_ENTRY();

	rms = bob_of(service, struct m0_reqh_rm_service, rms_svc, &rms_bob);
	dom = &rms->rms_dom;

#undef CNTR_POST
#define CNTR_POST(counter)						\
		m0_addb_post_cntr(&m0_addb_gmc,                         \
				  M0_ADDB_CTX_VEC(&m0_rm_addb_ctx),	\
				  &counter);

	for (i = 0; i < ARRAY_SIZE(dom->rd_types); ++i) {
		struct m0_rm_resource_type *rt = dom->rd_types[i];
		struct rm_addb_stats       *as;

		if (rt != NULL) {
			as = &rt->rt_addb_stats;
			for (j = 0; j < ARRAY_SIZE(as->as_req); ++j) {
				CNTR_POST(as->as_req[j].rs_nr);
				CNTR_POST(as->as_req[j].rs_time);
				as->as_req[j].rs_count = 0;
			}
			CNTR_POST(as->as_credit_time);
		}
	}
#undef CNTR_POST
}

M0_INTERNAL struct m0_rm_domain *
m0_rm_svc_domain_get(const struct m0_reqh_service *svc)
{
	struct m0_reqh_rm_service *rms;

	M0_PRE(svc != NULL);
	M0_PRE(svc->rs_type == &m0_rms_type);
	rms = bob_of(svc, struct m0_reqh_rm_service, rms_svc, &rms_bob);
	return &rms->rms_dom;
}

/** @} end of rm_service group */
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
