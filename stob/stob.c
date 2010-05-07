#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>  /* memset */

#include "lib/assert.h"
#include "lib/memory.h"

#include "stob.h"

/**
   @addtogroup stob
   @{
 */

static void c2_stob_io_private_fini(struct c2_stob_io *io)
{
	if (io->si_stob_private != NULL) {
		c2_free(io->si_stob_private);
		io->si_stob_private = NULL;
	}
}

static bool c2_stob_io_is_locked(const struct c2_stob *obj)
{
	return obj->so_op->sop_io_is_locked(obj);
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
	memset(io, 0, sizeof *io);

	io->si_opcode = SIO_INVALID;
	io->si_state  = SIS_IDLE;
	c2_clink_init(&io->si_wait, NULL);
	c2_sm_init(&io->si_mach);

	C2_POST(io->si_state == SIS_IDLE);
}

void c2_stob_io_fini(struct c2_stob_io *io)
{
	C2_PRE(io->si_state == SIS_IDLE);
	c2_sm_fini(&io->si_mach);
	c2_clink_fini(&io->si_wait);
	c2_stob_io_private_fini(io);
}

int c2_stob_io_launch(struct c2_stob_io *io, struct c2_stob *obj, 
		      struct c2_dtx *tx, struct c2_io_scope *scope)
{
	int result;

	C2_PRE(!c2_clink_is_armed(&io->si_wait));
	C2_PRE(io->si_obj == NULL);
	C2_PRE(io->si_state == SIS_IDLE);
	C2_PRE(io->si_opcode != SIO_INVALID);
	C2_PRE(c2_vec_count(&io->si_user.div_vec.ov_vec) == 
	       c2_vec_count(&io->si_stob.ov_vec));

	if (io->si_stob_magic != obj->so_type->st_magic) {
		c2_stob_io_private_fini(io);
		result = obj->so_op->sop_io_init(obj, io);
	} else
		result = 0;

	if (result == 0) {
		io->si_obj   = obj;
		io->si_tx    = tx;
		io->si_scope = scope;
		io->si_state = SIS_BUSY;
		c2_stob_io_lock(obj);
		/* XXX do something about barriers here. */
		result = io->si_op->sio_launch(io);
		C2_ASSERT(equi(result == 0, !c2_stob_io_is_locked(obj)));
		if (result != 0) {
			io->si_state = SIS_IDLE;
			c2_stob_io_unlock(obj);
		}
	}
	C2_POST(ergo(result != 0, io->si_state == SIS_IDLE));
	C2_POST(ergo(result != 0, !c2_clink_is_armed(&io->si_wait)));
	return result;
}

void c2_stob_io_cancel(struct c2_stob_io *io)
{
	c2_stob_io_lock(io->si_obj);
	if (io->si_state == SIS_BUSY)
		io->si_op->sio_cancel(io);
	c2_stob_io_unlock(io->si_obj);
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
