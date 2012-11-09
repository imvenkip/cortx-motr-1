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
 * Original creation date: 06-Nov-2012
 */

#include "lib/types.h"              /* uint32_t */
#include "lib/misc.h"               /* C2_IN */
#include "lib/tlist.h"
#include "lib/thread.h"

#include "sm/sm.h"
#include "stob/stob.h"
#include "stob/mirror.h"

/**
   @addtogroup stobmirror
   @{
 */

static const struct c2_stob_type_op   mirror_stob_type_op;
static const struct c2_stob_op        mirror_stob_op;
static const struct c2_stob_domain_op mirror_stob_domain_op;
static const struct c2_stob_io_op     mirror_stob_io_op;

enum target_state {
	ONLINE,
	FAILED,
	REPAIR
};

enum {
	MAX_NR_TARGET = 16
};

struct target {
	enum target_state      t_state;
	struct c2_stob_domain *t_dom;
};

struct mirror_domain {
	struct c2_stob_domain mr_base;
	struct c2_tl          mr_object;
	uint32_t              mr_nr;
	struct target         mr_target[MAX_NR_TARGET];
	struct c2_sm_group    mr_group;
	struct c2_thread      mr_ast_thread;
	bool                  mr_shutdown;
};

/**
 * Mirror storage object.
 */
struct mirror_stob {
	uint64_t         ms_magix;
	struct c2_stob   ms_stob;
	struct c2_tlink  ms_linkage;
	struct c2_stob  *ms_obj;
};

C2_TL_DESCR_DEFINE(mirror, "mirror stobs", static, struct mirror_stob,
		   ms_linkage, ms_magix,
		   0x3377,
		   0x3377);

C2_TL_DEFINE(mirror, static, struct mirror_stob);

static inline struct mirror_stob *stob2mirror(struct c2_stob *stob)
{
	return container_of(stob, struct mirror_stob, ms_stob);
}

static inline struct mirror_domain *domain2mirror(struct c2_stob_domain *dom)
{
	return container_of(dom, struct mirror_domain, mr_base);
}

/**
 * Implementation of c2_stob_type_op::sto_init().
 */
static int mirror_stob_type_init(struct c2_stob_type *stype)
{
	c2_stob_type_init(stype);
	return 0;
}

/**
 * Implementation of c2_stob_type_op::sto_fini().
 */
static void mirror_stob_type_fini(struct c2_stob_type *stype)
{
	c2_stob_type_fini(stype);
}

/**
 * Implementation of c2_stob_domain_op::sdo_fini().
 *
 * Finalizes all still existing in-memory objects.
 */
static void mirror_domain_fini(struct c2_stob_domain *self)
{
	struct mirror_domain *mdom;
	uint32_t              i;

	mdom = domain2mirror(self);
	mdom->mr_shutdown = true;
	c2_thread_join(&mdom->mr_ast_thread);
	c2_thread_fini(&mdom->mr_ast_thread);
	c2_sm_group_fini(&mdom->mr_group);
	for (i = 0; i < mdom->mr_nr; ++i) {
		if (mdom->mr_target[i].t_dom != NULL)
			c2_stob_domain_fini(mdom->mr_target[i].t_dom);
	}
	mirror_tlist_fini(&mdom->mr_object);
	c2_stob_domain_fini(self);
	c2_free(mdom);
}

static void ast_thread(struct mirror_domain *mdom)
{
	struct c2_sm_group *g = &mdom->mr_group;

	while (!mdom->mr_shutdown) {
		c2_chan_wait(&g->s_clink);
		c2_sm_group_lock(g);
		c2_sm_asts_run(g);
		c2_sm_group_unlock(g);
	}
}

/**
 * Implementation of c2_stob_type_op::sto_domain_locate().
 *
 * @note the domain returned is not immediately ready for
 * use. c2_mirror_stob_setup() has to be called against it first.
 */
static int mirror_stob_type_domain_locate(struct c2_stob_type *type,
					  const char *domain_name,
					  struct c2_stob_domain **out)
{
	struct mirror_domain  *mdom;
	struct c2_stob_domain *dom;
	int                    result;

	C2_ALLOC_PTR(mdom);
	if (mdom != NULL) {
		c2_sm_group_init(&mdom->mr_group);
		result = C2_THREAD_INIT(&mdom->mr_ast_thread,
					struct mirror_domain *, NULL,
					ast_thread, mdom,
					"ast(%s)", domain_name);
		if (result == 0) {
			mirror_tlist_init(&mdom->mirror_object);
			*out = dom = &adom->mirror_base;
			dom->sd_ops = &mirror_stob_domain_op;
			c2_stob_domain_init(dom, type);
		} else {
			c2_sm_group_fini(&mdom->mr_group);
			c2_free(mdom);
		}
	} else {
		result = -ENOMEM;
	}
	return result;
}

