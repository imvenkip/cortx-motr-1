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

#include "be/io.h"

#include "lib/memory.h"		/* m0_alloc */
#include "lib/errno.h"		/* ENOMEM */

#include "be/be.h"		/* m0_be_op_state_set */

/**
 * @addtogroup be
 *
 * @{
 */

static bool be_io_cb(struct m0_clink *link);

static void be_io_free(struct m0_be_io *bio)
{
	struct m0_stob_io *io = &bio->bio_io;

	m0_free(io->si_user.ov_vec.v_count);
	m0_free(io->si_user.ov_buf);
	m0_free(io->si_stob.iv_vec.v_count);
	m0_free(io->si_stob.iv_index);
}

M0_INTERNAL int m0_be_io_init(struct m0_be_io *bio,
			      struct m0_stob *stob,
			      const struct m0_be_tx_credit *size_max)
{
	struct m0_stob_io *io = &bio->bio_io;
	m0_bcount_t	   nr = size_max->tc_reg_nr;
	int		   rc;

	*bio = (struct m0_be_io) {
		.bio_stob = stob,
		.bio_bshift = stob->so_op->sop_block_shift(stob),
		.bio_credit = *size_max,
	};
	m0_stob_io_init(io);

	M0_ALLOC_ARR(io->si_user.ov_vec.v_count, nr);
	M0_ALLOC_ARR(io->si_user.ov_buf, nr);
	M0_ALLOC_ARR(io->si_stob.iv_vec.v_count, nr);
	M0_ALLOC_ARR(io->si_stob.iv_index, nr);

	if (io->si_user.ov_vec.v_count == NULL ||
	    io->si_user.ov_buf == NULL ||
	    io->si_stob.iv_vec.v_count == NULL ||
	    io->si_stob.iv_index == NULL) {
		be_io_free(bio);
		m0_stob_io_fini(io);
		rc = -ENOMEM;
	} else {
		m0_clink_init(&bio->bio_clink, be_io_cb);
		rc = 0;
	}
	M0_POST(ergo(rc == 0, m0_be_io__invariant(bio)));
	return rc;
}

M0_INTERNAL void m0_be_io_fini(struct m0_be_io *bio)
{
	M0_PRE(m0_be_io__invariant(bio));

	m0_clink_fini(&bio->bio_clink);
	be_io_free(bio);
	m0_stob_io_fini(&bio->bio_io);
}

M0_INTERNAL bool m0_be_io__invariant(struct m0_be_io *bio)
{
	struct m0_stob_io *io = &bio->bio_io;

	return bio != NULL &&
	       io->si_user.ov_vec.v_nr == io->si_stob.iv_vec.v_nr &&
			m0_vec_count(&io->si_user.ov_vec) ==
			m0_vec_count(&io->si_stob.iv_vec) &&
	       io->si_user.ov_vec.v_nr <= bio->bio_credit.tc_reg_nr &&
	       m0_vec_count(&io->si_user.ov_vec) <= bio->bio_credit.tc_reg_size;
	/* XXX m0_vec_count(&io->si_user.ov_vec) is called two times here. */
}

M0_INTERNAL void m0_be_io_add(struct m0_be_io *bio,
			      void *ptr_user,
			      m0_bindex_t offset_stob,
			      m0_bcount_t size)
{
	struct m0_stob_io *io	  = &bio->bio_io;
	m0_bcount_t	   v_size = m0_vec_count(&io->si_user.ov_vec);
	uint32_t	   nr	  = io->si_user.ov_vec.v_nr;
	uint32_t	   bshift = bio->bio_bshift;

	M0_PRE(m0_be_io__invariant(bio));
	M0_PRE(v_size + size <= bio->bio_credit.tc_reg_size);
	M0_PRE(nr + 1	     <= bio->bio_credit.tc_reg_nr);

	io->si_user.ov_buf[nr] = m0_stob_addr_pack(ptr_user, bshift);
	io->si_user.ov_vec.v_count[nr] = size;
	io->si_stob.iv_index[nr] =
		(m0_bindex_t) m0_stob_addr_pack((void *) offset_stob, bshift);
	io->si_stob.iv_vec.v_count[nr] = size;

	++io->si_user.ov_vec.v_nr;
	++io->si_stob.iv_vec.v_nr;

	M0_POST(m0_be_io__invariant(bio));
}

M0_INTERNAL void m0_be_io_configure(struct m0_be_io *bio,
				    enum m0_stob_io_opcode opcode)
{
	bio->bio_io.si_opcode = opcode;
	/* XXX */
	bio->bio_io.si_fol_rec_part = (void *) 1;
}

static bool be_io_cb(struct m0_clink *link)
{
	struct m0_be_io	  *bio = container_of(link, struct m0_be_io, bio_clink);
	struct m0_be_op	  *op = bio->bio_op;
	struct m0_stob_io *io = &bio->bio_io;

	m0_clink_del(&bio->bio_clink);
	op->bo_sm.sm_rc = io->si_rc;
	m0_be_op_state_set(op,
			   io->si_rc == 0 ? M0_BOS_SUCCESS : M0_BOS_FAILURE);
	/* XXX add fsync() to linux stob fd
	 * stob2linux
	 * fd
	 * fsync()
	 */
	return io->si_rc == 0;
}

M0_INTERNAL void m0_be_io_launch(struct m0_be_io *bio, struct m0_be_op *op)
{
	struct m0_stob_io *io = &bio->bio_io;
	int		   rc;

	M0_PRE(m0_be_io__invariant(bio));

	bio->bio_op = op;
	m0_be_op_state_set(op, M0_BOS_ACTIVE);

	m0_clink_add_lock(&io->si_wait, &bio->bio_clink);

	rc = m0_stob_io_launch(io, bio->bio_stob,
			       NULL /* XXX */, NULL /* XXX */);
	if (rc != 0) {
		m0_clink_del_lock(&bio->bio_clink);
		op->bo_sm.sm_rc = rc;
		m0_be_op_state_set(op, M0_BOS_FAILURE);
	}
}

M0_INTERNAL void m0_be_io_reset(struct m0_be_io *bio)
{
	struct m0_stob_io *io = &bio->bio_io;

	io->si_user.ov_vec.v_nr = 0;
	io->si_stob.iv_vec.v_nr = 0;
	/* XXX hack. proper reset should:
	 * - save allocated vectors
	 * - m0_stob_io_fini()
	 * - m0_stob_io_init()
	 * - restore allocated vectors
	 */
	io->si_obj = NULL;
}

M0_INTERNAL int m0_be_io_sync(struct m0_stob *stob,
			      enum m0_stob_io_opcode opcode,
			      void *ptr_user,
			      m0_bindex_t offset_stob,
			      m0_bcount_t size)
{
	struct m0_be_io bio;
	int             rc;

	rc = m0_be_io_init(&bio, stob, &M0_BE_TX_CREDIT(1, size));
	if (rc == 0) {
		m0_be_io_add(&bio, ptr_user, offset_stob, size);
		m0_be_io_configure(&bio, opcode);
		M0_BE_OP_SYNC(op, m0_be_io_launch(&bio, &op));
		m0_be_io_fini(&bio);
	}
	return rc;
}

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
