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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/cdefs.h"
#include "lib/arith.h"   /* C2_3WAY */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"

#include "stob/stob.h"

/**
   @addtogroup stob
   @{
 */

static const struct c2_addb_ctx_type c2_stob_type_addb = {
	.act_name = "stob-type"
};

static const struct c2_addb_ctx_type c2_stob_domain_addb = {
	.act_name = "stob-domain"
};

static const struct c2_addb_ctx_type c2_stob_addb = {
	.act_name = "stob-domain"
};

C2_TL_DESCR_DEFINE(dom, "stob domains", static, struct c2_stob_domain,
		   sd_domain_linkage, sd_magic, 0xABD1CAB1EAB5CE55,
		   0xACCE551B1EEFFACE);
C2_TL_DEFINE(dom, static, struct c2_stob_domain);

int c2_stob_type_init(struct c2_stob_type *kind)
{
	dom_tlist_init(&kind->st_domains);
	c2_addb_ctx_init(&kind->st_addb, &c2_stob_type_addb,
			 &c2_addb_global_ctx);
	return 0;
}

void c2_stob_type_fini(struct c2_stob_type *kind)
{
	c2_addb_ctx_fini(&kind->st_addb);
	dom_tlist_fini(&kind->st_domains);
}

int c2_stob_domain_locate(struct c2_stob_type *type, const char *domain_name,
			  struct c2_stob_domain **dom)
{
	return C2_STOB_TYPE_OP(type, sto_domain_locate, domain_name, dom);
}

void c2_stob_domain_init(struct c2_stob_domain *dom, struct c2_stob_type *t)
{
	c2_rwlock_init(&dom->sd_guard);
	dom->sd_type = t;
	dom_tlink_init_at_tail(dom, &t->st_domains);
	c2_addb_ctx_init(&dom->sd_addb, &c2_stob_domain_addb, &t->st_addb);
}

void c2_stob_domain_fini(struct c2_stob_domain *dom)
{
	c2_addb_ctx_fini(&dom->sd_addb);
	c2_rwlock_fini(&dom->sd_guard);
	dom_tlink_del_fini(dom);
	dom->sd_magic = 0;
}

int c2_stob_find(struct c2_stob_domain *dom, const struct c2_stob_id *id,
		 struct c2_stob **out)
{
	return dom->sd_ops->sdo_stob_find(dom, id, out);
}

void c2_stob_init(struct c2_stob *obj, const struct c2_stob_id *id,
		  struct c2_stob_domain *dom)
{
	c2_atomic64_set(&obj->so_ref, 1);
	obj->so_state = CSS_UNKNOWN;
	obj->so_id = *id;
	obj->so_domain = dom;
	c2_addb_ctx_init(&obj->so_addb, &c2_stob_addb, &dom->sd_addb);
}

void c2_stob_fini(struct c2_stob *obj)
{
	c2_addb_ctx_fini(&obj->so_addb);
}

C2_BASSERT(sizeof(struct c2_uint128) == sizeof(struct c2_stob_id));

int c2_stob_locate(struct c2_stob *obj, struct c2_dtx *tx)
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
		C2_IMPOSSIBLE("invalid object state");
	}
	C2_POST(ergo(result == 0, obj->so_state == CSS_EXISTS));
	C2_POST(ergo(result == -ENOENT, obj->so_state == CSS_NOENT));
	return result;
}

int c2_stob_create(struct c2_stob *obj, struct c2_dtx *tx)
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
		C2_IMPOSSIBLE("invalid object state");
	}
	C2_POST(ergo(result == 0, obj->so_state == CSS_EXISTS));
	return result;
}

void c2_stob_get(struct c2_stob *obj)
{
	c2_atomic64_inc(&obj->so_ref);
}

void c2_stob_put(struct c2_stob *obj)
{
	struct c2_stob_domain *dom;

	dom = obj->so_domain;
	c2_rwlock_write_lock(&dom->sd_guard);
	if (c2_atomic64_dec_and_test(&obj->so_ref))
		obj->so_op->sop_fini(obj);
	c2_rwlock_write_unlock(&dom->sd_guard);
}

