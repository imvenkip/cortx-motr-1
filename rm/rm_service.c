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
#include "rm/ut/rings.h"
#include "rm/ut/rings.c"

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
	int                         rc = 0;
	struct m0_reqh_rm_service  *rms;
	struct m0_rm_resource_type *rtype;

	M0_PRE(service != NULL);
	M0_ENTRY();

	rms = bob_of(service, struct m0_reqh_rm_service, rms_svc, &rms_bob);

	m0_rm_domain_init(&rms->rms_dom);
	RM_ALLOC_PTR(rtype, RESOURCE_TYPE_ALLOC, &m0_rm_addb_ctx);
	if (rtype == NULL) {
		rc = -ENOMEM;
		goto err;
	}
	rc = m0_rm_type_register(&rms->rms_dom, rtype);
	rtype->rt_ops = &rings_rtype_ops;
	M0_RETURN(0);
err:
	m0_rm_domain_fini(&rms->rms_dom);
	M0_RETURN(rc);
}

static void rms_stop(struct m0_reqh_service *service)
{
	int                        i;
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
		m0_tl_for(m0_remotes, &owner->ro_resource->r_remote, remote) {
			m0_remotes_tlist_del(remote);
			m0_rm_remote_fini(remote);
			m0_free(remote);
		} m0_tl_endfor;
		m0_rm_resource_del(owner->ro_resource);
		m0_rm_owner_fini(owner);
		rmsvc_owner_tlink_del_fini(owner);
		m0_rm_resource_free(res);
		m0_free(owner);
	} m0_tl_endfor;

	for (i = 0; i < ARRAY_SIZE(rms->rms_dom.rd_types); ++i) {
		struct m0_rm_resource_type *rtype = rms->rms_dom.rd_types[i];

		if (rtype != NULL) {
			m0_rm_type_deregister(rtype);
			m0_free(rtype);
		}
	}
	m0_rm_domain_fini(&rms->rms_dom);

	M0_LEAVE();
}

M0_INTERNAL int m0_rm_svc_owner_create(struct m0_reqh_service *service,
				       struct m0_rm_owner    **owner,
				       struct m0_buf          *resbuf)
{
	int                         rc;
	struct m0_reqh_rm_service  *rms;
	uint64_t                    rtype_id;
	struct m0_rm_resource      *resource;
	struct m0_rm_resource_type *rtype;
	struct m0_bufvec_cursor     cursor;
	struct m0_rm_credit        *owner_credit = NULL;
	struct m0_bufvec            datum_buf =
		M0_BUFVEC_INIT_BUF(&resbuf->b_addr,
				   &resbuf->b_nob);

	M0_PRE(service != NULL);
	M0_PRE(*owner != NULL);

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
		resource->r_type = rtype;
		m0_rm_resource_add(rtype, resource);
		m0_rm_owner_init(*owner, resource, NULL);

		RM_ALLOC_PTR(owner_credit, OWNER_CREDIT_ALLOC, &m0_rm_addb_ctx);
		if (owner_credit == NULL) {
			rc = -ENOMEM;
			goto err_owner;
		}
		m0_rm_credit_init(owner_credit, *owner);
		owner_credit->cr_ops->cro_initial_capital(owner_credit);
		rc = m0_rm_owner_selfadd(*owner, owner_credit);
		m0_free(owner_credit);
		if (rc != 0)
			goto err_credit;
		rmsvc_owner_tlink_init_at_tail(*owner, &rms->rms_owners);
	}

	M0_RETURN(rc);

err_credit:
	m0_rm_credit_fini(owner_credit);
	m0_free(owner_credit);
err_owner:
	m0_rm_owner_fini(*owner);
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
		struct rm_addb_stats       *as = &rt->rt_addb_stats;

		for (j = 0; j < ARRAY_SIZE(as->as_req); ++j) {
			CNTR_POST(as->as_req[j].rs_nr);
			CNTR_POST(as->as_req[j].rs_time);
			as->as_req[j].rs_count = 0;
		}
		CNTR_POST(as->as_credit_time);
	}
#undef CNTR_POST
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
