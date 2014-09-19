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
 * Original creation date: 4-Jul-2013
 */


#pragma once

#ifndef __MERO_BE_LOG_STORE_H__
#define __MERO_BE_LOG_STORE_H__


#include "lib/types.h"		/* m0_bcount_t */

#include "be/io.h"

/**
 * @defgroup be Meta-data back-end
 *
 * @{
 */

struct m0_stob;

/**
 * @verbatim
 * v
 * |
 * | transactions are stable;
 * | log records are discarded;
 * | log records couldn't be replayed.
 * +-------------------------------------------- <- ls_discarded
 * | transactions are logged;
 * | log records could be replayed.
 * +-------------------------------------------- <- ls_logged
 * | reserved space for transactions;
 * | there is no actual transactions here yet,
 * | but they will be added in the future.
 * +-------------------------------------------- <- ls_reserved
 * |
 * v
 * @endverbatim
 */
struct m0_be_log_store {
	struct m0_stob    *ls_stob;
	m0_bcount_t	   ls_size;

	m0_bindex_t        ls_discarded;
	m0_bindex_t        ls_pos;
	m0_bindex_t        ls_reserved;
};

struct m0_be_log_store_io {
	struct m0_be_log_store *lsi_ls;
	struct m0_be_io	      *lsi_io;
	struct m0_be_io	      *lsi_io_cblock;
	m0_bindex_t	       lsi_pos;
	m0_bindex_t	       lsi_end;
};

M0_INTERNAL void m0_be_log_store_init(struct m0_be_log_store *ls,
				      struct m0_stob *stob);
M0_INTERNAL void m0_be_log_store_fini(struct m0_be_log_store *ls);
M0_INTERNAL bool m0_be_log_store__invariant(struct m0_be_log_store *ls);

M0_INTERNAL int  m0_be_log_store_open(struct m0_be_log_store *ls);
M0_INTERNAL void m0_be_log_store_close(struct m0_be_log_store *ls);

M0_INTERNAL int  m0_be_log_store_create(struct m0_be_log_store *ls,
				       m0_bcount_t ls_size);
M0_INTERNAL void m0_be_log_store_destroy(struct m0_be_log_store *ls);

M0_INTERNAL struct m0_stob *m0_be_log_store_stob(struct m0_be_log_store *ls);

M0_INTERNAL int  m0_be_log_store_reserve(struct m0_be_log_store *ls,
					m0_bcount_t size);
M0_INTERNAL void m0_be_log_store_discard(struct m0_be_log_store *ls,
					m0_bcount_t size);
M0_INTERNAL void m0_be_log_store_pos_advance(struct m0_be_log_store *ls,
					     m0_bcount_t size);
M0_INTERNAL m0_bcount_t m0_be_log_store_size(const struct m0_be_log_store *ls);
M0_INTERNAL m0_bcount_t m0_be_log_store_free(const struct m0_be_log_store *ls);

M0_INTERNAL void m0_be_log_store_cblock_io_credit(struct m0_be_tx_credit *credit,
						 m0_bcount_t cblock_size);


M0_INTERNAL void m0_be_log_store_io_init(struct m0_be_log_store_io *lsi,
					struct m0_be_log_store *ls,
					struct m0_be_io *bio,
					struct m0_be_io *bio_cblock,
					m0_bcount_t size_reserved);
M0_INTERNAL void m0_be_log_store_io_add(struct m0_be_log_store_io *lsi,
				       void *ptr,
				       m0_bcount_t size);
M0_INTERNAL void m0_be_log_store_io_add_cblock(struct m0_be_log_store_io *lsi,
					      void *ptr_cblock,
					      m0_bcount_t size_cblock);
M0_INTERNAL void m0_be_log_store_io_sort(struct m0_be_log_store_io *lsi);
M0_INTERNAL void m0_be_log_store_io_fini(struct m0_be_log_store_io *lsi);
M0_INTERNAL bool m0_be_log_store_io__invariant(struct m0_be_log_store_io *lsi);

#define M0_BE_LOG_STOR_IO_ADD_PTR(lsi, ptr) \
	m0_be_log_store_io_add((lsi), (ptr), sizeof *(ptr))

/** @} end of be group */

#endif /* __MERO_BE_LOG_STORE_H__ */


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
