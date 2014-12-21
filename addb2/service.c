/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita.danilov@seagate.com>
 * Original creation date: 13-Feb-2015
 */

/**
 * @addtogroup addb2
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_ADDB

#include "lib/trace.h"
#include "lib/assert.h"
#include "lib/chan.h"
#include "lib/memory.h"
#include "lib/misc.h"                   /* M0_AMB */
#include "addb/addb.h"

#include "addb2/addb2.h"
#include "addb2/internal.h"
#include "addb2/consumer.h"
#include "addb2/net.h"

#undef M0_TRACE_SUBSYSTEM

struct addb2_service {
	struct m0_reqh_service   ase_service;
	struct m0_addb2_storgae *ase_stor;
	struct m0_addb2_source   ase_src;
};

struct addb2_fom {
	struct m0_fom             a2_fom;
	struct m0_addb2_trace_obj a2_obj;
	struct m0_chan            a2_chan;
	struct m0_addb2_cursor    a2_cur;
};

static int    addb2_service_start(struct m0_reqh_service *service);
static void   addb2_service_stop(struct m0_reqh_service *service);
static void   addb2_service_fini(struct m0_reqh_service *service);
static void   addb2_trace_done(struct m0_addb2_trace_obj *obj);
static size_t addb2_fom_home_locality(const struct m0_fom *fom);
static int    addb2_service_type_allocate(struct m0_reqh_service **service,
				      const struct m0_reqh_service_type *stype);

static const struct m0_reqh_service_ops       addb2_service_ops;
static const struct m0_reqh_service_type_ops  addb2_service_type_ops;
static const struct m0_fom_ops                addb2_fom_ops;
static const struct m0_fom_type_ops           addb2_fom_type_ops;

enum {
	M0_ADDB_CTXID_ADDB2_SVC = 2000,
	M0_ADDB_CTXID_ADDB2_FOM = 2001,
};

M0_ADDB_CT(m0_addb_ct_addb2_svc, M0_ADDB_CTXID_ADDB2_SVC, "hi", "low");
M0_ADDB_CT(m0_addb_ct_addb2_fom, M0_ADDB_CTXID_ADDB2_FOM);

M0_INTERNAL int m0_addb2_service_module_init(void)
{
	m0_addb_ctx_type_register(&m0_addb_ct_addb2_svc);
	m0_addb_ctx_type_register(&m0_addb_ct_addb2_fom);
	return m0_reqh_service_type_register(&m0_addb2_service_type);
}

M0_INTERNAL void m0_addb2_service_module_fini(void)
{
	m0_reqh_service_type_unregister(&m0_addb2_service_type);
}

static int addb2_service_start(struct m0_reqh_service *svc)
{
	M0_PRE(m0_reqh_service_state_get(svc) == M0_RST_STARTING);
	return 0;
}

static void addb2_service_stop(struct m0_reqh_service *svc)
{
	struct addb2_service *service = M0_AMB(svc, service, ase_service);

	M0_PRE(m0_reqh_service_state_get(svc) == M0_RST_STOPPED);
	m0_addb2_storage_stop(service->ase_service);
}

static void addb2_service_fini(struct m0_reqh_service *svc)
{
	struct addb2_service *service = M0_AMB(svc, service, ase_service);

	M0_PRE(m0_reqh_service_state_get(svc) == M0_RST_STOPPED);
	m0_addb2_storage_fini(service->ase_service);
	m0_addb2_source_fini(&service->ase_src);
	m0_free(service);
}

static int addb2_service_type_allocate(struct m0_reqh_service **svc,
				       const struct m0_reqh_service_type *stype)
{
	struct addb2_service *service;

	M0_ALLOC_PTR(service);
	if (service != NULL) {
		service->ase_stor = m0_addb2_storage_init(stob, size,
							  last,
							  ops);
		if (service->ase_stor != NULL) {
			*svc = &service->ase_reqhs;
			(*svc)->rs_type = stype;
			(*svc)->rs_ops  = &addb2_service_ops;
			m0_addb2_source_init(&service->ase_src);
			return M0_RC(0);
		}
	}
	return M0_ERR(-ENOMEM);
}

static int addb2_fom_create(struct m0_fop *fop,
			    struct m0_fom **out, struct m0_reqh *reqh)
{
	struct addb2_fom *fom;

