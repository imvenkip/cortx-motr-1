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
#include "lib/trace.h"		/* M0_LOG */

#include "be/tx_group_ondisk.h"

#include "be/log.h"
#include "be/tx_regmap.h"	/* m0_be_reg_area_used */
#include "be/tx.h"		/* m0_be_tx */

#include "lib/memory.h"		/* m0_alloc */
#include "lib/errno.h"		/* ENOMEM */

/**
 * @addtogroup be
 *
 * @{
 */

void be_tx_group_ondisk_cblock_credit(struct m0_be_tx_credit *cblock)
{
	m0_be_log_cblock_credit(cblock, sizeof(struct tx_group_commit_block));
}

void be_log_io_credit_tx(struct m0_be_tx_credit *io_tx,
				const struct m0_be_tx_credit *prepared)
{
	*io_tx = *prepared;

	/* for log wrap case */
	m0_be_tx_credit_add(io_tx, &M0_BE_TX_CREDIT(1, 0));
	m0_be_tx_credit_add(io_tx,
			    &M0_BE_TX_CREDIT_TYPE(struct tx_group_header));
	m0_be_tx_credit_add(io_tx,
			    &M0_BE_TX_CREDIT_TYPE(struct tx_group_entry));
	m0_be_tx_credit_mac(io_tx, &M0_BE_TX_CREDIT_TYPE(struct tx_reg_header),
			    prepared->tc_reg_nr);
	m0_be_tx_credit_add(io_tx, &M0_BE_TX_CREDIT_TYPE(struct
						  tx_group_commit_block));
}

void be_log_io_credit_group(struct m0_be_tx_credit *io_group,
				   size_t tx_nr_max,
				   const struct m0_be_tx_credit *prepared)
{
	struct m0_be_tx_credit io_tx;

	M0_ASSERT(tx_nr_max > 0);

	m0_be_tx_credit_init(io_group);

	be_log_io_credit_tx(&io_tx, prepared);
	m0_be_tx_credit_add(io_group, &io_tx);

	be_log_io_credit_tx(&io_tx, &M0_BE_TX_CREDIT(0, 0));
	m0_be_tx_credit_mac(io_group, &io_tx, tx_nr_max - 1);
}

static void be_group_ondisk_free(struct m0_be_group_ondisk *go)
{
	m0_free(go->go_entry);
	m0_free(go->go_reg);
}

M0_INTERNAL int m0_be_group_ondisk_init(struct m0_be_group_ondisk *go,
					struct m0_stob *log_stob,
					size_t tx_nr_max,
					struct m0_be_tx_credit *size_max)
{
	struct m0_be_tx_credit credit_log;
	struct m0_be_tx_credit credit_log_cblock;
	int		       rc;
	int		       rc1;
	int		       rc2;
	int		       rc3;
	int		       rc4;

	M0_ENTRY();

	M0_ALLOC_ARR(go->go_entry, tx_nr_max);
	M0_ALLOC_ARR(go->go_reg, size_max->tc_reg_nr);

	be_log_io_credit_group(&credit_log, tx_nr_max, size_max);
	be_tx_group_ondisk_cblock_credit(&credit_log_cblock);
	m0_be_tx_credit_sub(&credit_log, &credit_log_cblock);

	rc1 = m0_be_io_init(&go->go_io_log, log_stob, &credit_log);
	rc2 = m0_be_io_init(&go->go_io_log_cblock, log_stob,
			    &credit_log_cblock);
	rc3 = m0_be_io_init(&go->go_io_seg, log_stob, size_max);

	rc4 = m0_be_reg_area_init(&go->go_area, size_max, false);

	if (go->go_entry == NULL || go->go_reg == NULL ||
	    rc1 != 0 || rc2 != 0 || rc3 != 0 || rc4 != 0) {
		if (rc1 == 0)
			m0_be_io_fini(&go->go_io_log);
		if (rc2 == 0)
			m0_be_io_fini(&go->go_io_log_cblock);
		if (rc3 == 0)
			m0_be_io_fini(&go->go_io_seg);
		if (rc4 == 0)
			m0_be_reg_area_fini(&go->go_area);
		be_group_ondisk_free(go);
		rc = -ENOMEM;
	} else {
		rc = 0;
	}
	M0_POST(ergo(rc == 0, m0_be_group_ondisk__invariant(go)));
	M0_RETURN(rc);
}

M0_INTERNAL void m0_be_group_ondisk_fini(struct m0_be_group_ondisk *go)
{
	M0_PRE(m0_be_group_ondisk__invariant(go));
	m0_be_io_fini(&go->go_io_log);
	m0_be_io_fini(&go->go_io_log_cblock);
	m0_be_io_fini(&go->go_io_seg);
	m0_be_reg_area_fini(&go->go_area);
	be_group_ondisk_free(go);
}

