/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Egor Nikulenkov <egor.nikulenkov@seagate.com>
 * Original creation date: 23-Jun-2016
 */

#pragma once

#ifndef __MERO_DIX_LAYOUT_H__
#define __MERO_DIX_LAYOUT_H__


#include "lib/types.h"
#include "fid/fid.h"
#include "fid/fid_xc.h"
#include "dix/imask.h"
#include "dix/imask_xc.h"
#include "lib/buf.h"

/**
 * @addtogroup dix
 *
 * @{
 *
 * Distributed index layout is based on parity de-clustering layout and
 * determines targets (pool disks) for index records.
 */

/* Import */
struct m0_pdclust_layout;
struct m0_pdclust_instance;
struct m0_pool_version;
struct m0_layout_domain;

enum dix_layout_type {
	DIX_LTYPE_UNKNOWN,
	DIX_LTYPE_ID,
	DIX_LTYPE_DESCR,
};

enum m0_dix_hash_fnc_type {
	HASH_FNC_NONE,
	HASH_FNC_FNV1,
	HASH_FNC_CITY
};

struct m0_dix_ldesc {
	uint32_t            ld_hash_fnc;
	struct m0_fid       ld_pver;
	struct m0_dix_imask ld_imask;
} M0_XCA_RECORD;

struct m0_dix_layout {
	uint32_t dl_type;
	union {
		uint64_t            dl_id   M0_XCA_TAG("DIX_LTYPE_ID");
		struct m0_dix_ldesc dl_desc M0_XCA_TAG("DIX_LTYPE_DESCR");
	} u;
} M0_XCA_UNION;

struct m0_dix_linst {
	struct m0_dix_ldesc        *li_ldescr;
	struct m0_pdclust_layout   *li_pl;
	struct m0_pdclust_instance *li_pi;
};

/**
 * Iterator over targets of index record parity group units.
 *
 * The order of iteration is:
 * Tn, Tp1 ... Tpk, Ts1, ..., Tsk,
 * where Tn - target for data unit. There is always one data unit;
 *       Tp1 ... Tpk - targets for parity units;
 *       Ts1 ... Tsk - targets for spare units;
 *       'k' is determined by pool version 'K' attribute.
 * Iterator constructs distributed index layout internally and uses it to
 * calculate successive target on every iteration.
 *
 * Target in this case is a device index in a pool version.
 */
struct m0_dix_layout_iter {
	/** Layout instance. */
	struct m0_dix_linst dit_linst;

	/** Width of a parity group. */
	uint32_t            dit_W;

	/** Current position. */
	uint64_t            dit_unit;

	/**
	 * Key of the record that should be distributed after application of the
	 * identity mask.
	 */
	struct m0_buf       dit_key;
};

M0_INTERNAL void m0_dix_target(struct m0_dix_linst *inst,
			       uint64_t             unit,
			       struct m0_buf       *key,
			       uint64_t            *out_id);

M0_INTERNAL uint32_t m0_dix_devices_nr(struct m0_dix_linst *linst);
M0_INTERNAL struct m0_pooldev *m0_dix_tgt2sdev(struct m0_dix_linst *linst,
					       uint64_t             tgt);

M0_INTERNAL int m0_dix_layout_init(struct m0_dix_linst     *dli,
				   struct m0_layout_domain *domain,
				   const struct m0_fid     *fid,
				   uint64_t                 layout_id,
				   struct m0_pool_version  *pver,
				   struct m0_dix_ldesc     *dld);

M0_INTERNAL void m0_dix_layout_fini(struct m0_dix_linst *dli);

M0_INTERNAL int m0_dix_ldesc_init(struct m0_dix_ldesc       *ld,
				  struct m0_ext             *range,
				  m0_bcount_t                nr,
				  enum m0_dix_hash_fnc_type  htype,
				  struct m0_fid             *pver);

M0_INTERNAL int m0_dix_ldesc_copy(struct m0_dix_ldesc       *dst,
				  const struct m0_dix_ldesc *src);

M0_INTERNAL void m0_dix_ldesc_fini(struct m0_dix_ldesc *ld);

M0_INTERNAL
int m0_dix_layout_iter_init(struct m0_dix_layout_iter *iter,
			    const struct m0_fid       *index,
			    struct m0_layout_domain   *ldom,
			    struct m0_pool_version    *pver,
			    struct m0_dix_ldesc       *ldesc,
			    struct m0_buf             *key);

M0_INTERNAL void m0_dix_layout_iter_next(struct m0_dix_layout_iter *iter,
					 uint64_t                  *tgt);

M0_INTERNAL void m0_dix_layout_iter_goto(struct m0_dix_layout_iter *iter,
					 uint64_t                   unit_nr);
M0_INTERNAL void m0_dix_layout_iter_reset(struct m0_dix_layout_iter *iter);

M0_INTERNAL void m0_dix_layout_iter_get_at(struct m0_dix_layout_iter *iter,
					   uint64_t                   unit,
					   uint64_t                  *tgt);

M0_INTERNAL uint32_t m0_dix_liter_N(struct m0_dix_layout_iter *iter);
M0_INTERNAL uint32_t m0_dix_liter_P(struct m0_dix_layout_iter *iter);
M0_INTERNAL uint32_t m0_dix_liter_K(struct m0_dix_layout_iter *iter);
M0_INTERNAL uint32_t m0_dix_liter_W(struct m0_dix_layout_iter *iter);
M0_INTERNAL uint32_t m0_dix_liter_spare_offset(struct m0_dix_layout_iter *iter);

M0_INTERNAL
uint32_t m0_dix_liter_unit_classify(struct m0_dix_layout_iter *iter,
				    uint64_t                   unit);

M0_INTERNAL void m0_dix_layout_iter_fini(struct m0_dix_layout_iter *iter);

M0_INTERNAL bool m0_dix_layout_eq(const struct m0_dix_layout *layout1,
				  const struct m0_dix_layout *layout2);

/** @} end of dix group */
#endif /* __MERO_DIX_LAYOUT_H__ */

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