	M0_ALLOC_PTR(fom);
	if (fom != NULL) {
		*out = &fom->a2_fom;
		m0_chan_init(&fom->a2_chan);
		m0_fom_init(*out, &fop->f_type->ft_fom_type,
			    &addb2_fom_ops, fop, NULL, reqh);
		return M0_RC(0);
	} else
		return M0_RC(-ENOMEM);
}

enum {
	ADDB2_CONSUME = M0_FOM_PHASE_INIT,
	ADDB2_FINISH  = M0_FOM_PHASE_FINISH,
	ADDB2_SUBMIT,
	ADDB2_DONE
};

static int addb2_fom_tick(struct m0_fom *fom0)
{
	struct addb2_fom          *fom     = M0_AMB(fom, fom0, a2_fom);
	struct m0_addb2_trace_obj *obj     = &fom->a2_obj;
	struct m0_addb2_trace     *trace   = m0_fop_data(fom->fo_fop);
	struct addb2_service      *service = M0_AMB(service, fom0->fo_service,
						    ase_service);
	struct m0_addb2_storage   *stor    = service->ase_stor;
	struct m0_addb2_source    *src     = &service->ase_src;

	trace = m0_fop_data(fom->fo_fop);
	switch (m0_fom_phase(fom)) {
	case ADDB2_CONSUME:
		m0_addb2_cursor_init(&fom->a2_cur, trace);
		while (m0_addb2_cursor_next(&fom->a2_cur) > 0)
			m0_addb2_consume(src, &cur->cu_rec);
		m0_addb2_cursor_fini(&fom->a2_cur);
		m0_fom_phase_set(fom0, ADDB2_SUBMIT);
		return M0_FSO_AGAIN;
	case ADDB2_SUBMIT:
		obj->o_tr   = *trace;
		obj->o_done = &addb2_trace_done;
		m0_fom_wait_on(fom0, &fom->a2_chan, &fom0->fo_cb);
		m0_addb2_storage_submit(stor, obj);
		m0_fom_phase_set(fom0, ADDB2_DONE);
		return M0_FSO_WAIT;
	case ADDB2_DONE:
		m0_fom_phase_set(fom0, ADDB2_FINISH);
		return M0_FSO_WAIT;
	default:
		M0_IMPOSSIBLE("Invalid phase");
	}
}

static void addb2_fom_fini(struct m0_fom *fom0)
{
	struct addb2_fom *fom = M0_AMB(fom, fom0, a2_fom);

	m0_fom_fini(fom0);
	m0_chan_fini(&fom->a2_chan);
	m0_free(fom);
}

static void addb2_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
	M0_ADDB_CTX_INIT(mc, &fom->fo_addb_ctx, &m0_addb_ct_addb2_fom,
			 &fom->fo_service->rs_addb_ctx);
}

static void addb2_trace_done(struct m0_addb2_trace_obj *obj)
{
	struct addb2_fom *fom = M0_AMB(fom, obj, a2_obj.o_tr);
	m0_chan_signal(&fom->a2_chan);
}

static size_t addb2_fom_home_locality(const struct m0_fom *fom)
{
	static size_t seq = 0;
	return seq++;
}

static const struct m0_fom_ops addb2_fom_ops = {
	.fo_tick          = &addb2_fom_tick,
	.fo_home_locality = &addb2_fom_home_locality,
	.fo_addb_init     = &addb2_fom_addb_init,
	.fo_fini          = &addb2_fom_fini
};

static const struct m0_fom_type_ops addb2_fom_type_ops = {
	.fto_create = &addb2_fom_create
};

static const struct m0_reqh_service_type_ops addb2_service_type_ops = {
	.rsto_service_allocate = &addb2_service_type_service_allocate
};

static const struct m0_reqh_addb2_service_ops addb2_service_ops = {
	.rso_start = &addb2_service_start,
	.rso_stop  = &addb2_service_stop,
	.rso_fini  = &addb2_service_fini
};

M0_REQH_SERVICE_TYPE_DEFINE(m0_addb2_service_type, &addb2_service_type_ops,
			    "addb2", &m0_addb_ct_addb2_svc, 2);

/** @} end of addb2 group */

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
