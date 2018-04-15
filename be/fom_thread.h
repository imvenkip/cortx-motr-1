/* -*- C -*- */
/*
 * COPYRIGHT 2018 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <max.medved@seagate.com>
 * Original creation date: 13-Apr-2018
 */

#pragma once

#ifndef __MERO_BE_FOM_THREAD_H__
#define __MERO_BE_FOM_THREAD_H__

/**
 * @defgroup be
 *
 * @{
 */

#include "lib/thread.h"         /* m0_thread */
#include "lib/semaphore.h"      /* m0_semaphore */
#include "sm/sm.h"              /* m0_sm_group */
#include "fop/fom.h"            /* m0_fom_locality */

struct m0_fom;

struct m0_be_fom_thread {
	struct m0_thread        fth_thread;
	struct m0_semaphore     fth_start_sem;
	struct m0_semaphore     fth_wakeup_sem;
	struct m0_fom_locality  fth_loc;
	struct m0_clink         fth_clink;
	struct m0_fom          *fth_fom;
};

M0_INTERNAL int m0_be_fom_thread_init(struct m0_be_fom_thread *fth,
                                      struct m0_fom           *fom);
M0_INTERNAL void m0_be_fom_thread_fini(struct m0_be_fom_thread *fth);

M0_INTERNAL void m0_be_fom_thread_wakeup(struct m0_be_fom_thread *fth);


/** @} end of be group */
#endif /* __MERO_BE_FOM_THREAD_H__ */

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
