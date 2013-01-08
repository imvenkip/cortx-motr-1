/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 04/28/2010
 */

/*
 * Define the ADDB types in this file.
 */
#undef M0_ADDB_CT_CREATE_DEFINITION
#define M0_ADDB_CT_CREATE_DEFINITION
#undef M0_ADDB_RT_CREATE_DEFINITION
#define M0_ADDB_RT_CREATE_DEFINITION
#include "stob/stob_addb.h"

#include "lib/misc.h"   /* M0_SET0 */
#include "lib/cdefs.h"
#include "lib/arith.h"   /* M0_3WAY */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_STOB
#include "lib/trace.h"

#include "stob/stob.h"

/**
   @addtogroup stob
   @{
 */

M0_TL_DESCR_DEFINE(dom, "stob domains", static, struct m0_stob_domain,
		   sd_domain_linkage, sd_magic, 0xABD1CAB1EAB5CE55,
		   0xACCE551B1EEFFACE);
M0_TL_DEFINE(dom, static, struct m0_stob_domain);

struct m0_addb_ctx m0_stob_mod_ctx;

M0_INTERNAL int m0_stob_mod_init(void)
{
	m0_addb_ctx_type_register(&m0_addb_ct_stob_mod);
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &m0_stob_mod_ctx,
			 &m0_addb_ct_stob_mod, &m0_addb_proc_ctx);
	return 0;
}

M0_INTERNAL void m0_stob_mod_fini(void)
{
        m0_addb_ctx_fini(&m0_stob_mod_ctx);
}

M0_INTERNAL int m0_stob_type_init(struct m0_stob_type *kind)
{
	dom_tlist_init(&kind->st_domains);
	/** @todo m0_addb_ctx_init(&kind->st_addb, &m0_stob_type_addb,
			 &m0_addb_global_ctx);
	 */
	return 0;
}

M0_INTERNAL void m0_stob_type_fini(struct m0_stob_type *kind)
{
	/** @todo m0_addb_ctx_fini(&kind->st_addb); */
	dom_tlist_fini(&kind->st_domains);
}

M0_INTERNAL int m0_stob_domain_locate(struct m0_stob_type *type,
				      const char *domain_name,
				      struct m0_stob_domain **dom)
{
	return M0_STOB_TYPE_OP(type, sto_domain_locate, domain_name, dom);
}

M0_INTERNAL void m0_stob_domain_init(struct m0_stob_domain *dom,
				     struct m0_stob_type *t)
{
	m0_rwlock_init(&dom->sd_guard);
	dom->sd_type = t;
	dom_tlink_init_at_tail(dom, &t->st_domains);
	/** @todo m0_addb_ctx_init(&dom->sd_addb, &m0_stob_domain_addb,
	    &t->st_addb);
	 */
}

M0_INTERNAL void m0_stob_domain_fini(struct m0_stob_domain *dom)
{
	/** @todo m0_addb_ctx_fini(&dom->sd_addb); */
	m0_rwlock_fini(&dom->sd_guard);
	dom_tlink_del_fini(dom);
	dom->sd_magic = 0;
}

M0_INTERNAL int m0_stob_find(struct m0_stob_domain *dom,
			     const struct m0_stob_id *id, struct m0_stob **out)
{
	return dom->sd_ops->sdo_stob_find(dom, id, out);
}

M0_INTERNAL void m0_stob_init(struct m0_stob *obj, const struct m0_stob_id *id,
			      struct m0_stob_domain *dom)
{
	m0_atomic64_set(&obj->so_ref, 1);
	obj->so_state = CSS_UNKNOWN;
	obj->so_id = *id;
	obj->so_domain = dom;
	/** @todo m0_addb_ctx_init(&obj->so_addb, &m0_stob_addb,
	    &dom->sd_addb);
	 */
}

M0_INTERNAL void m0_stob_fini(struct m0_stob *obj)
{
	/** @todo m0_addb_ctx_fini(&obj->so_addb); */
}

M0_BASSERT(sizeof(struct m0_uint128) == sizeof(struct m0_stob_id));

