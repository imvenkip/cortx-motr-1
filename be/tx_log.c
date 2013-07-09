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
 * Original author: Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>
 * Original creation date: 17-Jun-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"
#include "lib/misc.h" /* M0_SET0 */
#include "lib/errno.h"
#include "lib/memory.h"
#include "be/tx_log.h"
#include "be/tx.h"


/**
 * @addtogroup be
 *
 * @{
 */

#if 0
M0_INTERNAL m0_bcount_t tx_log_size(const struct m0_be_tx *tx,
				    const struct m0_be_tx_credit *cr,
				    bool leader)
{
	return (leader ? sizeof(struct tx_group_header_X) : 0) +
		sizeof(struct tx_group_entry_X) + sizeof(struct tx_reg_header_X) +
		tx->t_payload_size +
		cr->tc_reg_nr * sizeof(struct m0_be_reg_d) + cr->tc_reg_size;
}

M0_INTERNAL m0_bcount_t tx_log_free_space(const struct m0_be_tx_engine *eng)
{
	return eng->te_log_X.lg_size - eng->te_reserved; /* XXX */
}

M0_INTERNAL m0_bcount_t tx_prepared_log_size(const struct m0_be_tx *tx)
{
	return tx_log_size(tx, &tx->t_prepared, true);
}

M0_INTERNAL m0_bcount_t tx_group_header_size(m0_bcount_t tx_nr)
{
	return sizeof(struct tx_group_header_X) +
		tx_nr * sizeof(struct tx_group_entry_X);
}

M0_INTERNAL void log_init(struct m0_be_log_X *log, m0_bcount_t log_size,
			  m0_bcount_t group_size, m0_bcount_t reg_max)

{
	*log = (struct m0_be_log_X) {
		.lg_lsn         = 0ULL,
		.lg_size        = log_size,
		.lg_gr_reg_max  = reg_max,
		.lg_gr_size_max = group_size,
	};
}

static void iovec_free(struct m0_bufvec *bv, struct m0_indexvec *iv)
{
	m0_free(bv->ov_vec.v_count);
	m0_free(bv->ov_buf);
	m0_free(iv->iv_vec.v_count);
	m0_free(iv->iv_index);
}

static int iovec_alloc(struct m0_bufvec *bv, struct m0_indexvec *iv,
		       m0_bindex_t nr)
{
	M0_SET0(bv);
	M0_SET0(iv);

	M0_ALLOC_ARR(bv->ov_vec.v_count, nr);
	M0_ALLOC_ARR(bv->ov_buf, nr);
	M0_ALLOC_ARR(iv->iv_vec.v_count, nr);
	M0_ALLOC_ARR(iv->iv_index, nr);

	if (bv->ov_vec.v_count == NULL || bv->ov_buf == NULL ||
	    iv->iv_vec.v_count == NULL || iv->iv_index == NULL) {
		iovec_free(bv, iv);
		return -ENOMEM;
	}

	return 0;
}

static bool log_io_completed(struct m0_clink *link)
{
	return true;
}

static int log_stor_open(struct m0_be_log_stor_X *stor)
{
	static struct m0_stob_id  id = { .si_bits = M0_UINT128(0x106, 0x570b) };
	struct m0_indexvec       *iv = &stor->ls_io.si_stob;
	struct m0_bufvec         *bv = &stor->ls_io.si_user;
	int                       rc;

	M0_ENTRY();
	M0_PRE(stor->ls_stob->so_domain != NULL);
	M0_PRE(stor->ls_stob->so_state  != CSS_EXISTS);

	m0_clink_init(&stor->ls_clink, log_io_completed);
	m0_stob_io_init(&stor->ls_io);

	rc = iovec_alloc(bv, iv, 2);
	if (rc != 0)
		return rc;

	rc = m0_stob_find(stor->ls_stob->so_domain, &id, &stor->ls_stob) ?:
		m0_stob_create(stor->ls_stob, NULL);

	if (rc != 0)
		iovec_free(bv, iv);

	return rc;
}

static void log_stor_close(struct m0_be_log_stor_X *stor)
{
	struct m0_indexvec *iv = &stor->ls_io.si_stob;
	struct m0_bufvec   *bv = &stor->ls_io.si_user;

	m0_clink_fini(&stor->ls_clink);
	m0_stob_io_fini(&stor->ls_io);
	m0_stob_put(stor->ls_stob);
	iovec_free(bv, iv); /* XXX TODO: stob destroy ... */
}

M0_INTERNAL int log_open(struct m0_be_log_X *log)
{
	int rc;

	M0_ENTRY();

	M0_ALLOC_ARR(log->lg_grent_buf, log->lg_gr_size_max);
	M0_ALLOC_ARR(log->lg_grent.ge_reg.rs_reg, log->lg_gr_reg_max);
	if (log->lg_grent_buf == NULL || log->lg_grent.ge_reg.rs_reg == NULL)
		goto err;

	rc = log_stor_open(&log->lg_stor);
	if (rc == 0)
		M0_RETURN(rc);
err:
	m0_free(log->lg_grent_buf);
	m0_free(log->lg_grent.ge_reg.rs_reg);
	M0_RETURN(-ENOMEM);
}

M0_INTERNAL void log_close(struct m0_be_log_X *log)
{
	log_stor_close(&log->lg_stor);
	m0_free(log->lg_grent.ge_reg.rs_reg);
	m0_free(log->lg_grent_buf);
}
#endif

/** @} end of be group */

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
