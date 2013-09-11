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

#include "sm/sm.h"		/* m0_sm */
#include "reqh/reqh.h"		/* m0_reqh */
#include "stob/stob.h"		/* m0_stob */

#include "be/be.h"		/* m0_be */
#include "be/domain.h"		/* m0_be_domain */
#include "be/seg.h"		/* m0_be_seg */

#include <sys/types.h>		/* pid_t */

struct m0_be_ut_sm_group_thread {
	struct m0_thread    sgt_thread;
	pid_t		    sgt_tid;
	struct m0_semaphore sgt_stop_sem;
	struct m0_sm_group  sgt_grp;
};

struct m0_be_ut_backend {
	struct m0_reqh			  but_reqh;
	struct m0_be_domain		  but_dom;
	struct m0_be_domain_cfg		  but_dom_cfg;
	struct m0_be_ut_sm_group_thread **but_sgt;
	size_t				  but_sgt_size;
	struct m0_mutex			  but_sgt_lock;
};

/*
 * Fill cfg with default configuration.
 * @note bec_group_fom_reqh is not set here
 */
void m0_be_ut_backend_cfg_default(struct m0_be_domain_cfg *cfg);

void m0_be_ut_backend_init(struct m0_be_ut_backend *ut_be);
void m0_be_ut_backend_fini(struct m0_be_ut_backend *ut_be);

struct m0_reqh *m0_be_ut_reqh_get(void);
void m0_be_ut_reqh_put(struct m0_reqh *reqh);

struct m0_sm_group *
m0_be_ut_backend_sm_group_lookup(struct m0_be_ut_backend *ut_be);

void m0_be_ut_backend_thread_exit(struct m0_be_ut_backend *ut_be);

/* will work with single thread only */
void m0_be_ut_tx_init(struct m0_be_tx *tx, struct m0_be_ut_backend *ut_be);

struct m0_be_ut_seg {
	/**
	 * Stob to test. It can point to m0_be_ut_seg.bus_stob_ if
	 * there is new stob and to existing stob if it isn't new.
	 */
	struct m0_stob		*bus_stob;
	/** Newly created stob. This field is unused if stob already exists */
	struct m0_stob		 bus_stob_;
	/** Segment to test */
	struct m0_be_seg	 bus_seg;
	/** Pointer to m0_be_ut_seg.bus_seg.bs_allocator */
	struct m0_be_allocator	*bus_allocator;
	void			*bus_copy;
};

void m0_be_ut_seg_init(struct m0_be_ut_seg *ut_seg,
		       struct m0_be_ut_backend *ut_be,
		       m0_bcount_t size);
void m0_be_ut_seg_fini(struct m0_be_ut_seg *ut_seg);
void m0_be_ut_seg_check_persistence(struct m0_be_ut_seg *ut_seg);
void m0_be_ut_seg_reload(struct m0_be_ut_seg *ut_seg);

/* m0_be_allocator_{init,create,open} */
void m0_be_ut_seg_allocator_init(struct m0_be_ut_seg *ut_seg,
				 struct m0_be_ut_backend *ut_be);
/* m0_be_allocator_{close,destroy,fini} */
void m0_be_ut_seg_allocator_fini(struct m0_be_ut_seg *ut_seg,
				 struct m0_be_ut_backend *ut_be);

struct m0_stob *m0_be_ut_stob_get(bool stob_create);
void m0_be_ut_stob_put(struct m0_stob *stob, bool stob_destroy);

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
