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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 29-May-2013
 */

#pragma once
#ifndef __MERO_BE_BE_H__
#define __MERO_BE_BE_H__

#include "be/op.h"		/* XXX dirty hack. remove it ASAP */

/**
 * @defgroup be Meta-data back-end
 *
 * This file contains BE details visible to the user.
 *
 * Definitions
 * - BE - metadata backend. Implemented as recoverable virtual memory (RVM) with
 *   write-ahead logging without undo;
 * - Segment - part of virtual memory backed (using BE) by stob;
 * - Region - continuous part of segment. Is defined by address of the first
 *   byte of the region and region size;
 * - Transaction - recoverable set of segment changes;
 * - Capturing - process of saving region contents into transaction's private
 *   memory buffer;
 * @{
 */

struct m0_be {
};

/* These two are called from mero/init.c. */
M0_INTERNAL int  m0_backend_init(void);
M0_INTERNAL void m0_backend_fini(void);

/** @} end of be group */
#endif /* __MERO_BE_BE_H__ */

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
