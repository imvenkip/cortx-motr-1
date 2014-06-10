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

#include "be/io.h"

#include <unistd.h>		 /* fdatasync */

#include "lib/memory.h"		 /* m0_alloc */
#include "lib/errno.h"		 /* ENOMEM */

#include "stob/linux.h"		 /* m0_stob_linux_container */
#include "stob/io.h"		 /* m0_stob_iovec_sort */

#include "be/be.h"		 /* m0_be_op_state_set */

/**
 * @addtogroup be
 *
 * @{
 */

static bool be_io_cb(struct m0_clink *link);

static m0_bindex_t be_io_stob_offset_pack(m0_bindex_t offset, uint32_t bshift)
{
	return (m0_bindex_t)m0_stob_addr_pack((void *)offset, bshift);
}

static void be_io_free(struct m0_be_io *bio)
{
	m0_free(bio->bio_bv_user.ov_vec.v_count);
	m0_free(bio->bio_bv_user.ov_buf);
	m0_free(bio->bio_iv_stob.iv_vec.v_count);
	m0_free(bio->bio_iv_stob.iv_index);
	m0_free(bio->bio_part);
}

static void be_io_part_init(struct m0_be_io_part *bip,
			    struct m0_be_io	 *bio)
{
	bip->bip_bio = bio;
	m0_stob_io_init(&bip->bip_sio);
	m0_clink_init(&bip->bip_clink, be_io_cb);
}

static void be_io_part_fini(struct m0_be_io_part *bip)
{
	m0_clink_fini(&bip->bip_clink);
	m0_stob_io_fini(&bip->bip_sio);
}

static bool be_io_part_invariant(struct m0_be_io_part *bip)
{
	struct m0_stob_io *sio = &bip->bip_sio;

	return _0C(bip->bip_stob != NULL) &&
	       _0C(bip->bip_bio != NULL) &&
	       _0C(sio->si_user.ov_vec.v_nr == sio->si_stob.iv_vec.v_nr) &&
	       _0C(m0_vec_count(&sio->si_user.ov_vec) ==
		   m0_vec_count(&sio->si_stob.iv_vec));
}

static int be_io_part_launch(struct m0_be_io_part *bip)
{
	struct m0_stob_io *sio = &bip->bip_sio;
	int		   rc;

	m0_clink_add_lock(&sio->si_wait, &bip->bip_clink);

	M0_LOG(M0_DEBUG, "sio = %p, sio->si_user count = %"PRIu32" size = %lu",
	       sio, sio->si_user.ov_vec.v_nr,
	       (unsigned long)m0_vec_count(&sio->si_user.ov_vec));
	M0_LOG(M0_DEBUG, "sio = %p, sio->si_stob count = %"PRIu32" size = %lu",
	       sio, sio->si_stob.iv_vec.v_nr,
	       (unsigned long)m0_vec_count(&sio->si_stob.iv_vec));

	rc = m0_stob_io_launch(sio, bip->bip_stob, NULL, NULL);
	if (rc != 0)
		m0_clink_del_lock(&bip->bip_clink);
	return rc;
}

static void be_io_part_reset(struct m0_be_io_part *bip)
{
	struct m0_stob_io *sio = &bip->bip_sio;

	sio->si_user.ov_vec.v_nr = 0;
	sio->si_stob.iv_vec.v_nr = 0;
	sio->si_obj = NULL;

	bip->bip_stob	= NULL;
	bip->bip_bshift = 0;
	bip->bip_rc	= 0;
}