M0_INTERNAL bool m0_be_group_ondisk__invariant(struct m0_be_group_ondisk *go)
{
	return true;
}

M0_INTERNAL void m0_be_group_ondisk_reset(struct m0_be_group_ondisk *go)
{
	m0_be_io_reset(&go->go_io_log);
	m0_be_io_reset(&go->go_io_log_cblock);
	m0_be_io_reset(&go->go_io_seg);
	m0_be_reg_area_reset(&go->go_area);
}

M0_INTERNAL void m0_be_group_ondisk_reserved(struct m0_be_group_ondisk *go,
					     struct m0_be_tx_group *group,
					     struct m0_be_tx_credit *reserved,
					     size_t *tx_nr)
{
	struct m0_be_tx_credit tx_prepared;
	struct m0_be_tx       *tx;

	*tx_nr = 0;
	m0_be_tx_credit_init(reserved);
	m0_tl_for(grp, &group->tg_txs, tx) {
		m0_be_reg_area_prepared(&tx->t_reg_area, &tx_prepared);
		m0_be_tx_credit_add(reserved, &tx_prepared);
		++*tx_nr;
	} m0_tl_endfor;
}

M0_INTERNAL void m0_be_group_ondisk_io_reserved(struct m0_be_group_ondisk *go,
						struct m0_be_tx_group *group,
						struct m0_be_tx_credit
						*io_reserved)
{
	struct m0_be_tx_credit reserved = M0_BE_TX_CREDIT(0, 0);
	size_t		       tx_nr = 0;

	m0_be_group_ondisk_reserved(go, group, &reserved, &tx_nr);
	be_log_io_credit_group(io_reserved, tx_nr, &reserved);
}

M0_INTERNAL void m0_be_group_ondisk_serialize(struct m0_be_group_ondisk *go,
					      struct m0_be_tx_group *group,
					      struct m0_be_log *log)
{
	struct m0_be_log_stor_io lsi;
	struct m0_be_tx_credit	 reg_cr;
	struct m0_be_tx_credit	 io_cr;
	struct m0_be_reg_d	*rd;
	struct m0_be_tx		*tx;
	int			 i;
	uint64_t		 tx_nr;
	uint64_t		 reg_nr;


	m0_be_group_ondisk_reserved(go, group, &reg_cr, &tx_nr);
	m0_be_group_ondisk_io_reserved(go, group, &io_cr);
	m0_be_log_stor_io_init(&lsi, &log->lg_stor, &go->go_io_log,
			       &go->go_io_log_cblock, io_cr.tc_reg_size);

	/* merge transactions reg_area */
	tx_nr = 0;
	M0_BE_TX_GROUP_TX_FORALL(group, tx) {
		m0_be_reg_area_merge_in(&go->go_area, m0_be_tx__reg_area(tx));
		++tx_nr;
	} M0_BE_TX_GROUP_TX_ENDFOR;

	reg_nr = 0;
	M0_BE_REG_AREA_FORALL(&go->go_area, rd) {
		go->go_reg[reg_nr] = (struct tx_reg_header) {
			.rh_lsn    = 0xABC,	/* XXX */
			.rh_offset = (uintptr_t) rd->rd_reg.br_addr,
			.rh_size   = rd->rd_reg.br_size,
			.rh_seg_id = 0xDEF,	/* XXX */
		};
		++reg_nr;
		M0_ASSERT(reg_nr <= reg_cr.tc_reg_nr);
	}

	go->go_header.gh_tx_nr	= tx_nr;
	go->go_header.gh_reg_nr = reg_nr;

	/* add to log io */
	M0_BE_LOG_STOR_IO_ADD_PTR(&lsi, &go->go_header);
	for (i = 0; i < go->go_header.gh_tx_nr; ++i)
		M0_BE_LOG_STOR_IO_ADD_PTR(&lsi, &go->go_entry[i]);
	for (i = 0; i < go->go_header.gh_reg_nr; ++i)
		M0_BE_LOG_STOR_IO_ADD_PTR(&lsi, &go->go_reg[i]);
	M0_BE_REG_AREA_FORALL(&go->go_area, rd) {
		m0_be_log_stor_io_add(&lsi, rd->rd_buf, rd->rd_reg.br_size);
	}
	m0_be_log_stor_io_add_cblock(&lsi, &go->go_cblock,
				     sizeof(go->go_cblock));
	m0_be_log_stor_io_sort(&lsi);
	m0_be_log_stor_io_fini(&lsi);

	/* add to seg io */
	m0_be_reg_area_io_add(&go->go_area, &go->go_io_seg);

	m0_be_io_configure(&go->go_io_log, SIO_WRITE);
	m0_be_io_configure(&go->go_io_log_cblock, SIO_WRITE);
	m0_be_io_configure(&go->go_io_seg, SIO_WRITE);
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