int c2_mirror_stob_setup(struct c2_stob_domain *dom, uint32_t nr,
			 struct c2_stob_domain **targets);
{
	struct mirror_domain *mdom;
	uint32_t              i;

	mdom = domain2mirror(dom);

	C2_PRE(nr <= MAX_NR_TARGET);
	C2_PRE(dom->sd_ops == &mirror_stob_domain_op);
	C2_PRE(mdom->mr_nr == 0);

	mdom->mr_nr = nr;
	for (i = 0; i < mdom->mr_nr; ++i) {
		mdom->mr_target[i].t_state = ONLINE;
		mdom->mr_target[i].t_dom   = targets[i];
	}
	return 0;
}

void c2_mirror_failed(struct c2_stob_domain *dom, uint64_t mask)
{
	struct mirror_domain *mdom = domain2mirror(dom);
	uint32_t              i;

	C2_PRE((mask & ~((1ULL << mdom->mr_nr) - 1ULL)) == 0);

	for (i = 0; i < mdom->mr_nr; ++i, mask >>= 1ULL) {
		if (mask & 1)
			mdom->mr_target[i].t_state = FAILED;
	}
}

void c2_mirror_repair(struct c2_stob_domain *dom, uint64_t mask)
{
}

/**
 * Searches for the object with a given identifier in the domain object list.
 *
 * This function is used by mirror_domain_stob_find() to check whether in-memory
 * representation of an object already exists.
 */
static struct mirror_stob *mirror_domain_lookup(struct mirror_domain *mdom,
						const struct c2_stob_id *id)
{
	struct mirror_stob *obj;

	C2_PRE(mdom->mr_nr > 0);

	c2_tl_for(mirror, &mdom->mr_object, obj) {
		if (c2_stob_id_eq(id, &obj->ms_stob.so_id)) {
			c2_stob_get(&obj->as_stob);
			break;
		}
	} c2_tl_endfor;
	return obj;
}

/**
 * Implementation of c2_stob_op::sop_fini().
 */
static void mirror_stob_fini(struct c2_stob *stob)
{
	struct mirror_stob *mstob;
	uint32_t            i;

	mstob = stob2mirror(stob);
	for (i = 0; i < mdom->mr_nr; ++i) {
		if (mstob->ms_obj[i] != NULL)
			c2_stob_put(mstob->ms_obj[i]);
	}
	mirror_tlink_del_fini(mstob);
	c2_stob_fini(&mdom->mr_base);
	c2_free(mstob);
}

static int mirror_stob_init(struct mirror_domain *mdom,
			    const struct c2_stob_id *id,
			    struct mirror_stob *mstob)
{
	struct c2_stob *stob;
	uint32_t        i;
	int             result;

	stob = &mstob->ms_stob;
	stob->so_op = &mirror_stob_op;
	c2_stob_init(stob, id, &mdom->mr_base);
	for (i = 0; i < mdom->mr_nr; ++i) {
		result = c2_stob_find(mdom->mr_target[i], id, &mstob->ms_obj[i]);
		if (result != 0) {
			mirror_stob_fini(stob);
			break;
		}
	}
	return result;
}

/**
 * Implementation of c2_stob_domain_op::sdo_stob_find().
 *
 * Returns an in-memory representation of the object with a given identifier.
 */
static int mirror_domain_stob_find(struct c2_stob_domain *dom,
				   const struct c2_stob_id *id,
				   struct c2_stob **out)
{
	struct mirror_domain *mdom;
	struct mirror_stob   *mstob;
	struct mirror_stob   *ghost;
	int                   result;

	mdom = domain2mirror(dom);

	C2_PRE(mdom->mr_nr > 0);

	result = 0;
	c2_rwlock_read_lock(&dom->sd_guard);
	mstob = mirror_domain_lookup(adom, id);
	c2_rwlock_read_unlock(&dom->sd_guard);

	if (mstob == NULL) {
		mstob = c2_alloc(sizeof *mstob +
				 mdom->mr_nr * sizeof mstob->ms_obj[0]);
		if (mstob == NULL)
			return -ENOMEM;
		result = mirror_stob_init(mdom, id, mstob);
		if (result == 0) {
			c2_rwlock_write_lock(&dom->sd_guard);
			ghost = mirror_domain_lookup(mdom, id);
			if (ghost == NULL) {
				mirror_tlink_init_at(mstob, &mdom->mr_object);
			} else {
				mirror_stob_fini(&mstob->ms_stob);
				mstob = ghost;
				c2_stob_get(&mstob->ms_stob);
			}
			c2_rwlock_write_unlock(&dom->sd_guard);
		}
	}
	*out = &mstob->ms_stob;
	return result;
}

