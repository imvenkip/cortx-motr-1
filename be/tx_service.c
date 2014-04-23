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

#undef M0_TRACE_SUBSYSTEM
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#undef M0_ADDB_CT_CREATE_DEFINITION
#define M0_ADDB_CT_CREATE_DEFINITION
#include "addb/addb.h"

#include "be/be.h"
#include "be/tx_service.h"
#include "lib/errno.h"
#include "lib/memory.h"

/**
 * @addtogroup be
 * @{
 */

/* ------------------------------------------------------------------
 * ADDB
 * ------------------------------------------------------------------ */

enum { M0_ADDB_CTXID_TX_SERVICE = 1800 };

M0_ADDB_CT(m0_addb_ct_tx_service, M0_ADDB_CTXID_TX_SERVICE, "hi", "low");

static void _addb_init(void)
{
	const struct m0_addb_ctx_type *act;
	/* static struct m0_addb_ctx tx_service_mod_ctx; */
	/* XXX not thread-safe */
	act = m0_addb_ctx_type_lookup(M0_ADDB_CTXID_TX_SERVICE);
	if (act == NULL) {
		 m0_addb_ctx_type_register(&m0_addb_ct_tx_service);
		 /*M0_ADDB_CTX_INIT(&m0_addb_gmc, &m0_tx_service_mod_addb_ctx,*/
		 /*		 &m0_addb_ct_tx_service, &m0_addb_proc_ctx);*/
	}
}

static void _addb_fini(void)
{
        /* m0_addb_ctx_fini(&tx_service_mod_ctx); */
}

/* ------------------------------------------------------------------
 * TX service
 * ------------------------------------------------------------------ */

/** Transaction service. */
struct tx_service {
	struct m0_reqh_service ts_reqh;
};

static int txs_allocate(struct m0_reqh_service **out,
			struct m0_reqh_service_type *stype,
			struct m0_reqh_context *rctx);

static const struct m0_reqh_service_type_ops txs_stype_ops = {
	.rsto_service_allocate = txs_allocate
};

M0_REQH_SERVICE_TYPE_DEFINE(m0_be_txs_stype, &txs_stype_ops, "be-tx-service",
                            &m0_addb_ct_tx_service, 1);

M0_INTERNAL int m0_be_txs_register(void)
{
	return m0_reqh_service_type_register(&m0_be_txs_stype);
}

M0_INTERNAL void m0_be_txs_unregister(void)
{
	m0_reqh_service_type_unregister(&m0_be_txs_stype);
}

static int  txs_start(struct m0_reqh_service *service);
static void txs_stop(struct m0_reqh_service *service);
static void txs_fini(struct m0_reqh_service *service);

static const struct m0_reqh_service_ops txs_ops = {
	.rso_start = txs_start,
	.rso_stop  = txs_stop,
	.rso_fini  = txs_fini
};

/** Allocates and initialises transaction service. */
static int txs_allocate(struct m0_reqh_service **service,
			struct m0_reqh_service_type *stype,
			struct m0_reqh_context *rctx)
{
	struct tx_service *s;

	M0_ENTRY();
	M0_PRE(stype == &m0_be_txs_stype);

	M0_ALLOC_PTR(s);
	if (s == NULL)
		return M0_RC(-ENOMEM);

	*service = &s->ts_reqh;
	(*service)->rs_ops = &txs_ops;
	_addb_init();

	return M0_RC(0);
}

/** Finalises and deallocates transaction service. */
static void txs_fini(struct m0_reqh_service *service)
{
	M0_ENTRY();
	_addb_fini();
	m0_free(container_of(service, struct tx_service, ts_reqh));
	M0_LEAVE();
}

static int txs_start(struct m0_reqh_service *service)
{
	M0_ENTRY();
	return M0_RC(0);
}

static void txs_stop(struct m0_reqh_service *service)
{
	M0_ENTRY();
	M0_LEAVE();
}

/** @} end of be group */
#undef M0_ADDB_CT_CREATE_DEFINITION
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
