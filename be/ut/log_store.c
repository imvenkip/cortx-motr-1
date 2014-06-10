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
#include "be/be.h"

#include "lib/misc.h"		/* M0_SET0 */
#include "ut/ut.h"		/* M0_UT_ASSERT */
#include "ut/stob.h"		/* m0_ut_stob_linux_get */

#include <stdlib.h>		/* rand_r */

enum {
	BE_UT_LOG_STOR_SIZE  = 0x100,
	BE_UT_LOG_STOR_STEP  = 0xF,
	BE_UT_LOG_STOR_ITER  = 0x200,
	BE_UT_LOG_STOR_CR_NR = 0x7,
};

static char	   be_ut_log_store_pre[BE_UT_LOG_STOR_SIZE];
static char	   be_ut_log_store_post[BE_UT_LOG_STOR_SIZE];
static unsigned	   be_ut_log_store_seed;
static m0_bindex_t be_ut_log_store_pos;

/* this random may have non-uniform distribution */
static int be_ut_log_store_rand(int mod)
{
	return rand_r(&be_ut_log_store_seed) % mod;
}

static void
be_ut_log_store_io_read(struct m0_be_log_store *ls, char *buf, m0_bcount_t size)
{
	struct m0_be_io bio = {};

	m0_be_io_init(&bio, &M0_BE_TX_CREDIT(1, size), 1);
	m0_be_io_add(&bio, ls->ls_stob, buf, 0, size);
	m0_be_io_configure(&bio, SIO_READ);

	M0_BE_OP_SYNC(op, m0_be_io_launch(&bio, &op));

	m0_be_io_fini(&bio);
}

static void
be_ut_log_store_rand_cr(struct m0_be_tx_credit *cr, m0_bcount_t size)
{
	int         buf[BE_UT_LOG_STOR_CR_NR];
	m0_bcount_t i;

	M0_SET0(cr);
	M0_SET_ARR0(buf);
	for (i = 0; i < size; ++i)
		++buf[be_ut_log_store_rand(ARRAY_SIZE(buf))];
	for (i = 0; i < ARRAY_SIZE(buf); ++i) {
		if (buf[i] != 0)
			m0_be_tx_credit_add(cr, &M0_BE_TX_CREDIT(1, buf[i]));
	}
	/* wrap credit */
	m0_be_tx_credit_add(cr, &M0_BE_TX_CREDIT(1, 0));
}

static void be_ut_log_store_io_write_sync(struct m0_be_io *bio)
{
	m0_be_io_configure(bio, SIO_WRITE);
	M0_BE_OP_SYNC(op, m0_be_io_launch(bio, &op));
}

static void
be_ut_log_store_io_check(struct m0_be_log_store *ls, m0_bcount_t size)
{
	struct m0_be_log_store_io lsi;
	struct m0_be_tx_credit    io_cr_log;
	struct m0_be_tx_credit    io_cr_log_cblock;
	struct m0_be_io           io_log = {};
	struct m0_be_io           io_log_cblock = {};
	m0_bcount_t               cblock_size;
	m0_bcount_t               data_size;
	int                       cmp;
	int                       rc;
	int                       i;
	char                      rbuf[BE_UT_LOG_STOR_SIZE];

	M0_PRE(size <= ARRAY_SIZE(rbuf));

	for (i = 0; i < size; ++i)
		rbuf[i] = be_ut_log_store_rand(0x100);

	cblock_size = be_ut_log_store_rand(size - 1) + 1;
	data_size   = size - cblock_size;
	M0_ASSERT(cblock_size > 0);
	M0_ASSERT(data_size > 0);

	be_ut_log_store_rand_cr(&io_cr_log, data_size);
	m0_be_log_store_cblock_io_credit(&io_cr_log_cblock, cblock_size);

	rc = m0_be_io_init(&io_log, &io_cr_log, 1);
	M0_UT_ASSERT(rc == 0);
	rc = m0_be_io_init(&io_log_cblock, &io_cr_log_cblock, 1);
	M0_UT_ASSERT(rc == 0);

	m0_be_log_store_io_init(&lsi, ls, &io_log, &io_log_cblock, size);

	/* save */
	be_ut_log_store_io_read(ls, be_ut_log_store_pre,
			       ARRAY_SIZE(be_ut_log_store_pre));
	/* log storage io */
	m0_be_log_store_io_add(&lsi, rbuf, data_size);
	m0_be_log_store_io_add_cblock(&lsi, &rbuf[data_size], cblock_size);
	m0_be_log_store_io_sort(&lsi);
	m0_be_log_store_io_fini(&lsi);
	be_ut_log_store_io_write_sync(&io_log);
	be_ut_log_store_io_write_sync(&io_log_cblock);
	/* do operation in saved memory representation of the log storage */
	for (i = 0; i < size; ++i) {
		be_ut_log_store_pre[(be_ut_log_store_pos + i) %
				   BE_UT_LOG_STOR_SIZE] = rbuf[i];
	}
	be_ut_log_store_pos += size;
	/* check if it was done in log */
	be_ut_log_store_io_read(ls, be_ut_log_store_post,
			       ARRAY_SIZE(be_ut_log_store_post));
	cmp = memcmp(be_ut_log_store_pre, be_ut_log_store_post, size);
	M0_UT_ASSERT(cmp == 0);

	m0_be_io_fini(&io_log_cblock);
	m0_be_io_fini(&io_log);
}