/**
 * Implementation of c2_stob_domain_op::sdo_tx_make().
 */
static int mirror_domain_tx_make(struct c2_stob_domain *dom, struct c2_dtx *tx)
{
	struct mirror_domain  *mdom;
	struct c2_stob_domain *child;
	uint32_t               i;

	mdom = domain2mirror(dom);
	C2_PRE(mdom->mr_nr > 0);

	child = mdom->mr_target[0].t_dom;
	return child->sd_ops->sdo_tx_make(child, tx);
}

/**
 * Implementation of c2_stob_op::sop_create().
 */
static int mirror_stob_create(struct c2_stob *obj, struct c2_dtx *tx)
{
	struct mirror_domain *mdom;
	struct mirror_stob   *mobj;
	uint32_t              i;
	int                   result;

	mdom = domain2mirror(obj->so_domain);
	C2_PRE(mdom->mr_nr > 0);

	if (!c2_forall(j, mdom->mr_nr, mdom->mr_target[j].t_state != REPAIR))
		return -EIO;
	mobj = stob2mirror(obj);
	for (i = 0; i < mdom->mr_nr; ++i) {
		result = c2_stob_create(mobj->ms_stob[i], tx);
		if (result != 0)
			/** @todo delete sub-objects on failure. */
			break;
	}
	return result;
}

/**
 * Implementation of c2_stob_op::sop_locate().
 */
static int mirror_stob_locate(struct c2_stob *obj, struct c2_dtx *tx)
{
	struct mirror_domain *mdom;
	struct mirror_stob   *mobj;
	uint32_t              i;
	int                   result;

	mdom = domain2mirror(obj->so_domain);
	C2_PRE(mdom->mr_nr > 0);
	mobj = stob2mirror(obj);

	for (i = 0, i < mdom->mr_nr; ++i) {
		result = c2_stob_locate(mobj->ms_obj[i], tx);
		if (result != 0)
			break;
	}
	return result;
}

/** @} end group stobmirror */

/*****************************************************************************/
/*                                    IO code                                */
/*****************************************************************************/

/**
   @addtogroup stobmirror

   <b>Mirror IO implementation.</b>

   @{
 */

struct mirror_stob_io;

struct io_target {
	struct mirror_stob_io *t_io;
	/** Target IO request */
	struct c2_stob_io      t_req;
	/** Clink registered with target completion channel to intercept target
	    IO completion notification. */
	struct c2_clink        t_clink;
};

/**
   Mirror private IO state.
 */
struct mirror_stob_io {
	/** parent IO request */
	struct c2_mutex    mi_lock;
	struct c2_stob_io *mi_io;
	uint32_t           mi_start;
	uint32_t           mi_todo;
	uint32_t           mi_total;
	uint32_t           mi_success;
	struct c2_sm_ast   mi_ast;
	struct io_target   mi_target[0];
};

static bool mirror_endio(struct c2_clink *link);

/**
 * Implementation of c2_stob_op::sop_io_init().
 */
int mirror_stob_io_init(struct c2_stob *stob, struct c2_stob_io *io)
{
	struct mirror_stob_io *mio;
	struct mirror_domain  *mdom;
	int                    result;
	uint32_t               i;

	mdom = domain2mirror(stob->so_domain);
	C2_PRE(mdom->mr_nr > 0);

	C2_PRE(io->si_state == SIS_IDLE);

	mio = c2_alloc(sizeof *mio + mdom->mr_nr * sizeof mio->mi_target[0]);
	if (mio != NULL) {
		io->si_stob_private = mio;
		io->si_op = &mirror_stob_io_op;
		mio->mi_io = io;
		for (i = 0; i < mdom->mr_nr; ++i) {
			struct io_target *t = &mio->mi_target[i];

			t->t_io = mio;
			c2_stob_io_init(t->t_req);
			c2_clink_init(&t->t_clink, &mirror_endio);
			c2_clink_add(&t->t_req.si_wait, &t->t_clink);
		}
		c2_mutex_init(&mio->mi_lock);
		result = 0;
	} else {
		result = -ENOMEM;
	}
	return result;
}

/**
 * Implementation of c2_stob_io_op::sio_fini().
 */
