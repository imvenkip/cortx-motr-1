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

#include "lib/rwlock.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/time.h"
#include "lib/misc.h" /* M0_SET_ARR0 */
#include "lib/trace.h" /* m0_console_printf */
#include "lib/finject.h" /* M0_FI_ENABLED */
#include "lib/uuid.h"
#include "lib/lockers.h"
#include "reqh/reqh.h"
#include "reqh/reqh_service.h"
#include "mero/magic.h"

/**
   @addtogroup reqhservice
   @{
 */

/**
   static global list of service types.
   Holds struct m0_reqh_service_type instances linked via
   m0_reqh_service_type::rst_linkage.

   @see struct m0_reqh_service_type
 */
static struct m0_tl rstypes;

/** Protects access to list rstypes. */
static struct m0_rwlock rstypes_rwlock;

M0_TL_DESCR_DEFINE(rstypes, "reqh service types", static,
                   struct m0_reqh_service_type, rst_linkage, rst_magix,
                   M0_REQH_SVC_TYPE_MAGIC, M0_REQH_SVC_HEAD_MAGIC);

M0_TL_DEFINE(rstypes, static, struct m0_reqh_service_type);

static struct m0_bob_type rstypes_bob;
M0_BOB_DEFINE(static, &rstypes_bob, m0_reqh_service_type);

M0_INTERNAL bool m0_reqh_service_invariant(const struct m0_reqh_service *svc)
{
	return m0_reqh_service_bob_check(svc) &&
	M0_IN(svc->rs_state, (M0_RST_INITIALISING, M0_RST_INITIALISED,
				M0_RST_STARTING, M0_RST_STARTED,
				M0_RST_STOPPING)) &&
	svc->rs_type != NULL && svc->rs_ops != NULL &&
	ergo(M0_IN(svc->rs_state, (M0_RST_INITIALISED, M0_RST_STARTING,
				   M0_RST_STARTED, M0_RST_STOPPING)),
	     svc->rs_uuid != 0 && svc->rs_reqh != NULL) &&
	ergo(M0_IN(svc->rs_state, (M0_RST_STARTED, M0_RST_STOPPING)),
	     m0_reqh_svc_tlist_contains(&svc->rs_reqh->rh_services, svc));
}
M0_EXPORTED(m0_reqh_service_invariant);

M0_INTERNAL struct m0_reqh_service_type *
m0_reqh_service_type_find(const char *sname)
{
	struct m0_reqh_service_type *t;

	M0_PRE(sname != NULL);

	m0_rwlock_read_lock(&rstypes_rwlock);
        m0_tl_for(rstypes, &rstypes, t) {
		M0_ASSERT(m0_reqh_service_type_bob_check(t));
                if (strcmp(t->rst_name, sname) == 0)
			break;
        } m0_tl_endfor;
	m0_rwlock_read_unlock(&rstypes_rwlock);

        return t;
}

M0_INTERNAL int m0_reqh_service_allocate(struct m0_reqh_service **service,
					 struct m0_reqh_service_type *stype,
					 const char *arg)
{
	int rc;

	M0_PRE(service != NULL && stype != NULL);

        rc = stype->rst_ops->rsto_service_allocate(service, stype, arg);
        if (rc == 0) {
		(*service)->rs_type = stype;
		m0_reqh_service_bob_init(*service);
		M0_POST(m0_reqh_service_invariant(*service));
	}
	return rc;
}

M0_INTERNAL int m0_reqh_service_start(struct m0_reqh_service *service)
{
	int             rc;
	unsigned        key;
	struct m0_reqh *reqh;

	M0_PRE(m0_reqh_service_invariant(service));

	reqh = service->rs_reqh;
	service->rs_state = M0_RST_STARTING;

	/**
	 * NOTE: The key is required to be set before 'rso_start'
	 * as some services can call m0_fom_init() directly in
	 * their service start, m0_fom_init() finds the service
	 * given reqh, using this key
	 */
	m0_rwlock_write_lock(&reqh->rh_rwlock);
	key = service->rs_type->rst_key;
	M0_ASSERT(m0_reqh_lockers_is_empty(reqh, key));
	m0_reqh_lockers_set(reqh, key, service);
	m0_rwlock_write_unlock(&reqh->rh_rwlock);

	rc = service->rs_ops->rso_start(service);

	m0_rwlock_write_lock(&reqh->rh_rwlock);
	if (rc == 0) {
		m0_reqh_svc_tlist_add_tail(&reqh->rh_services, service);
		service->rs_state = M0_RST_STARTED;
		M0_ASSERT(m0_reqh_service_invariant(service));
        } else {
		M0_ASSERT(m0_reqh_lockers_get(reqh, key) == service);
		m0_reqh_lockers_clear(reqh, key);
		service->rs_state = M0_RST_FAILED;
	}
	m0_rwlock_write_unlock(&reqh->rh_rwlock);

	return rc;
}

M0_INTERNAL void
m0_reqh_service_prepare_to_stop(struct m0_reqh_service *service)
{
	M0_PRE(m0_reqh_service_invariant(service));
	M0_PRE(service->rs_state == M0_RST_STARTED);

	if (service->rs_ops->rso_prepare_to_stop != NULL)
		service->rs_ops->rso_prepare_to_stop(service);
	service->rs_state = M0_RST_STOPPING;
}