static void be_ut_log_store_io_check_nop(struct m0_be_log_store *ls,
					m0_bcount_t size)
{
	struct m0_be_log_store_io lsi = {};
	struct m0_be_io		  io_log = {};
	struct m0_be_io		  io_log_cblock = {};
	int			  rc;

	rc = m0_be_io_init(&io_log, &M0_BE_TX_CREDIT(1, 1), 1);
	M0_UT_ASSERT(rc == 0);
	rc = m0_be_io_init(&io_log_cblock, &M0_BE_TX_CREDIT(1, 1), 1);
	M0_UT_ASSERT(rc == 0);

	m0_be_log_store_io_init(&lsi, ls, &io_log, &io_log_cblock, size);
	m0_be_log_store_io_fini(&lsi);

	m0_be_io_fini(&io_log_cblock);
	m0_be_io_fini(&io_log);
}

static void be_ut_log_store(bool fake_io)
{
	struct m0_be_log_store	ls;
	struct m0_stob	       *stob;
	const m0_bcount_t     log_size = BE_UT_LOG_STOR_SIZE;
	m0_bcount_t	      used;
	m0_bcount_t	      step;
	int		      rc;
	int		      i;

	stob = m0_ut_stob_linux_get();
	M0_SET0(&ls);
	m0_be_log_store_init(&ls, stob);

	rc = m0_be_log_store_create(&ls, log_size);
	M0_UT_ASSERT(rc == 0);

	used = 0;
	be_ut_log_store_seed = 0;
	be_ut_log_store_pos = 0;
	for (step = 2; step <= BE_UT_LOG_STOR_STEP; ++step) {
		for (i = 0; i < BE_UT_LOG_STOR_ITER; ++i) {
			M0_UT_ASSERT(0 <= used && used <= BE_UT_LOG_STOR_SIZE);
			if (used + step <= log_size) {
				rc = m0_be_log_store_reserve(&ls, step);
				M0_UT_ASSERT(rc == 0);
				used += step;
				if (fake_io) {
					be_ut_log_store_io_check_nop(&ls, step);
				} else {
					be_ut_log_store_io_check(&ls, step);
				}
			} else {
				rc = m0_be_log_store_reserve(&ls, step);
				M0_UT_ASSERT(rc != 0);
				m0_be_log_store_discard(&ls, step);
				used -= step;
			}
		}
	}
	m0_be_log_store_discard(&ls, used);

	m0_be_log_store_destroy(&ls);
	m0_be_log_store_fini(&ls);
	m0_ut_stob_put(stob, true);
}

void m0_be_ut_log_store_reserve(void)
{
	be_ut_log_store(true);
}

void m0_be_ut_log_store_io(void)
{
	be_ut_log_store(false);
}

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
