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

#include "be/tx_group_ondisk.h"

#include "be/log.h"
#include "be/tx_regmap.h"	/* m0_be_reg_area_used */
#include "be/tx_internal.h"	/* m0_be_tx__reg_area */

#include "lib/memory.h"		/* m0_alloc */
#include "lib/errno.h"		/* ENOMEM */

/**
 * @addtogroup be
 *
 * @{
 */

static int group_io_init(struct m0_be_group_ondisk *g, struct m0_stob *stob,
			 const struct m0_be_tx_credit *cr_logrec,
			 const struct m0_be_tx_credit *cr_commit_block,
			 const struct m0_be_tx_credit *cr_group_maxsize);
static void group_io_fini(struct m0_be_group_ondisk *g);

M0_INTERNAL void be_log_io_credit_tx(struct m0_be_tx_credit *io_tx,
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

M0_INTERNAL void be_log_io_credit_group(struct m0_be_tx_credit *io_group,
					size_t tx_nr_max,
					const struct m0_be_tx_credit *prepared)
{
	struct m0_be_tx_credit io_tx;
	M0_PRE(tx_nr_max > 0);

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
					const struct m0_be_tx_credit *size_max)
{
	struct m0_be_tx_credit cr_logrec;
	struct m0_be_tx_credit cr_commit_block;
	int                    rc;

	M0_ENTRY();

	M0_ALLOC_ARR(go->go_entry, tx_nr_max);
	if (go->go_entry == NULL)
		goto err;

	M0_ALLOC_ARR(go->go_reg, size_max->tc_reg_nr);
	if (go->go_reg == NULL)
		goto err_entry;

	be_log_io_credit_group(&cr_logrec, tx_nr_max, size_max);
	m0_be_log_cblock_credit(&cr_commit_block,
				sizeof(struct tx_group_commit_block));
	m0_be_tx_credit_sub(&cr_logrec, &cr_commit_block);

	rc = group_io_init(go, log_stob, &cr_logrec, &cr_commit_block,
			   size_max);
	if (rc != 0)
		goto err_reg;

	rc = m0_be_reg_area_init(&go->go_area, size_max, false);
	if (rc != 0)
		goto err_io;

	M0_POST(m0_be_group_ondisk__invariant(go));
	M0_RETURN(0);

err_io:
	group_io_fini(go);
err_reg:
	m0_free(go->go_reg);
err_entry:
	m0_free(go->go_entry);
err:
	M0_RETURN(-ENOMEM);
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
	return true; /* XXX TODO */
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
	M0_BE_TX_GROUP_TX_FORALL(group, tx) {
		m0_be_reg_area_prepared(&tx->t_reg_area, &tx_prepared);
		m0_be_tx_credit_add(reserved, &tx_prepared);
		++*tx_nr;
	} M0_BE_TX_GROUP_TX_ENDFOR;
}

M0_INTERNAL void m0_be_group_ondisk_io_reserved(struct m0_be_group_ondisk *go,
						struct m0_be_tx_group *group,
						struct m0_be_tx_credit
						*io_reserved)
{
	struct m0_be_tx_credit reserved = M0_BE_TX_CREDIT(0, 0);
	size_t                 tx_nr;

	m0_be_group_ondisk_reserved(go, group, &reserved, &tx_nr);
	be_log_io_credit_group(io_reserved, tx_nr, &reserved);
}

M0_INTERNAL void m0_be_group_ondisk_serialize(struct m0_be_group_ondisk *go,
					      struct m0_be_tx_group *group,
					      struct m0_be_log *log)
{
	struct m0_be_log_store_io lsi;
	struct m0_be_tx_credit   reg_cr;
	struct m0_be_tx_credit   io_cr;
	struct m0_be_reg_d      *rd;
	struct m0_be_tx         *tx;
	int                      i;
	uint64_t                 tx_nr;
	uint64_t                 reg_nr;


	m0_be_group_ondisk_reserved(go, group, &reg_cr, &tx_nr);
	m0_be_group_ondisk_io_reserved(go, group, &io_cr);
	m0_be_log_store_io_init(&lsi, &log->lg_stor, &go->go_io_log,
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
		m0_be_log_store_io_add(&lsi, rd->rd_buf, rd->rd_reg.br_size);
	}
	m0_be_log_store_io_add_cblock(&lsi, &go->go_cblock,
				     sizeof(go->go_cblock));
	m0_be_log_store_io_sort(&lsi);
	m0_be_log_store_io_fini(&lsi);

	/* add to seg io */
	m0_be_reg_area_io_add(&go->go_area, &go->go_io_seg);

	m0_be_io_configure(&go->go_io_log, SIO_WRITE);
	m0_be_io_configure(&go->go_io_log_cblock, SIO_WRITE);
	m0_be_io_configure(&go->go_io_seg, SIO_WRITE);
}

static int group_io_init(struct m0_be_group_ondisk *g, struct m0_stob *stob,
			 const struct m0_be_tx_credit *cr_logrec,
			 const struct m0_be_tx_credit *cr_commit_block,
			 const struct m0_be_tx_credit *cr_group_maxsize)
{
	int rc;
	/*
	 * XXX TODO: Write a small library module for dealing with chains
	 * of init/fini statements. The implementation will be pretty much
	 * similar to mero/init.c.
	 */
	rc = m0_be_io_init(&g->go_io_log, stob, cr_logrec);
	if (rc != 0)
		return rc;

	rc = m0_be_io_init(&g->go_io_log_cblock, stob, cr_commit_block);
	if (rc != 0)
		goto err;

	rc = m0_be_io_init(&g->go_io_seg, stob, cr_group_maxsize);
	if (rc == 0)
		return 0;

	m0_be_io_fini(&g->go_io_log_cblock);
err:
	m0_be_io_fini(&g->go_io_log);
	return rc;
}

static void group_io_fini(struct m0_be_group_ondisk *g)
{
	m0_be_io_fini(&g->go_io_seg);
	m0_be_io_fini(&g->go_io_log_cblock);
	m0_be_io_fini(&g->go_io_log);
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
