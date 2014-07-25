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
 * Original author: Rajanikant Chirmade <rajanikant_chirmade@xyratex.com>
 * Original creation date: 07/04/2013
 */

#pragma once

#ifndef __MERO_STATS_STATS_FOPS_H__
#define __MERO_STATS_STATS_FOPS_H__

#include "lib/types.h"
#include "xcode/xcode_attr.h"
#include "addb/addb.h"
#include "addb/addb_wire.h"
#include "addb/addb_wire_xc.h"

/**
 * @defgroup stats_fop Stats FOP
 * @{
 */
enum {
	/** Sine stats using same id as corresponding addb rec type id */
	M0_STATS_ID_UNDEFINED = M0_ADDB_RECID_UNDEF
};

struct m0_fop;
struct m0_ref;

extern struct m0_fop_type m0_fop_stats_update_fopt;
extern struct m0_fop_type m0_fop_stats_query_fopt;
extern struct m0_fop_type m0_fop_stats_query_rep_fopt;

/* @note Same fop definations will be defined from monitoring infra
 *       Need to merge these changes properly.
 *       Please remove tis note after merge.
 */

/**
 * Mero nodes send sequence of m0_stats_sum.
 */
struct m0_stats_sum {
	/** Stats id, this is corresponding addb rec type id. */
	uint32_t		  ss_id;
	/** Stats summary data */
	struct m0_addb_uint64_seq ss_data;
} M0_XCA_RECORD;

struct m0_stats_recs {
	/** Stats sequence length */
	uint64_t	      sf_nr;
	/** Stats sequence data */
	struct m0_stats_sum  *sf_stats;
} M0_XCA_SEQUENCE;

/** stats update fop */
struct m0_stats_update_fop {
	/** ADDB stats */
	struct m0_stats_recs suf_stats;
} M0_XCA_RECORD;

/** stats query fop */
struct m0_stats_query_fop {
	/** Stats ids */
	struct m0_addb_uint64_seq sqf_ids;
} M0_XCA_RECORD;

/** stats query reply fop */
struct m0_stats_query_rep_fop {
	int32_t              sqrf_rc;
	/** ADDB stats */
	struct m0_stats_recs sqrf_stats;
} M0_XCA_RECORD;

/**
 * Get stats update fop
 * @param fop pointer to fop
 * @return pointer to stats update fop.
 */
M0_INTERNAL struct m0_stats_update_fop *
m0_stats_update_fop_get(struct m0_fop *fop);

/**
 * Get stats query fop
 * @param fop pointer to fop
 * @return pointer to stats query fop.
 */
M0_INTERNAL struct m0_stats_query_fop *
m0_stats_query_fop_get(struct m0_fop *fop);

/**
 * Get stats query reply fop
 * @param fop pointer to fop
 * @return pointer to stats query reply fop.
 */
M0_INTERNAL struct m0_stats_query_rep_fop *
m0_stats_query_rep_fop_get(struct m0_fop *fop);

/**
 * m0_stats_query_fop_release
 */
M0_INTERNAL void m0_stats_query_fop_release(struct m0_ref *ref);

M0_INTERNAL int  m0_stats_fops_init(void);
M0_INTERNAL void m0_stats_fops_fini(void);

/** @} end group stats_fop */
#endif /* __MERO_STATS_STATS_FOPS_H_ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