static bool be_io_part_add(struct m0_be_io_part *bip,
			   void			*ptr_user,
			   m0_bindex_t		 offset_stob,
			   m0_bcount_t		 size)
{
	struct m0_stob_io *sio	  = &bip->bip_sio;
	uint32_t	   bshift = bip->bip_bshift;
	void		 **u_buf  = sio->si_user.ov_buf;
	struct m0_vec	  *u_vec  = &sio->si_user.ov_vec;
	m0_bindex_t	  *s_offs = sio->si_stob.iv_index;
	struct m0_vec	  *s_vec  = &sio->si_stob.iv_vec;
	uint32_t	   nr	  = u_vec->v_nr;
	void		  *ptr_user_packed;
	m0_bindex_t	   offset_stob_packed;
	bool		   added;

	M0_PRE(u_vec->v_nr == s_vec->v_nr);

	ptr_user_packed	   = m0_stob_addr_pack(ptr_user, bshift);
	offset_stob_packed = be_io_stob_offset_pack(offset_stob, bshift);

	if (nr > 0 &&
	    u_buf [nr - 1] + u_vec->v_count[nr - 1] == ptr_user_packed &&
	    s_offs[nr - 1] + s_vec->v_count[nr - 1] == offset_stob_packed) {
		/* optimization for sequential regions */
		u_vec->v_count[nr - 1] += size;
		s_vec->v_count[nr - 1] += size;
		added = false;
	} else {
		u_buf[nr]	   = ptr_user_packed;
		u_vec->v_count[nr] = size;
		s_offs[nr]	   = offset_stob_packed;
		s_vec->v_count[nr] = size;

		++u_vec->v_nr;
		++s_vec->v_nr;
		added = true;
	}
	return added;
}

M0_INTERNAL int m0_be_io_init(struct m0_be_io		   *bio,
			      const struct m0_be_tx_credit *size_max,
			      size_t			    stob_nr_max)
{
	struct m0_bufvec   *bv = &bio->bio_bv_user;
	struct m0_indexvec *iv = &bio->bio_iv_stob;
	m0_bcount_t	    nr = size_max->tc_reg_nr;
	int		    rc;
	size_t		    i;

	bio->bio_credit	     = *size_max;
	bio->bio_stob_nr     = 0;
	bio->bio_stob_nr_max = stob_nr_max;

	M0_ALLOC_ARR(bv->ov_vec.v_count, nr);
	M0_ALLOC_ARR(bv->ov_buf,	 nr);
	M0_ALLOC_ARR(iv->iv_vec.v_count, nr);
	M0_ALLOC_ARR(iv->iv_index,	 nr);
	M0_ALLOC_ARR(bio->bio_part,	 stob_nr_max);

	if (bv->ov_vec.v_count == NULL ||
	    bv->ov_buf	       == NULL ||
	    iv->iv_vec.v_count == NULL ||
	    iv->iv_index       == NULL ||
	    bio->bio_part      == NULL) {
		be_io_free(bio);
		rc = -ENOMEM;
	} else {
		for (i = 0; i < bio->bio_stob_nr_max; ++i)
			be_io_part_init(&bio->bio_part[i], bio);
		m0_be_io_reset(bio);
		rc = 0;
	}
	M0_POST(ergo(rc == 0, m0_be_io__invariant(bio)));
	return rc;
}

M0_INTERNAL void m0_be_io_fini(struct m0_be_io *bio)
{
	unsigned i;

	M0_PRE(m0_be_io__invariant(bio));

	for (i = 0; i < bio->bio_stob_nr_max; ++i)
		be_io_part_fini(&bio->bio_part[i]);
	be_io_free(bio);
}

