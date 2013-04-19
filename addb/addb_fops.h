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

#pragma once

#ifndef __MERO_ADDB_ADDB_FOPS_H__
#define __MERO_ADDB_ADDB_FOPS_H__

#include "fop/fop.h"
#include "xcode/xcode_attr.h"
#include "addb/addb_wire.h"

/**
 *      @defgroup addb-fops ADDB FOPs
 *
 *	@{
 */

/**
 * ADDB FOP.
 * It is used to send ADDB records to mero server node running ADDB service.
 */
struct rpcsink_fop;

struct m0_addb_ts_rec_data {
	/** record data */
	struct m0_addb_rec *atrd_data M0_XCA_OPAQUE("m0_addb_rec_xc_type");
} M0_XCA_RECORD;

struct m0_addb_rpc_sink_fop {
	/** Number of ADDB records */
	uint32_t                    arsf_nr;
	/** Sequence of ADDB records */
	struct m0_addb_ts_rec_data *arsf_recs;
} M0_XCA_SEQUENCE;

extern struct m0_fop_type m0_fop_addb_rpc_sink_fopt;

M0_INTERNAL int m0_addb_rec_xc_type(const struct m0_xcode_obj   *par,
				    const struct m0_xcode_type **out);

M0_INTERNAL int m0_addb_rpc_sink_fop_init(struct rpcsink_fop *rsfop,
					  uint32_t nrecs);

M0_INTERNAL void m0_addb_rpc_sink_fop_fini(struct rpcsink_fop *rsfop);

M0_INTERNAL int m0_addb_service_fop_init(void);
M0_INTERNAL void m0_addb_service_fop_fini(void);

/** @} */ /* end of addb_rpcsink group */

#endif
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
