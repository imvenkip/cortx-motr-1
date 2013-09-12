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

#undef M0_TRACE_SUBSYSTEM
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
#include "rm/file.h"

M0_TL_DESCR_DEFINE(rmsvc_owner, "RM Service Owners", static, struct m0_rm_owner,
		   ro_owner_linkage, ro_magix,
		   M0_RM_OWNER_LIST_MAGIC, M0_RM_OWNER_LIST_HEAD_MAGIC);
M0_TL_DEFINE(rmsvc_owner, static, struct m0_rm_owner);

static int rms_allocate(struct m0_reqh_service      **service,
			struct m0_reqh_service_type  *stype,
			struct m0_reqh_context       *rctx);
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
	.rso_stop            = rms_stop,
	.rso_fini            = rms_fini,
	.rso_stats_post_addb = rms_stats_post_addb
};

M0_REQH_SERVICE_TYPE_DEFINE(m0_rms_type, &rms_type_ops, "rmservice",
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

	m0_addb_ctx_type_register(&m0_addb_ct_rm_mod);
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &m0_rm_addb_ctx,
			 &m0_addb_ct_rm_mod, &m0_addb_proc_ctx);
	m0_addb_ctx_type_register(&m0_addb_ct_rms_serv);

#undef RT_REG
#define RT_REG(n) m0_addb_rec_type_register(&m0_addb_rt_rm_##n)
	RT_REG(borrow_rate);
	RT_REG(revoke_rate);
	RT_REG(borrow_times);
	RT_REG(revoke_times);
	RT_REG(credit_times);
#undef RT_REG

	m0_reqh_service_type_register(&m0_rms_type);
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
	m0_reqh_service_type_unregister(&m0_rms_type);
	m0_addb_ctx_fini(&m0_rm_addb_ctx);

	M0_LEAVE();
}

static int rms_allocate(struct m0_reqh_service      **service,
			struct m0_reqh_service_type  *stype,
			struct m0_reqh_context       *rctx)
{
	struct m0_reqh_rm_service *rms;

	M0_ENTRY();

	M0_PRE(service != NULL && stype != NULL);

	RM_ALLOC_PTR(rms, SERVICE_ALLOC, &m0_rm_addb_ctx);
	if (rms == NULL)
		M0_RETURN(-ENOMEM);

	m0_reqh_rm_service_bob_init(rms);

	*service = &rms->rms_svc;
	(*service)->rs_ops = &rms_ops;
	rmsvc_owner_tlist_init(&rms->rms_owners);
	M0_RETURN(0);
}

static void rms_fini(struct m0_reqh_service *service)
{
	struct m0_reqh_rm_service *rms;

	M0_ENTRY();
	M0_PRE(service != NULL);

	rms = bob_of(service, struct m0_reqh_rm_service, rms_svc, &rms_bob);
	rmsvc_owner_tlist_fini(&rms->rms_owners);
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

	/** Register various resource types */
	m0_file_lock_type_register(&rms->rms_dom);

	M0_RETURN(0);
}

static void rms_stop(struct m0_reqh_service *service)
{
	struct m0_reqh_rm_service *rms;
	struct m0_rm_owner        *owner;
	struct m0_rm_remote       *remote;

	M0_PRE(service != NULL);
	M0_ENTRY();

	rms = bob_of(service, struct m0_reqh_rm_service, rms_svc, &rms_bob);

	m0_tl_for (rmsvc_owner, &rms->rms_owners, owner) {
		struct m0_rm_resource *res = owner->ro_resource;

		m0_rm_owner_windup(owner);
		m0_rm_owner_timedwait(owner, ROS_FINAL, M0_TIME_NEVER);
		m0_tl_teardown(m0_remotes,
			       &owner->ro_resource->r_remote, remote) {
			m0_rm_remote_fini(remote);
			m0_free(remote);
		}
		m0_rm_resource_del(owner->ro_resource);
		m0_rm_owner_fini(owner);
		rmsvc_owner_tlink_del_fini(owner);
		m0_rm_resource_free(res);
		m0_free(owner);
	} m0_tl_endfor;

	m0_file_lock_type_deregister();
	m0_rm_domain_fini(&rms->rms_dom);

	M0_LEAVE();
}

static struct m0_rm_owner *
rmsvc_owner_lookup(const struct m0_reqh_rm_service *rms,
		   const struct m0_rm_resource     *res)
{
	struct m0_rm_owner         *scan;
	struct m0_rm_resource_type *rt;

	M0_PRE(res != NULL);
	M0_PRE(rms != NULL);

	rt = res->r_type;
	M0_ASSERT(rt->rt_ops->rto_eq != NULL);

	m0_tl_for (rmsvc_owner, &rms->rms_owners, scan) {
		M0_ASSERT(scan != NULL);
		if (rt->rt_ops->rto_eq(res, scan->ro_resource))
			break;
	} m0_tl_endfor;
	return scan;
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
		return rc;

	rtype = m0_rm_resource_type_lookup(&rms->rms_dom, rtype_id);
	if (rtype == NULL)
		return -EINVAL;
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

		owner = rmsvc_owner_lookup(rms, resource);
		if (owner == NULL) {
			RM_ALLOC_PTR(owner, RMSVC_OWNER_ALLOC,
				     &m0_rm_addb_ctx);
			if (owner == NULL) {
				rc = -ENOMEM;
				goto err_resource;
			} else {
				m0_rm_owner_init(owner, resource, NULL);

				RM_ALLOC_PTR(ow_cr, OWNER_CREDIT_ALLOC,
					     &m0_rm_addb_ctx);
				if (ow_cr == NULL) {
					rc = -ENOMEM;
					goto err_owner;
				}
				m0_rm_credit_init(ow_cr, owner);
				ow_cr->cr_ops->cro_initial_capital(ow_cr);
				rc = m0_rm_owner_selfadd(owner, ow_cr);
				m0_free(ow_cr);
				if (rc != 0)
					goto err_credit;
				rmsvc_owner_tlink_init_at_tail(owner,
						&rms->rms_owners);
			}
		}
		*out = owner;
	}

	M0_RETURN(rc);

err_credit:
	m0_rm_credit_fini(ow_cr);
	m0_free(ow_cr);
err_owner:
	m0_rm_owner_fini(owner);
err_resource:
	m0_rm_resource_del(resource);


	M0_RETURN(rc);
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
	if (m0_addb_counter_nr(&counter) > 0) {				\
		M0_ADDB_POST_CNTR(&m0_addb_gmc,                         \
				  M0_ADDB_CTX_VEC(&m0_rm_addb_ctx),	\
				  &counter);				\
	}

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
