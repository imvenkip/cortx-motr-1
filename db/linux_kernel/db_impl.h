/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 09/23/2010
 */

#pragma once

#ifndef __COLIBRI_DB_LINUX_KERNEL_DB_IMPL_H__
#define __COLIBRI_DB_LINUX_KERNEL_DB_IMPL_H__

#include "lib/tlist.h"
#include "lib/mutex.h"

/**
   @addtogroup db Data-base interfaces.

   <b>Linux kernel implementation.</b>

   @{
 */

struct c2_dbenv_impl {
};

struct c2_table_impl {
	/**
	   Kernel "table" is simply a list of pairs in memory.
	 */
	struct c2_tl    tk_pair;
	struct c2_mutex tk_lock;
};

struct c2_db_buf_impl {
};

struct c2_db_tx_impl {
};

struct c2_db_tx_waiter_impl {
};

/**
   (key, record) pair in a kernel memory.
 */
struct c2_db_kpair {
	uint64_t        dk_magix;
	struct c2_tlink dk_linkage;
	struct c2_buf   dk_key;
	struct c2_buf   dk_rec;
	/* followed by dk_key.a_nob + dk_rec.a_nob bytes */
};

struct c2_db_cursor_impl {
	struct c2_db_kpair *ck_current;
};

/** @} end of db group */

/* __COLIBRI_DB_LINUX_KERNEL_DB_IMPL_H__ */
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
