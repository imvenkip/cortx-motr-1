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
 * Original author: Rajanikant Chirmade <Rajanikant_Chirmade@xyratex.com>,
 * Original creation date: 01/24/2013
 */

#include "lib/errno.h"

#include "lib/memory.h"
#include "rpc/rpc_opcodes.h"
#include "fop/fop.h"
#include "fop/fop_item_type.h"
#include "addb/addb_fops.h"
#include "addb/addb_fops_xc.h"

extern struct m0_reqh_service_type m0_addb_svc_type;
extern const struct m0_rpc_item_ops addb_rpc_sink_rpc_item_ops;

struct m0_fop_type m0_fop_addb_rpc_sink_fopt;

M0_INTERNAL int m0_addb_rec_xc_type(const struct m0_xcode_obj   *par,
				    const struct m0_xcode_type **out)
{
	*out = m0_addb_rec_xc;
	return 0;
}

static void m0_addb_rpc_sink_fop_release(struct m0_ref *ref)
{
	struct m0_fop      *fop   = container_of(ref, struct m0_fop, f_ref);
	struct rpcsink_fop *rsfop = rsfop_from_fop(fop);

	m0_addb_rpc_sink_fop_fini(rsfop);
	m0_free(rsfop);
}

M0_INTERNAL int m0_addb_service_fop_init(void)
{
	m0_xc_addb_fops_init();

	/**
	 * @todo Add rpc_ops, when addb rpc items encode/decode functions
	 * would be defined, currently make use of default ones.
	 */
	m0_fop_addb_rpc_sink_fopt.ft_magix = 0;
	return M0_FOP_TYPE_INIT(&m0_fop_addb_rpc_sink_fopt,
				.name      = "ADDB rpcsink fop",
				.opcode    = M0_ADDB_RPC_SINK_FOP_OPCODE,
				.xt        = m0_addb_rpc_sink_fop_xc,
#ifndef __KERNEL__
				.fom_ops   = &addb_fom_type_ops,
				.svc_type  = &m0_addb_svc_type,
				.sm        = &addb_fom_sm_conf,
#endif
				.rpc_flags = M0_RPC_ITEM_TYPE_ONEWAY);
}

M0_INTERNAL void m0_addb_service_fop_fini(void)
{
	m0_fop_type_fini(&m0_fop_addb_rpc_sink_fopt);
	m0_xc_addb_fops_fini();
}

M0_INTERNAL int m0_addb_rpc_sink_fop_init(struct rpcsink_fop *rsfop,
					  uint32_t nrecs)
{
	struct m0_addb_rpc_sink_fop *fop_recs;

	M0_PRE(nrecs > 0);

	M0_ALLOC_PTR(fop_recs);
	if (fop_recs == NULL)
		M0_RETERR(-ENOMEM, "m0_addb_rpc_sink_fop_init");

	M0_ALLOC_ARR(fop_recs->arsf_recs, nrecs);
	if (fop_recs->arsf_recs == NULL) {
		m0_free(fop_recs);
		M0_RETERR(-ENOMEM, "m0_addb_rpc_sink_fop_init");
	}
	fop_recs->arsf_nr = nrecs;

	m0_fop_init(&rsfop->rf_fop, &m0_fop_addb_rpc_sink_fopt,
		    (void *)fop_recs, m0_addb_rpc_sink_fop_release);

	rsfop->rf_fop.f_item.ri_ops      = &addb_rpc_sink_rpc_item_ops;
	rsfop->rf_fop.f_item.ri_deadline = 0;

	return 0;
}

M0_INTERNAL void m0_addb_rpc_sink_fop_fini(struct rpcsink_fop *rsfop)
{
	struct m0_addb_rpc_sink_fop *fop_recs = m0_fop_data(&rsfop->rf_fop);;

	/*
	 * m0_xcode_free() trying to free pointers in
	 * m0_addb_rpc_sink_fop::arsf_recs. These pointers
	 * are from transient store and re-used for other ADDB
	 * records. m0_fop_fini() should not free m0_fop::f_data::fd_data
	 */
	m0_free(fop_recs->arsf_recs);
	m0_free(fop_recs);
	rsfop->rf_fop.f_data.fd_data = NULL;

	m0_fop_fini(&rsfop->rf_fop);
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
