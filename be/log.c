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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include "be/log.h"

#include "lib/errno.h"		/* ENOSYS */

#include "be/tx_group_ondisk.h"	/* m0_be_group_ondisk_serialize */

/**
 * @addtogroup be
 *
 * @{
 */

M0_INTERNAL void m0_be_log_init(struct m0_be_log *log)
{
	m0_be_log_stor_init(&log->lg_stor);
	M0_POST(m0_be_log__invariant(log));
}

M0_INTERNAL void m0_be_log_fini(struct m0_be_log *log)
{
	M0_PRE(m0_be_log__invariant(log));
	m0_be_log_stor_fini(&log->lg_stor);
}

M0_INTERNAL bool m0_be_log__invariant(struct m0_be_log *log)
{
	return true;
}

M0_INTERNAL int m0_be_log_open(struct m0_be_log *log)
{
	return -ENOSYS;
}

M0_INTERNAL void m0_be_log_close(struct m0_be_log *log)
{
}

M0_INTERNAL int m0_be_log_create(struct m0_be_log *log, m0_bcount_t log_size)
{
	return m0_be_log_stor_create(&log->lg_stor, log_size);
}

M0_INTERNAL void m0_be_log_destroy(struct m0_be_log *log)
{
	m0_be_log_stor_destroy(&log->lg_stor);
}

M0_INTERNAL struct m0_stob *m0_be_log_stob(struct m0_be_log *log)
{
	return m0_be_log_stor_stob(&log->lg_stor);
}

M0_INTERNAL void
m0_be_log_cblock_credit(struct m0_be_tx_credit *credit, m0_bcount_t cblock_size)
{
	m0_be_log_stor_cblock_io_credit(credit, cblock_size);
}

M0_INTERNAL int m0_be_log_submit(struct m0_be_log *log,
				 struct m0_be_op *op,
				 struct m0_be_tx_group *group)
{
	m0_be_group_ondisk_serialize(&group->tg_od, group, log);

	return m0_be_io_launch(&group->tg_od.go_io_log, op);
}

M0_INTERNAL int m0_be_log_commit(struct m0_be_log *log,
				 struct m0_be_op *op,
				 struct m0_be_tx_group *group)
{
	return m0_be_io_launch(&group->tg_od.go_io_log_cblock, op);
}

/*
M0_INTERNAL void m0_be_log_discard(struct m0_be_log *log,
				   struct m0_be_group_ondisk *group)
{
	struct m0_be_tx_credit reserved;
	size_t		       tx_nr;

	m0_be_group_ondisk_reserved(&group->gr_od, group, &reserved, &tx_nr);
	m0_be_log_stor_discard(&log->lg_stor, reserved.tx_reg_size);
}
*/

M0_INTERNAL void m0_be_log_discard(struct m0_be_log *log,
				   struct m0_be_tx_credit *reserved)
{
	m0_be_log_stor_discard(&log->lg_stor, reserved->tc_reg_size);
}

M0_INTERNAL int
m0_be_log_reserve_tx(struct m0_be_log *log, struct m0_be_tx_credit *prepared)
{
	struct m0_be_tx_credit io_tx;
	int                    rc;

	M0_ENTRY();
	M0_PRE(m0_be_log__invariant(log));

	be_log_io_credit_tx(&io_tx, prepared);
	rc = m0_be_log_stor_reserve(&log->lg_stor, io_tx.tc_reg_size);

	M0_POST(m0_be_log__invariant(log));
	M0_RETURN(rc);
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
