/* -*- C -*- */

#ifndef __COLIBRI_DB_LINUX_KERNEL_DB_IMPL_H__
#define __COLIBRI_DB_LINUX_KERNEL_DB_IMPL_H__

#include "lib/list.h"
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
	struct c2_list  tk_pair;
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
	struct c2_list_link dk_linkage;
	struct c2_buf       dk_key;
	struct c2_buf       dk_rec;
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
