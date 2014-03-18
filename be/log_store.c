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

#include "be/log_store.h"

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"		/* M0_LOG */
#include "lib/errno.h"		/* ENOSYS */
#include "lib/misc.h"		/* M0_SET0 */

#include "stob/linux.h"		/* m0_linux_stob_domain_locate */
#include "dtm/dtm.h"		/* m0_dtx_init */

#include <stdlib.h>		/* system */
#include <sys/stat.h>		/* mkdir */
#include <sys/types.h>		/* mkdir */

/*
#define LOGD(...) printf(__VA_ARGS__)
*/
#define LOGD(...)

/* XXX */
/*
#define BE_LOG_STOR_DESTOROY_STOB 1
*/

/**
 * @addtogroup be
 *
 * @{
 */

M0_INTERNAL void m0_be_log_store_init(struct m0_be_log_store *ls,
				      struct m0_stob *stob)
{
	ls->ls_stob = stob;
}

M0_INTERNAL void m0_be_log_store_fini(struct m0_be_log_store *ls)
{
}

M0_INTERNAL bool m0_be_log_store__invariant(struct m0_be_log_store *ls)
{
	return _0C(ls != NULL) &&
	       _0C(ls->ls_discarded <= ls->ls_pos) &&
	       _0C(ls->ls_pos <= ls->ls_reserved) &&
	       _0C(ergo(ls->ls_size > 0,
		     (ls->ls_reserved - ls->ls_discarded) <= ls->ls_size));
}

M0_INTERNAL int  m0_be_log_store_open(struct m0_be_log_store *ls)
{
	/* XXX */
	M0_IMPOSSIBLE("Not implemented yet");
	return -ENOSYS;
}

M0_INTERNAL void m0_be_log_store_close(struct m0_be_log_store *ls)
{
	/* XXX */
	M0_IMPOSSIBLE("Not implemented yet");
}

M0_INTERNAL int  m0_be_log_store_create(struct m0_be_log_store *ls,
				       m0_bcount_t ls_size)
{
	M0_PRE(m0_be_log_store__invariant(ls));

	ls->ls_size = ls_size;

	M0_POST(m0_be_log_store__invariant(ls));

	return 0;
}

M0_INTERNAL void m0_be_log_store_destroy(struct m0_be_log_store *ls)
{
	M0_PRE(m0_be_log_store__invariant(ls));
}

M0_INTERNAL struct m0_stob *m0_be_log_store_stob(struct m0_be_log_store *ls)
{
	return ls->ls_stob;
}

M0_INTERNAL m0_bcount_t m0_be_log_store_free(const struct m0_be_log_store *ls)
{
	return ls->ls_size - (ls->ls_reserved - ls->ls_discarded);
}

M0_INTERNAL m0_bcount_t m0_be_log_store_size(const struct m0_be_log_store *ls)
{
	return ls->ls_size;
}

M0_INTERNAL int m0_be_log_store_reserve(struct m0_be_log_store *ls,
				       m0_bcount_t size)
{
	int rc;

	M0_ENTRY("size = %lu", size);

	M0_PRE(m0_be_log_store__invariant(ls));
	if (m0_be_log_store_free(ls) < size) {
		rc = -EAGAIN;
	} else {
		ls->ls_reserved += size;
		rc = 0;
	}
	M0_POST(m0_be_log_store__invariant(ls));

	return M0_RC(rc);
}

M0_INTERNAL void m0_be_log_store_discard(struct m0_be_log_store *ls,
					m0_bcount_t size)
{
	M0_ENTRY("size = %lu", size);

	M0_PRE(m0_be_log_store__invariant(ls));
	M0_PRE(ls->ls_discarded + size <= ls->ls_pos);

	ls->ls_discarded += size;

	M0_POST(m0_be_log_store__invariant(ls));
	M0_LEAVE();
}

static void be_log_store_pos_advance(struct m0_be_log_store *ls,
				    m0_bcount_t size,
				    m0_bindex_t *pos_prev,
				    m0_bindex_t *pos_curr)
{
	M0_PRE(m0_be_log_store__invariant(ls));
	M0_PRE(ls->ls_pos + size <= ls->ls_reserved);

	if (pos_prev != NULL)
		*pos_prev = ls->ls_pos;
	ls->ls_pos += size;
	if (pos_curr != NULL)
		*pos_curr = ls->ls_pos;

	M0_POST(m0_be_log_store__invariant(ls));
}

M0_INTERNAL void m0_be_log_store_pos_advance(struct m0_be_log_store *ls,
					     m0_bcount_t size)
{
	be_log_store_pos_advance(ls, size, NULL, NULL);
}