M0_INTERNAL bool m0_be_io__invariant(struct m0_be_io *bio)
{
	struct m0_be_io_part *bip;
	struct m0_stob_io    *sio;
	struct m0_bufvec     *bv = &bio->bio_bv_user;
	struct m0_indexvec   *iv = &bio->bio_iv_stob;
	m0_bcount_t	      count_total = 0;
	uint32_t	      nr_total	  = 0;
	unsigned	      i;
	uint32_t	      pos = 0;

	for (i = 0; i < bio->bio_stob_nr; ++i) {
		bip	     = &bio->bio_part[i];
		sio	     = &bip->bip_sio;
		nr_total    += sio->si_user.ov_vec.v_nr;
		count_total += m0_vec_count(&sio->si_user.ov_vec);
		if (be_io_part_invariant(bip) &&
		    _0C(sio->si_user.ov_vec.v_count ==
			&bv->ov_vec.v_count[pos]) &&
		    _0C(sio->si_user.ov_buf == &bv->ov_buf[pos]) &&
		    _0C(sio->si_stob.iv_vec.v_count ==
			&iv->iv_vec.v_count[pos]) &&
		    _0C(sio->si_stob.iv_index == &iv->iv_index[pos])) {

			pos = nr_total;
			continue;
		}
		return false;
	}

	return _0C(bio != NULL) &&
	       _0C(m0_be_tx_credit_le(&bio->bio_used, &bio->bio_credit)) &&
	       _0C(bio->bio_stob_nr <= bio->bio_stob_nr_max) &&
	       _0C(m0_atomic64_get(&bio->bio_stob_io_finished_nr) <=
		   bio->bio_stob_nr) &&
	       _0C(nr_total    <= bio->bio_used.tc_reg_nr) &&
	       _0C(count_total <= bio->bio_used.tc_reg_size);
}

static void be_io_vec_cut(struct m0_be_io      *bio,
			  struct m0_be_io_part *bip)
{
	struct m0_stob_io  *sio = &bip->bip_sio;
	struct m0_bufvec   *bv	= &bio->bio_bv_user;
	struct m0_indexvec *iv	= &bio->bio_iv_stob;
	uint32_t	    pos = bio->bio_vec_pos;

	sio->si_user.ov_vec.v_count = &bv->ov_vec.v_count[pos];
	sio->si_user.ov_buf	    = &bv->ov_buf[pos];
	sio->si_stob.iv_vec.v_count = &iv->iv_vec.v_count[pos];
	sio->si_stob.iv_index	    = &iv->iv_index[pos];
}

M0_INTERNAL void m0_be_io_add(struct m0_be_io *bio,
			      struct m0_stob  *stob,
			      void	      *ptr_user,
			      m0_bindex_t      offset_stob,
			      m0_bcount_t      size)
{
	struct m0_be_io_part *bip;
	bool		      added;

	M0_PRE(m0_be_io__invariant(bio));
	M0_PRE(bio->bio_used.tc_reg_size + size <= bio->bio_credit.tc_reg_size);
	M0_PRE(bio->bio_used.tc_reg_nr	 + 1	<= bio->bio_credit.tc_reg_nr);

	if (bio->bio_stob_nr == 0 ||
	    bio->bio_part[bio->bio_stob_nr - 1].bip_stob != stob) {
		M0_ASSERT(bio->bio_stob_nr < bio->bio_stob_nr_max);
		++bio->bio_stob_nr;
		bip = &bio->bio_part[bio->bio_stob_nr - 1];
		bip->bip_stob	= stob;
		bip->bip_bshift = m0_stob_block_shift(stob);
		be_io_vec_cut(bio, bip);
	}
	bip = &bio->bio_part[bio->bio_stob_nr - 1];
	added = be_io_part_add(bip, ptr_user, offset_stob, size);
	/* m0_be_io::bio_used is calculated for the worst-case */
	m0_be_tx_credit_add(&bio->bio_used, &M0_BE_TX_CREDIT(1, size));
	bio->bio_vec_pos += added;

	M0_POST(m0_be_io__invariant(bio));
}

M0_INTERNAL void m0_be_io_configure(struct m0_be_io	   *bio,
				    enum m0_stob_io_opcode  opcode)
{
	struct m0_stob_io *sio;
	unsigned	   i;

	for (i = 0; i < bio->bio_stob_nr; ++i) {
		sio = &bio->bio_part[i].bip_sio;
		sio->si_opcode   = opcode;
		sio->si_fol_frag = (void *)1; /* XXX */
	}
}

