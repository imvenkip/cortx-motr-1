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

#ifndef __MERO_BE_IO_H__
#define __MERO_BE_IO_H__

#include "lib/chan.h"		/* m0_clink */
#include "lib/types.h"		/* m0_bcount_t */

#include "stob/io.h"		/* m0_stob_io */

#include "be/tx_credit.h"	/* m0_be_tx_credit */


/**
 * @defgroup be
 *
 * @{
 */

struct m0_stob;

struct m0_be_io {
	struct m0_stob	       *bio_stob;
	uint32_t		bio_bshift;
	struct m0_stob_io	bio_io;
	/** clink signalled when @bio_io is completed */
	struct m0_clink		bio_clink;
	struct m0_be_tx_credit	bio_credit;
	/**
	 * operation passed by the user on which log has to signal when io of
	 * this group is completed
	 */
	struct m0_be_op	       *bio_op;
	bool			bio_sync;
};

M0_INTERNAL int m0_be_io_init(struct m0_be_io *bio,
			      struct m0_stob *stob,
			      const struct m0_be_tx_credit *size_max);
M0_INTERNAL void m0_be_io_fini(struct m0_be_io *bio);
M0_INTERNAL bool m0_be_io__invariant(struct m0_be_io *bio);


M0_INTERNAL void m0_be_io_add(struct m0_be_io *bio,
			      void *ptr_user,
			      m0_bindex_t offset_stob,
			      m0_bcount_t size);

/** call fdatasync() for linux stob after IO completion */
M0_INTERNAL void m0_be_io_sync_enable(struct m0_be_io *bio);

M0_INTERNAL void m0_be_io_configure(struct m0_be_io *bio,
				    enum m0_stob_io_opcode opcode);

M0_INTERNAL void m0_be_io_launch(struct m0_be_io *bio, struct m0_be_op *op);

M0_INTERNAL void m0_be_io_reset(struct m0_be_io *bio);

M0_INTERNAL int m0_be_io_single(struct m0_stob *stob,
				enum m0_stob_io_opcode opcode,
				void *ptr_user,
				m0_bindex_t offset_stob,
				m0_bcount_t size);

/** @} end of be group */

#endif /* __MERO_BE_IO_H__ */


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