M0_INTERNAL void m0_reqh_service_stop(struct m0_reqh_service *service)
{
	struct m0_reqh *reqh;
	unsigned        key;

	M0_ASSERT(m0_reqh_service_invariant(service));
	M0_ASSERT(M0_IN(service->rs_state, (M0_RST_STARTED, M0_RST_STOPPING)));

	if (service->rs_state != M0_RST_STOPPING)
		m0_reqh_service_prepare_to_stop(service);

	reqh = service->rs_reqh;
	service->rs_ops->rso_stop(service);
	m0_rwlock_write_lock(&reqh->rh_rwlock);
	m0_reqh_svc_tlist_del(service);
	service->rs_state = M0_RST_STOPPED;
	key = service->rs_type->rst_key;
	M0_ASSERT(m0_reqh_lockers_get(reqh, key) == service);
	m0_reqh_lockers_clear(reqh, key);
	m0_rwlock_write_unlock(&reqh->rh_rwlock);
}

M0_INTERNAL void m0_reqh_service_init(struct m0_reqh_service *service,
				      struct m0_reqh *reqh)
{
	struct m0_addb_ctx_type *serv_addb_ct;

	M0_PRE(service != NULL && reqh != NULL &&
		service->rs_state == M0_RST_INITIALISING);

	serv_addb_ct = service->rs_type->rst_addb_ct;
	M0_ASSERT(m0_addb_ctx_type_lookup(serv_addb_ct->act_id) != NULL);

	/**
	    act_cf_nr is 2 for all service ctx types,
	    1 for "hi" & 2 for "low"
	 */
	M0_ASSERT(serv_addb_ct->act_cf_nr == 2);

	service->rs_uuid = m0_uuid_generate();
	service->rs_state = M0_RST_INITIALISED;
	service->rs_reqh  = reqh;
	m0_reqh_svc_tlink_init(service);
	m0_mutex_init(&service->rs_mutex);

	/** @todo: Need to pass the service uuid "hi" & "low"
	   once available
	*/
	if (m0_addb_mc_is_fully_configured(&reqh->rh_addb_mc))
		M0_ADDB_CTX_INIT(&reqh->rh_addb_mc, &service->rs_addb_ctx,
				 serv_addb_ct,
				 &reqh->rh_addb_ctx,
				 0, 0);
	else /** This happens in UT, where no ADDB stob is specified */
		M0_ADDB_CTX_INIT(&m0_addb_gmc, &service->rs_addb_ctx,
				 serv_addb_ct,
				 &reqh->rh_addb_ctx,
				 0, 0);

	M0_POST(m0_reqh_service_invariant(service));
}

M0_INTERNAL void m0_reqh_service_fini(struct m0_reqh_service *service)
{
	M0_PRE(service != NULL && (service->rs_state == M0_RST_STOPPED ||
		service->rs_state == M0_RST_FAILED) &&
		m0_reqh_service_bob_check(service));

	m0_addb_ctx_fini(&service->rs_addb_ctx);
	m0_reqh_service_bob_fini(service);
	m0_reqh_svc_tlink_fini(service);
	m0_mutex_fini(&service->rs_mutex);
	service->rs_ops->rso_fini(service);
}

int m0_reqh_service_type_register(struct m0_reqh_service_type *rstype)
{
        M0_PRE(rstype != NULL);
	M0_PRE(!m0_reqh_service_is_registered(rstype->rst_name));
	M0_PRE(rstype->rst_addb_ct != NULL);

	if (M0_FI_ENABLED("fake_error"))
		return -EINVAL;

	m0_reqh_service_type_bob_init(rstype);
	m0_rwlock_write_lock(&rstypes_rwlock);
	rstype->rst_key = m0_reqh_lockers_allot();
	rstypes_tlink_init_at_tail(rstype, &rstypes);
	m0_rwlock_write_unlock(&rstypes_rwlock);

	return 0;
}

void m0_reqh_service_type_unregister(struct m0_reqh_service_type *rstype)
{
	M0_PRE(rstype != NULL && m0_reqh_service_type_bob_check(rstype));

	rstypes_tlink_del_fini(rstype);
	m0_reqh_service_type_bob_fini(rstype);
}

M0_INTERNAL int m0_reqh_service_types_length(void)
{
	return rstypes_tlist_length(&rstypes);
}

M0_INTERNAL void m0_reqh_service_list_print(void)
{
	struct m0_reqh_service_type *stype;

        m0_tl_for(rstypes, &rstypes, stype) {
                M0_ASSERT(m0_reqh_service_type_bob_check(stype));
                m0_console_printf(" %s\n", stype->rst_name);
        } m0_tl_endfor;
}

M0_INTERNAL bool m0_reqh_service_is_registered(const char *sname)
{
        return !m0_tl_forall(rstypes, stype, &rstypes,
                                strcasecmp(stype->rst_name, sname) != 0);
}

M0_INTERNAL int m0_reqh_service_types_init(void)
{
	rstypes_tlist_init(&rstypes);
	m0_bob_type_tlist_init(&rstypes_bob, &rstypes_tl);
	m0_rwlock_init(&rstypes_rwlock);

	return 0;
}
M0_EXPORTED(m0_reqh_service_types_init);

M0_INTERNAL void m0_reqh_service_types_fini(void)
{
	rstypes_tlist_fini(&rstypes);
	m0_rwlock_fini(&rstypes_rwlock);
}
M0_EXPORTED(m0_reqh_service_types_fini);

M0_INTERNAL struct m0_reqh_service *
m0_reqh_service_find(const struct m0_reqh_service_type *st,
		     struct m0_reqh                    *reqh)
{
	M0_PRE(st != NULL && reqh != NULL);

	return m0_reqh_lockers_get(reqh, st->rst_key);
}
M0_EXPORTED(m0_reqh_service_find);

/** @} endgroup reqhservice */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