static void c2_stob_io_private_fini(struct c2_stob_io *io)
{
	if (io->si_stob_private != NULL) {
		io->si_op->sio_fini(io);
		io->si_stob_private = NULL;
	}
}

static void c2_stob_io_lock(struct c2_stob *obj)
{
	obj->so_op->sop_io_lock(obj);
}

static void c2_stob_io_unlock(struct c2_stob *obj)
{
	obj->so_op->sop_io_unlock(obj);
}

void c2_stob_io_init(struct c2_stob_io *io)
{
	C2_SET0(io);

	io->si_opcode = SIO_INVALID;
	io->si_state  = SIS_IDLE;
	c2_chan_init(&io->si_wait);

	C2_POST(io->si_state == SIS_IDLE);
}

void c2_stob_io_fini(struct c2_stob_io *io)
{
	C2_PRE(io->si_state == SIS_IDLE);

	c2_chan_fini(&io->si_wait);
	c2_stob_io_private_fini(io);
}

int c2_stob_io_launch(struct c2_stob_io *io, struct c2_stob *obj,
		      struct c2_dtx *tx, struct c2_io_scope *scope)
{
	int result;

	C2_PRE(obj->so_state == CSS_EXISTS);
	C2_PRE(c2_chan_has_waiters(&io->si_wait));
	C2_PRE(io->si_obj == NULL);
	C2_PRE(io->si_state == SIS_IDLE);
	C2_PRE(io->si_opcode != SIO_INVALID);
	C2_PRE(c2_vec_count(&io->si_user.ov_vec) ==
	       c2_vec_count(&io->si_stob.iv_vec));
	C2_PRE(c2_stob_io_user_is_valid(&io->si_user));
	C2_PRE(c2_stob_io_stob_is_valid(&io->si_stob));

	if (io->si_stob_magic != obj->so_domain->sd_type->st_magic) {
		c2_stob_io_private_fini(io);
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
		c2_stob_io_lock(obj);
		result = io->si_op->sio_launch(io);
		if (result != 0) {
			io->si_state = SIS_IDLE;
			c2_stob_io_unlock(obj);
		}
	}
	C2_POST(ergo(result != 0, io->si_state == SIS_IDLE));
	return result;
}

bool c2_stob_io_user_is_valid(const struct c2_bufvec *user)
{
	return true;
}

bool c2_stob_io_stob_is_valid(const struct c2_indexvec *stob)
{
	uint32_t    i;
	c2_bindex_t reached;

	for (reached = 0, i = 0; i < stob->iv_vec.v_nr; ++i) {
		if (stob->iv_index[i] < reached)
			return false;
		reached = stob->iv_index[i] + stob->iv_vec.v_count[i];
	}
	return true;
}

void *c2_stob_addr_pack(const void *buf, uint32_t shift)
{
	uint64_t addr = (uint64_t)buf;

	C2_PRE(((addr >> shift) << shift) == addr);
	return (void *)(addr >> shift);
}

void *c2_stob_addr_open(const void *buf, uint32_t shift)
{
	uint64_t addr = (uint64_t)buf;

	C2_PRE(((addr << shift) >> shift) == addr);
	return (void *)(addr << shift);
}

int c2_stob_create_helper(struct c2_stob_domain    *dom,
			  struct c2_dtx            *dtx,
			  const struct c2_stob_id  *stob_id,
			  struct c2_stob          **out)
{
	struct c2_stob *stob;
	int             rc;

	rc = c2_stob_find(dom, stob_id, &stob);
	if (rc == 0) {
		/*
		 * Here, stob != NULL and c2_stob_find() has taken reference on
		 * stob. On error must call c2_stob_put() on stob, after this
		 * point.
		 */
		if (stob->so_state == CSS_UNKNOWN)
			rc = c2_stob_locate(stob, dtx);
		if (stob->so_state == CSS_NOENT)
			rc = c2_stob_create(stob, dtx);

		*out = stob->so_state == CSS_EXISTS ? stob : NULL;
		if (rc != 0)
			c2_stob_put(stob);
	}
	return rc;
}

void c2_stob_iovec_sort(struct c2_stob_io *stob)
{
	struct c2_indexvec *ivec = &stob->si_stob;
	struct c2_bufvec   *bvec = &stob->si_user;
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

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