M0_INTERNAL int m0_stob_locate(struct m0_stob *obj, struct m0_dtx *tx)
{
	int result;

	switch (obj->so_state) {
	case CSS_UNKNOWN:
		result = obj->so_op->sop_locate(obj, tx);
		switch (result) {
		case 0:
			obj->so_state = CSS_EXISTS;
			break;
		case -ENOENT:
			obj->so_state = CSS_NOENT;
			break;
		}
		break;
	case CSS_EXISTS:
		result = 0;
		break;
	case CSS_NOENT:
		result = -ENOENT;
		break;
	default:
		M0_IMPOSSIBLE("invalid object state");
		result = -EINVAL;
		break;
	}
	M0_POST(ergo(result == 0, obj->so_state == CSS_EXISTS));
	M0_POST(ergo(result == -ENOENT, obj->so_state == CSS_NOENT));
	return result;
}

M0_INTERNAL int m0_stob_create(struct m0_stob *obj, struct m0_dtx *tx)
{
	int result;

	switch (obj->so_state) {
	case CSS_UNKNOWN:
	case CSS_NOENT:
		result = obj->so_op->sop_create(obj, tx);
		if (result == 0)
			obj->so_state = CSS_EXISTS;
		break;
	case CSS_EXISTS:
		result = 0;
		break;
	default:
		M0_IMPOSSIBLE("invalid object state");
		result = -EINVAL;
		break;
	}
	M0_POST(ergo(result == 0, obj->so_state == CSS_EXISTS));
	return result;
}

M0_INTERNAL void m0_stob_get(struct m0_stob *obj)
{
	m0_atomic64_inc(&obj->so_ref);
	M0_LEAVE("ref: %lu", (unsigned long)m0_atomic64_get(&obj->so_ref));
}

M0_INTERNAL void m0_stob_put(struct m0_stob *obj)
{
	struct m0_stob_domain *dom;

	M0_ENTRY("ref: %lu", (unsigned long)m0_atomic64_get(&obj->so_ref));

	dom = obj->so_domain;
	m0_rwlock_write_lock(&dom->sd_guard);
	if (m0_atomic64_dec_and_test(&obj->so_ref))
		obj->so_op->sop_fini(obj);
	m0_rwlock_write_unlock(&dom->sd_guard);
}

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
	m0_chan_init(&io->si_wait);

	M0_POST(io->si_state == SIS_IDLE);
}

M0_INTERNAL void m0_stob_io_fini(struct m0_stob_io *io)
{
	M0_PRE(io->si_state == SIS_IDLE);

	m0_chan_fini(&io->si_wait);
	m0_stob_io_private_fini(io);
}

M0_INTERNAL int m0_stob_io_launch(struct m0_stob_io *io, struct m0_stob *obj,
				  struct m0_dtx *tx, struct m0_io_scope *scope)
{
	int result;

	M0_PRE(obj->so_state == CSS_EXISTS);
	M0_PRE(m0_chan_has_waiters(&io->si_wait));
	M0_PRE(io->si_obj == NULL);
	M0_PRE(io->si_state == SIS_IDLE);
	M0_PRE(io->si_opcode != SIO_INVALID);
	M0_PRE(m0_vec_count(&io->si_user.ov_vec) ==
	       m0_vec_count(&io->si_stob.iv_vec));
	M0_PRE(m0_stob_io_user_is_valid(&io->si_user));
	M0_PRE(m0_stob_io_stob_is_valid(&io->si_stob));

	if (io->si_stob_magic != obj->so_domain->sd_type->st_magic) {
		m0_stob_io_private_fini(io);
		result = obj->so_op->sop_io_init(obj, io);
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
			io->si_state = SIS_IDLE;
		}
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

M0_INTERNAL int m0_stob_create_helper(struct m0_stob_domain *dom,
				      struct m0_dtx *dtx,
				      const struct m0_stob_id *stob_id,
				      struct m0_stob **out)
{
	struct m0_stob *stob;
	int             rc;

	rc = m0_stob_find(dom, stob_id, &stob);
	if (rc == 0) {
		/*
		 * Here, stob != NULL and m0_stob_find() has taken reference on
		 * stob. On error must call m0_stob_put() on stob, after this
		 * point.
		 */
		if (stob->so_state == CSS_UNKNOWN)
			rc = m0_stob_locate(stob, dtx);
		if (stob->so_state == CSS_NOENT)
			rc = m0_stob_create(stob, dtx);

		*out = stob->so_state == CSS_EXISTS ? stob : NULL;
		if (rc != 0)
			m0_stob_put(stob);
	}
	return rc;
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

/** @} end group stob */

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