M0_INTERNAL void m0_be_log_store_cblock_io_credit(struct m0_be_tx_credit *credit,
						  m0_bcount_t cblock_size)
{
	/* XXX */
	/*
	 * commit block can wrap around
	 */
	*credit = M0_BE_TX_CREDIT(2, cblock_size);
}

M0_INTERNAL void m0_be_log_store_io_init(struct m0_be_log_store_io *lsi,
					struct m0_be_log_store *ls,
					struct m0_be_io *bio,
					struct m0_be_io *bio_cblock,
					m0_bcount_t size_reserved)
{
	*lsi = (struct m0_be_log_store_io) {
		.lsi_ls		   = ls,
		.lsi_io		   = bio,
		.lsi_io_cblock	   = bio_cblock,
	};

	M0_ENTRY("size_reserved = %lu", size_reserved);

	m0_be_io_reset(bio);
	m0_be_io_reset(bio_cblock);
	be_log_store_pos_advance(ls, size_reserved,
				&lsi->lsi_pos, &lsi->lsi_end);
	M0_POST(m0_be_log_store_io__invariant(lsi));

	M0_LEAVE();
}

static bool be_log_store_io_add_wraps(m0_bindex_t pos,
				     m0_bcount_t size,
				     m0_bcount_t ls_size)
{
	return pos / ls_size != (pos + size - 1) / ls_size;
}

static void be_log_store_io_add_nowrap(struct m0_be_log_store_io *lsi,
				      struct m0_be_io *bio,
				      void *ptr,
				      m0_bcount_t size,
				      m0_bcount_t ls_size)
{
	M0_PRE(!be_log_store_io_add_wraps(lsi->lsi_pos, size, ls_size));


	LOGD("%s: ptr = %p, size = %lu, pos = %lu, end = %lu\n",
	     __func__, ptr, size, lsi->lsi_pos, lsi->lsi_end);

	m0_be_io_add(bio, ptr, lsi->lsi_pos % ls_size, size);
	lsi->lsi_pos += size;
}

static void be_log_store_io_add(struct m0_be_log_store_io *lsi,
			       struct m0_be_io *bio,
			       void *ptr,
			       m0_bcount_t size)
{
	m0_bcount_t ls_size = m0_be_log_store_size(lsi->lsi_ls);
	m0_bcount_t size1;
	m0_bindex_t wrap_point;

	M0_PRE(m0_be_log_store_io__invariant(lsi));
	M0_PRE(size > 0);
	M0_PRE(lsi->lsi_pos + size <= lsi->lsi_end);

	if (!be_log_store_io_add_wraps(lsi->lsi_pos, size, ls_size)) {
		be_log_store_io_add_nowrap(lsi, bio, ptr, size, ls_size);
	} else {
		wrap_point = (lsi->lsi_pos + ls_size - 1) / ls_size * ls_size;
		size1 = wrap_point - lsi->lsi_pos;
		M0_ASSERT(size1 < size);

		be_log_store_io_add_nowrap(lsi, bio, ptr, size1, ls_size);
		be_log_store_io_add_nowrap(lsi, bio, ptr + size1, size - size1,
					   ls_size);
	}

	M0_POST(m0_be_log_store_io__invariant(lsi));
}

M0_INTERNAL void m0_be_log_store_io_add(struct m0_be_log_store_io *lsi,
				       void *ptr,
				       m0_bcount_t size)
{
	M0_PRE(m0_be_log_store_io__invariant(lsi));

	be_log_store_io_add(lsi, lsi->lsi_io, ptr, size);

	M0_POST(m0_be_log_store_io__invariant(lsi));
}

M0_INTERNAL void m0_be_log_store_io_add_cblock(struct m0_be_log_store_io *lsi,
					      void *ptr_cblock,
					      m0_bcount_t size_cblock)
{
	M0_PRE(m0_be_log_store_io__invariant(lsi));

	lsi->lsi_pos = lsi->lsi_end - size_cblock;
	be_log_store_io_add(lsi, lsi->lsi_io_cblock, ptr_cblock, size_cblock);

	M0_POST(m0_be_log_store_io__invariant(lsi));
}

M0_INTERNAL void m0_be_log_store_io_sort(struct m0_be_log_store_io *lsi)
{
	m0_stob_iovec_sort(&lsi->lsi_io->bio_io);
	m0_stob_iovec_sort(&lsi->lsi_io_cblock->bio_io);
}

M0_INTERNAL void m0_be_log_store_io_fini(struct m0_be_log_store_io *lsi)
{
	M0_PRE(m0_be_log_store_io__invariant(lsi));
}

M0_INTERNAL bool m0_be_log_store_io__invariant(struct m0_be_log_store_io *lsi)
{
	return _0C(lsi != NULL) && _0C(lsi->lsi_pos <= lsi->lsi_end);
}

/** @} end of be group */
#undef M0_TRACE_SUBSYSTEM


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
