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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 5-Jun-2013
 */

#pragma once
#ifndef __MERO_BE_UT_HELPER_H__
#define __MERO_BE_UT_HELPER_H__

#include "lib/types.h"		/* bool */
#include "lib/buf.h"		/* m0_buf */
#include "sm/sm.h"		/* m0_sm */

#include "be/be.h"		/* m0_be */
#include "be/domain.h"		/* m0_be_domain */
#include "be/seg.h"		/* m0_be_seg */

struct m0_be_ut_sm_group_thread;
struct m0_stob;

struct m0_be_ut_backend {
	struct m0_be_domain		  but_dom;
	struct m0_be_domain_cfg		  but_dom_cfg;
	struct m0_be_ut_sm_group_thread **but_sgt;
	size_t				  but_sgt_size;
	struct m0_mutex			  but_sgt_lock;
	bool				  but_sm_groups_unlocked;
};

/*
 * Fill cfg with default configuration.
 * @note bec_group_fom_reqh is not set here
 */
void m0_be_ut_backend_cfg_default(struct m0_be_domain_cfg *cfg);

void m0_be_ut_backend_init(struct m0_be_ut_backend *ut_be);
void m0_be_ut_backend_mkfs_init(struct m0_be_ut_backend *ut_be);
void m0_be_ut_backend_fini(struct m0_be_ut_backend *ut_be);

M0_INTERNAL void m0_be_ut_backend_init_cfg(struct m0_be_ut_backend *ut_be,
					   struct m0_be_domain_cfg *cfg);

M0_INTERNAL struct m0_reqh *m0_be_ut_reqh_get(void);
M0_INTERNAL void m0_be_ut_reqh_put(struct m0_reqh *reqh);

struct m0_sm_group *
m0_be_ut_backend_sm_group_lookup(struct m0_be_ut_backend *ut_be);
void m0_be_ut_backend_new_grp_lock_state_set(struct m0_be_ut_backend *ut_be,
					     bool unlocked_new);

void m0_be_ut_backend_thread_exit(struct m0_be_ut_backend *ut_be);

/* will work with single thread only */
void m0_be_ut_tx_init(struct m0_be_tx *tx, struct m0_be_ut_backend *ut_be);

struct m0_be_ut_seg {
	/** Stob for segment */
	struct m0_stob		*bus_stob;
	/** Segment to test */
	struct m0_be_seg	 bus_seg;
	/** Pointer to m0_be_ut_seg.bus_seg allocator */
	struct m0_be_allocator	*bus_allocator;
	void			*bus_copy;
};

void m0_be_ut_seg_init(struct m0_be_ut_seg *ut_seg,
		       struct m0_be_ut_backend *ut_be,
		       m0_bcount_t size);
void m0_be_ut_seg_fini(struct m0_be_ut_seg *ut_seg);
void m0_be_ut_seg_check_persistence(struct m0_be_ut_seg *ut_seg);
void m0_be_ut_seg_reload(struct m0_be_ut_seg *ut_seg);

/*
 * tx capturing checker for UT.
 *
 * Use case example:
 *
 * @code
 * struct m0_be_ut_txc tc;
 * struct m0_be_tx     tx;
 * struct m0_be_seg   *seg;
 * ...
 * // it can be initialized at any time before using
 * m0_be_ut_txc_init(&tc);
 * ...
 * // tx is successfully open
 * // seg is open
 * // note: m0_be_tx_capture() may be called before m0_be_ut_txc_start() - in
 * // this case m0_be_ut_txc_start() will check if captured regions weren't
 * // changed after capturing
 * m0_be_ut_txc_start(&tc, &tx, seg);
 * ...
 * // call m0_be_tx_capture(&tx, ...)
 * ...
 * // perform capturing checks
 * m0_be_ut_txc_check(&tc, &tx);
 * ...
 * // be sure to finalize m0_be_ut_txc
 * m0_be_ut_txc_fini(&tc);
 * @endcode
 */
struct m0_be_ut_txc {
	struct m0_buf butc_seg_copy;
};

M0_INTERNAL void m0_be_ut_txc_init(struct m0_be_ut_txc *tc);
M0_INTERNAL void m0_be_ut_txc_fini(struct m0_be_ut_txc *tc);

/*
 * Start capturing checking.
 *
 * @param tc capturing checker object
 * @param tx transaction to check
 * @param seg tx should capture only this segment data
 *
 * - can be called at any time between m0_be_tx_open() and m0_be_tx_close();
 * - will create copy of the segment;
 * - will perform all m0_be_ut_txc_check() checks.
 */
M0_INTERNAL void m0_be_ut_txc_start(struct m0_be_ut_txc *tc,
				    struct m0_be_tx *tx,
				    const struct m0_be_seg *seg);
/*
 * Check capturing correctness.
 *
 * This function will check if
 * - transaction reg_area have the same data for capturing regions as segment;
 * - all segment modifications from previous call to m0_be_ut_txc_start() were
 *   captured.
 *
 * @note This function may have problems with volatile-only regions.
 */
M0_INTERNAL void m0_be_ut_txc_check(struct m0_be_ut_txc *tc,
				    struct m0_be_tx *tx);

/* m0_be_allocator_{init,create,open} */
void m0_be_ut_seg_allocator_init(struct m0_be_ut_seg *ut_seg,
				 struct m0_be_ut_backend *ut_be);
/* m0_be_allocator_{close,destroy,fini} */
void m0_be_ut_seg_allocator_fini(struct m0_be_ut_seg *ut_seg,
				 struct m0_be_ut_backend *ut_be);

void m0_be_ut__seg_allocator_init(struct m0_be_seg *seg,
				  struct m0_be_ut_backend *ut_be);
void m0_be_ut__seg_allocator_fini(struct m0_be_seg *seg,
				  struct m0_be_ut_backend *ut_be);

M0_INTERNAL int m0_be_ut__seg_dict_create(struct m0_be_seg   *seg,
					  struct m0_sm_group *grp);

M0_INTERNAL int m0_be_ut__seg_dict_destroy(struct m0_be_seg   *seg,
					   struct m0_sm_group *grp);

extern struct m0_be_0type m0_be_ut_seg0;
extern struct m0_be_0type m0_be_ut_log0;
extern struct m0_be_0type m0_be_log0;
extern struct m0_be_0type m0_be_seg0;

struct m0_be_0type_log_opts {
	uint64_t    lo_stob_key;
	m0_bcount_t lo_size;
};


#endif /* __MERO_BE_UT_HELPER_H__ */

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