static void mirror_stob_io_fini(struct c2_stob_io *io)
{
	struct mirror_stob_io *mio = io->si_stob_private;
	struct mirror_domain  *mdom;
	uint32_t               i;

	mdom = domain2mirror(stob->so_domain);
	c2_mutex_fini(&mio->mi_lock);
	for (i = 0; i < mdom->mr_nr; ++i) {
		struct io_target *t = &mio->mi_target[i];

		c2_clink_del(&t->t_clink);
		c2_clink_fini(&t->t_clink);
		c2_stob_io_fini(t->t_req);
	}
	c2_free(mio);
}

static int subio_build(struct mirror_stob_io *mio,
		       uint32_t start, uint32_t end)
{
	struct c2_stob_io     *io   = mio->mi_io;
	struct c2_stob        *obj  = io->si_obj;
	struct mirror_domain  *mdom = domain2mirror(obj->so_domain);
	struct mirror_stob    *mobj = stob2mirror(obj);
	uint32_t               nr   = mdom->mr_nr;
	uint32_t               i;

	C2_PRE(nr > 0);
	C2_PRE(end <= nr);
	C2_PRE(obj->so_domain->sd_type == &c2_mirror_stob_type);
	C2_PRE(io->si_stob.iv_vec.v_nr > 0);
	C2_PRE(c2_vec_count(&io->si_user.ov_vec) > 0);
	C2_PRE(c2_mutex_is_locked(&mio->mi_lock));
	C2_PRE(mio->mi_todo == 0);
	C2_ASSERT(C2_IN(io->si_opcode, (SIO_READ, SIO_WRITE)));

	c2_mutex_lock(&mio->mi_lock);
	for (i = start % nr; i != end; i = (i + 1) % nr) {
		struct c2_stob_io *tio = &mio->mi_target[i].t_req;

		if (mdom->mr_target[i].t_state != ONLINE)
			continue;
		tio->si_opcode = io->si_opcode;
		tio->si_stob   = io->si_stob;
		tio->si_user   = io->si_user;
		tio->si_flags  = io->si_flags;
		result = c2_stob_io_launch(tio, mobj->ms_obj[i], io->si_tx,
					   io->si_scope);
		if (result != 0)
			break;
		mio->mi_todo++;
		if (io->si_opcode == SIO_READ)
			break;
	}
	mio->mi_total = mio->mi_todo;
	io->si_rc = io->si_rc ?: result;
	if (mio->mi_todo > 0 && result != 0)
		result = 0;
	c2_mutex_unlock(&mio->mi_lock);
	return result;
}

/**
 * Launch asynchronous IO.
 */
static int mirror_stob_io_launch(struct c2_stob_io *io)
{
	struct mirror_domain  *mdom = domain2mirror(io->si_obj->so_domain);
	struct mirror_stob    *mobj = stob2mirror(io->si_obj);
	struct mirror_stob_io *mio  = io->si_stob_private;
	int                    result;
	uint32_t               i;

	C2_PRE(mdom->mr_nr > 0);
	C2_PRE(io->si_obj->so_domain->sd_type == &c2_mirror_stob_type);
	C2_PRE(io->si_stob.iv_vec.v_nr > 0);
	C2_PRE(c2_vec_count(&io->si_user.ov_vec) > 0);
	C2_ASSERT(C2_IN(io->si_opcode, (SIO_READ, SIO_WRITE)));

	mio->mi_start = 0;
	return subio_build(mio, mio->mi_start, mdom->mr_nr);
}

/**
 * An implementation of c2_stob_op::sop_lock() method.
 */
static void mirror_stob_io_lock(struct c2_stob *stob)
{
}

/**
 * An implementation of c2_stob_op::sop_unlock() method.
 */
static void mirror_stob_io_unlock(struct c2_stob *stob)
{
}

/**
 * An implementation of c2_stob_op::sop_is_locked() method.
 */
static bool mirror_stob_io_is_locked(const struct c2_stob *stob)
{
	return true;
}

/**
 * An implementation of c2_stob_op::sop_block_shift() method.
 *
 * Mirror uses the same block size as its target objects.
 *
 * @todo check in the invariant that block sizes are the same.
 */
static uint32_t mirror_stob_block_shift(const struct c2_stob *stob)
{
	struct mirror_domain *mdom;
	struct mirror_stob   *mobj;
	uint32_t              i;

	mdom = domain2mirror(obj->so_domain);
	C2_PRE(mdom->mr_nr > 0);
	mobj = stob2mirror(obj);
	for (i = 0; i < mdom->mr_nr; ++i) {
		struct c2_stob *obj = mobj->ms_obj[i];
		if (obj != NULL)
			return obj->so_op->sop_block_shift(obj);
	}
	/*
	 * All targets are down, IO is not possible anyway, return something,
	 * because the signature of ->sop_block_shift() doesn't provide for
	 * error reporting.
	 */
	return 4*1024*1024;
}

