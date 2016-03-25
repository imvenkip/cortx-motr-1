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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_STOB
#include "lib/errno.h"         /* ENOMEM */
#include "lib/memory.h"
#include "lib/trace.h"
#include "addb2/addb2.h"
#include "fol/fol.h"           /* m0_fol_frag */

#include "stob/io.h"
#include "stob/stob.h"         /* m0_stob_state_get */
#include "stob/domain.h"       /* m0_stob_domain_id_get */
#include "stob/addb2.h"

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

M0_INTERNAL void m0_stob_io_credit(const struct m0_stob_io *io,
				   const struct m0_stob_domain *dom,
				   struct m0_be_tx_credit *accum)
{
	M0_PRE(io->si_opcode == SIO_WRITE);
	dom->sd_ops->sdo_stob_write_credit(dom, io, accum);
}

M0_INTERNAL int m0_stob_io_launch(struct m0_stob_io *io, struct m0_stob *obj,
				  struct m0_dtx *tx, struct m0_io_scope *scope)
{
	const struct m0_fid *fid = m0_stob_fid_get(obj);
	struct m0_indexvec  *iv  = &io->si_stob;
	struct m0_bufvec    *bv  = &io->si_user;
	uint8_t              type_id;
	int                  result;

	M0_PRE(m0_stob_state_get(obj) == CSS_EXISTS);
	M0_PRE(m0_chan_has_waiters(&io->si_wait));
	M0_PRE(io->si_obj == NULL);
	M0_PRE(io->si_state == SIS_IDLE);
	M0_PRE(io->si_opcode != SIO_INVALID);
	M0_PRE(m0_vec_count(&bv->ov_vec) == m0_vec_count(&iv->iv_vec));
	M0_PRE(m0_stob_io_user_is_valid(bv));
	M0_PRE(m0_stob_io_stob_is_valid(iv));
	M0_PRE(ergo(io->si_opcode == SIO_WRITE, io->si_fol_frag != NULL));

	M0_ADDB2_ADD(M0_AVI_STOB_IO_LAUNCH, FID_P(fid),
		     m0_vec_count(&bv->ov_vec),
		     bv->ov_vec.v_nr, iv->iv_vec.v_nr, iv->iv_index[0]);
	M0_ADDB2_PUSH(M0_AVI_STOB_IO_LAUNCH, FID_P(fid),
		      m0_vec_count(&bv->ov_vec),
		      bv->ov_vec.v_nr, iv->iv_vec.v_nr, iv->iv_index[0]);
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
		if (result != 0) {
			M0_LOG(M0_ERROR, "launch io=%p "FID_F" FAILED rc=%d",
					 io, FID_P(fid), result);
			io->si_state = SIS_IDLE;
		}
	}
	m0_addb2_pop(M0_AVI_STOB_IO_LAUNCH);
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

M0_INTERNAL int m0_stob_io_bufvec_launch(struct m0_stob   *stob,
					 struct m0_bufvec *bufvec,
					 int               op_code,
					 m0_bindex_t       offset)
{
	int                 rc;
	struct m0_stob_io   io;
	struct m0_fol_frag *fol_frag;
	struct m0_clink     clink;
	m0_bcount_t         count;
	m0_bindex_t         offset_idx = offset;

	M0_PRE(stob != NULL);
	M0_PRE(bufvec != NULL);
	M0_PRE(M0_IN(op_code, (SIO_READ, SIO_WRITE)));

	count = m0_vec_count(&bufvec->ov_vec);

	M0_ALLOC_PTR(fol_frag);
	if (fol_frag == NULL)
		return M0_ERR(-ENOMEM);

	m0_stob_io_init(&io);

	io.si_opcode = op_code;
	io.si_flags  = 0;
	io.si_fol_frag = fol_frag;
	io.si_user.ov_vec.v_nr = bufvec->ov_vec.v_nr;
	io.si_user.ov_vec.v_count = bufvec->ov_vec.v_count;
	io.si_user.ov_buf = bufvec->ov_buf;
	io.si_stob = (struct m0_indexvec) {
		.iv_vec = { .v_nr = 1, .v_count = &count },
		.iv_index = &offset_idx
	};

	m0_clink_init(&clink, NULL);
	m0_clink_add_lock(&io.si_wait, &clink);

	rc = m0_stob_io_launch(&io, stob, NULL, NULL);
	if (rc == 0) {
		m0_chan_wait(&clink);
		rc = io.si_rc;
	}

	m0_clink_del_lock(&clink);
	m0_clink_fini(&clink);

	m0_stob_io_fini(&io);

	return M0_RC(rc);
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
	int                 i;
	bool                exchanged;
	bool                different_count;

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

#undef M0_TRACE_SUBSYSTEM

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