static void be_io_finished(struct m0_be_io *bio)
{
	struct m0_be_op	     *op = bio->bio_op;
	uint64_t	      finished_nr;
	unsigned	      i;
	int		      rc;

	finished_nr = m0_atomic64_add_return(&bio->bio_stob_io_finished_nr, 1);
	/*
	 * Next `if' body will be executed only in the last finished stob I/O
	 * callback.
	 */
	if (finished_nr == bio->bio_stob_nr) {
		rc = 0;
		for (i = 0; i < bio->bio_stob_nr; ++i) {
			rc = bio->bio_part[i].bip_rc ?: rc;
			if (rc != 0) {
				M0_LOG(M0_INFO,
				       "failed I/O part number: %u, rc = %d",
				       i, rc);
				break;
			}
		}
		op->bo_sm.sm_rc = rc;
		m0_be_op_state_set(op, M0_BOS_SUCCESS);
	}
}

static bool be_io_cb(struct m0_clink *link)
{
	struct m0_be_io_part *bip = container_of(link,
						 struct m0_be_io_part,
						 bip_clink);
	struct m0_be_io	     *bio = bip->bip_bio;
	struct m0_stob_io    *sio = &bip->bip_sio;
	int		      rc;

	m0_clink_del(&bip->bip_clink);
	/* XXX Temporary hack. I/O error should be handled gracefully. */
	M0_ASSERT_INFO(sio->si_rc == 0, "stob I/O operation failed: "
		       "bio = %p, sio = %p, sio->si_rc = %d",
		       bio, sio, sio->si_rc);
	rc = sio->si_rc;

	/* XXX temporary hack:
	 * - sync() should be implemented on stob level or at least
	 * - sync() shoudn't be called from linux stob worker thread as it is
	 *   now.
	 */
	if (rc == 0 && bio->bio_sync) {
		rc = fdatasync(
			m0_stob_linux_container(bip->bip_sio.si_obj)->sl_fd);
		M0_ASSERT_INFO(rc == 0, "fdatasync() failed: %d", rc);
	}
	bip->bip_rc = rc;
	be_io_finished(bio);
	return rc == 0;
}

M0_INTERNAL void m0_be_io_launch(struct m0_be_io *bio, struct m0_be_op *op)
{
	unsigned i;
	int	 rc;

	M0_PRE(m0_be_io__invariant(bio));

	bio->bio_op = op;
	m0_be_op_state_set(op, M0_BOS_ACTIVE);

	rc = 0;
	for (i = 0; i < bio->bio_stob_nr; ++i) {
		if (rc == 0)
			rc = be_io_part_launch(&bio->bio_part[i]);
		if (rc != 0)
			be_io_finished(bio);
	}
}

M0_INTERNAL void m0_be_io_sync_enable(struct m0_be_io *bio)
{
	bio->bio_sync = true;
}

M0_INTERNAL void m0_be_io_reset(struct m0_be_io *bio)
{
	unsigned i;

	for (i = 0; i < bio->bio_stob_nr; ++i)
		be_io_part_reset(&bio->bio_part[i]);
	m0_atomic64_set(&bio->bio_stob_io_finished_nr, 0);
	bio->bio_vec_pos = 0;
	bio->bio_used	 = M0_BE_TX_CREDIT(0, 0);
	bio->bio_stob_nr = 0;
	bio->bio_sync	 = false;
}

M0_INTERNAL void m0_be_io_sort(struct m0_be_io *bio)
{
	unsigned i;

	for (i = 0; i < bio->bio_stob_nr; ++i)
		m0_stob_iovec_sort(&bio->bio_part[i].bip_sio);
}

M0_INTERNAL int m0_be_io_single(struct m0_stob	       *stob,
				enum m0_stob_io_opcode	opcode,
				void		       *ptr_user,
				m0_bindex_t		offset_stob,
				m0_bcount_t		size)
{
	struct m0_be_io bio = {};
	int             rc;

	rc = m0_be_io_init(&bio, &M0_BE_TX_CREDIT(1, size), 1);
	if (rc == 0) {
		m0_be_io_add(&bio, stob, ptr_user, offset_stob, size);
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