/**
 * An implementation of c2_stob_domain_op::sdo_block_shift() method.
 */
static uint32_t mirror_stob_domain_block_shift(struct c2_stob_domain *sd)
{
	C2_IMPOSSIBLE("Obsolete method.");
}

static void io_done(struct c2_stob_io *io)
{
	io->si_state = SIS_IDLE;
	c2_chan_broadcast(&io->si_wait);
}

static void read_try_next(struct c2_sm_group *grp, struct c2_sm_ast *ast)
{
	struct mirror_stob_io *mio;
	struct c2_stob_io     *io;
	uint32_t               idx = (uint32_t)ast->sa_datum;

	mio = container_of(ast, struct mirror_stob_io, mi_ast);
	io  = mio->mi_io;
	C2_PRE(io->si_opcode == SIO_READ);
	if (subio_build(mio, idx + 1, mio->mi_start) != 0)
		io_done(io);
}

static bool mirror_endio(struct c2_clink *link)
{
	struct mirror_stob_io *mio;
	struct io_target      *iot;
	struct c2_stob_io     *io;
	struct c2_stob_io     *subio;
	int32_t                rc;
	uint32_t               need;
	bool                   done;
	bool                   read;

	iot   = container_of(link, struct io_target, t_clink);
	subio = iot->t_req;
	mio   = iot->t_io;
	io    = mio->mi_io;
	read  = io->si_opcode == SIO_READ;

	C2_ASSERT(io->si_state == SIS_BUSY);
	C2_ASSERT(subio->si_state == SIS_IDLE);
	C2_ASSERT(mio->mi_done > 0);

	c2_mutex_lock(&mio->mi_lock);
	rc = subio->si_rc;
	if (rc == 0) {
		if (mio->mi_success == 0 || io->si_count == subio->si_count)
			mio->mi_success++;
		else
			rc = -EIO;
	}
	io->si_rc    = rc ?: io->si_rc;
	io->si_count = subio->si_count;

	done = --mio->mi_todo == 0;
	if (done) {
		need = read ? 1 : mio->mi_total;
		if (mio->mi_success >= need)
			io->si_rc = 0;
		else if (read) {
			struct mirror_domain *mdom;

			C2_ASSERT(io->si_rc != 0);
			mdom = domain2mirror(io->si_obj->so_domain);
			done = false;
			mio->mi_ast.sa_cb = &read_try_next;
			mio->mi_ast.sa_datum = (void *)(iot - mio->mi_target);
			c2_sm_ast_post(&mdom->mr_group, &mio->mi_ast);
		}
	}
	c2_mutex_unlock(&mio->mi_lock);
	if (done)
		io_done(io);
	return true;
}

static const struct c2_stob_io_op mirror_stob_io_op = {
	.sio_launch  = mirror_stob_io_launch,
	.sio_fini    = mirror_stob_io_fini
};

static const struct c2_stob_type_op mirror_stob_type_op = {
	.sto_init          = mirror_stob_type_init,
	.sto_fini          = mirror_stob_type_fini,
	.sto_domain_locate = mirror_stob_type_domain_locate
};

static const struct c2_stob_domain_op mirror_stob_domain_op = {
	.sdo_fini        = mirror_domain_fini,
	.sdo_stob_find   = mirror_domain_stob_find,
	.sdo_tx_make     = mirror_domain_tx_make,
	.sdo_block_shift = mirror_stob_domain_block_shift
};

static const struct c2_stob_op mirror_stob_op = {
	.sop_fini         = mirror_stob_fini,
	.sop_create       = mirror_stob_create,
	.sop_locate       = mirror_stob_locate,
	.sop_io_init      = mirror_stob_io_init,
	.sop_io_lock      = mirror_stob_io_lock,
	.sop_io_unlock    = mirror_stob_io_unlock,
	.sop_io_is_locked = mirror_stob_io_is_locked,
	.sop_block_shift  = mirror_stob_block_shift
};

struct c2_stob_type c2_mirror_stob_type = {
	.st_op    = &mirror_stob_type_op,
	.st_name  = "mirrorstob",
	.st_magic = 0x18B2A256
};

int c2_mirror_stobs_init(void)
{
	return C2_STOB_TYPE_OP(&c2_mirror_stob_type, sto_init);
}

void c2_mirror_stobs_fini(void)
{
	C2_STOB_TYPE_OP(&c2_mirror_stob_type, sto_fini);
}

/** @} end group stobad */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
