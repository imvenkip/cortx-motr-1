/* -*- C -*- */
/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 12-Mar-2014
 */

#include "stob/io.h"

#include "stob/stob.h"		/* m0_stob_state_get */
#include "stob/domain.h"	/* m0_stob_domain_id_get */

/**
 * @addtogroup stob
 *
 * @{
 */

static void m0_stob_io_private_fini(struct m0_stob_io *io)
{
	if (io->si_stob_private != NULL) {
		io->si_op->sio_fini(io);
		io->si_stob_private = NULL;
	}
}

M0_INTERNAL void m0_stob_io_init(struct m0_stob_io *io)
{
	M0_SET0(io);

	io->si_opcode = SIO_INVALID;
	io->si_state  = SIS_IDLE;
	m0_mutex_init(&io->si_mutex);
	m0_chan_init(&io->si_wait, &io->si_mutex);

	M0_POST(io->si_state == SIS_IDLE);
}

M0_INTERNAL void m0_stob_io_fini(struct m0_stob_io *io)
{
	M0_PRE(io->si_state == SIS_IDLE);

	m0_chan_fini_lock(&io->si_wait);
	m0_mutex_fini(&io->si_mutex);
	m0_stob_io_private_fini(io);
}

M0_INTERNAL int m0_stob_io_launch(struct m0_stob_io *io, struct m0_stob *obj,
				  struct m0_dtx *tx, struct m0_io_scope *scope)
{
	uint8_t type_id;
	int	result;

	M0_PRE(m0_stob_state_get(obj) == CSS_EXISTS);
	M0_PRE(m0_chan_has_waiters(&io->si_wait));
	M0_PRE(io->si_obj == NULL);
	M0_PRE(io->si_state == SIS_IDLE);
	M0_PRE(io->si_opcode != SIO_INVALID);
	M0_PRE(m0_vec_count(&io->si_user.ov_vec) ==
	       m0_vec_count(&io->si_stob.iv_vec));
	M0_PRE(m0_stob_io_user_is_valid(&io->si_user));
	M0_PRE(m0_stob_io_stob_is_valid(&io->si_stob));
	M0_PRE(ergo(io->si_opcode == SIO_WRITE, io->si_fol_frag != NULL));

	type_id = m0_stob_domain__type_id(
			m0_stob_domain_id_get(m0_stob_dom_get(obj)));
	if (io->si_stob_magic != type_id) {
		m0_stob_io_private_fini(io);
		result = obj->so_ops->sop_io_init(obj, io);
		io->si_stob_magic = type_id;
	} else
		result = 0;

	if (result == 0) {
		io->si_obj   = obj;
		io->si_tx    = tx;
		io->si_scope = scope;
		io->si_state = SIS_BUSY;
		io->si_rc    = 0;
		io->si_count = 0;
		result = io->si_op->sio_launch(io);
		if (result != 0)
			io->si_state = SIS_IDLE;
	}
	M0_POST(ergo(result != 0, io->si_state == SIS_IDLE));
	return result;
}

M0_INTERNAL bool m0_stob_io_user_is_valid(const struct m0_bufvec *user)
{
	return true;
}

M0_INTERNAL bool m0_stob_io_stob_is_valid(const struct m0_indexvec *stob)
{
	uint32_t    i;
	m0_bindex_t reached;

	for (reached = 0, i = 0; i < stob->iv_vec.v_nr; ++i) {
		if (stob->iv_index[i] < reached)
			return false;
		reached = stob->iv_index[i] + stob->iv_vec.v_count[i];
	}
	return true;
}

M0_INTERNAL void *m0_stob_addr_pack(const void *buf, uint32_t shift)
{
	uint64_t addr = (uint64_t)buf;

	M0_PRE(((addr >> shift) << shift) == addr);
	return (void *)(addr >> shift);
}

M0_INTERNAL void *m0_stob_addr_open(const void *buf, uint32_t shift)
{
	uint64_t addr = (uint64_t)buf;

	M0_PRE(((addr << shift) >> shift) == addr);
	return (void *)(addr << shift);
}

M0_INTERNAL void m0_stob_iovec_sort(struct m0_stob_io *stob)
{
	struct m0_indexvec *ivec = &stob->si_stob;
	struct m0_bufvec   *bvec = &stob->si_user;
	int		    i;
	bool		    exchanged;
	bool		    different_count;

#define SWAP_NEXT(arr, idx)			\
({						\
	int               _idx = (idx);		\
	typeof(&arr[idx]) _arr = (arr);		\
	typeof(arr[idx])  _tmp;			\
						\
	_tmp           = _arr[_idx];		\
	_arr[_idx]     = _arr[_idx + 1];	\
	_arr[_idx + 1] = _tmp;			\
})

	different_count = ivec->iv_vec.v_count != bvec->ov_vec.v_count;

	/*
	 * Bubble sort the index vectores.
	 * It also move bufvecs while sorting.
	 */
	do {
		exchanged = false;
		for (i = 0; i < ivec->iv_vec.v_nr - 1; i++) {
			if (ivec->iv_index[i] > ivec->iv_index[i + 1]) {

				SWAP_NEXT(ivec->iv_index, i);
				SWAP_NEXT(ivec->iv_vec.v_count, i);
				SWAP_NEXT(bvec->ov_buf, i);
				if (different_count)
					SWAP_NEXT(bvec->ov_vec.v_count, i);
				exchanged = true;
			}
		}
	} while (exchanged);

#undef SWAP_NEXT
}


/** @} end of stob group */

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
