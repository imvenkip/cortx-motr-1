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
#  include <config.h>
#endif

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/cdefs.h"
#include "lib/arith.h"   /* C2_3WAY */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"

#include "stob.h"

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

int c2_stob_type_init(struct c2_stob_type *kind)
{
	c2_list_init(&kind->st_domains);
	c2_addb_ctx_init(&kind->st_addb, &c2_stob_type_addb, 
			 &c2_addb_global_ctx);
	return 0;
}

void c2_stob_type_fini(struct c2_stob_type *kind)
{
	c2_addb_ctx_fini(&kind->st_addb);
	c2_list_fini(&kind->st_domains);
}

void c2_stob_domain_init(struct c2_stob_domain *dom, struct c2_stob_type *t)
{
	c2_rwlock_init(&dom->sd_guard);
	dom->sd_type = t;
	c2_list_add_tail(&t->st_domains, &dom->sd_domain_linkage);
	c2_addb_ctx_init(&dom->sd_addb, &c2_stob_domain_addb, &t->st_addb);
}

void c2_stob_domain_fini(struct c2_stob_domain *dom)
{
	c2_addb_ctx_fini(&dom->sd_addb);
	c2_rwlock_fini(&dom->sd_guard);
	c2_list_del(&dom->sd_domain_linkage);
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

bool c2_stob_id_eq(const struct c2_stob_id *id0, const struct c2_stob_id *id1)
{
	return c2_uint128_eq(&id0->si_bits, &id1->si_bits);
}

int c2_stob_id_cmp(const struct c2_stob_id *id0, const struct c2_stob_id *id1)
{
	return c2_uint128_cmp(&id0->si_bits, &id1->si_bits);
}

bool c2_stob_id_is_set(const struct c2_stob_id *id)
{
	static const struct c2_stob_id zero = {
		.si_bits = {
			.u_hi = 0,
			.u_lo = 0
		}
	};
	return !c2_stob_id_eq(id, &zero);
}

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
	c2_sm_init(&io->si_mach);

	C2_POST(io->si_state == SIS_IDLE);
}

void c2_stob_io_fini(struct c2_stob_io *io)
{
	C2_PRE(io->si_state == SIS_IDLE);

	c2_sm_fini(&io->si_mach);
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
	C2_PRE(c2_vec_count(&io->si_user.div_vec.ov_vec) == 
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

bool c2_stob_io_user_is_valid(const struct c2_diovec *user)
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
