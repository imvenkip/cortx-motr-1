#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "linux.h"

/**
   @addtogroup stoblinux
   @{
 */

void linux_stob_io_fini(struct c2_stob_io *io)
{
}

int  linux_stob_io_launch(struct c2_stob_io *io, struct c2_dtx *tx,
			  struct c2_io_scope *scope)
{
}

void linux_stob_io_cancel(struct c2_stob_io *io)
{
}

static const struct c2_stob_io_op linux_stob_io_op = {
	.sio_fini   = linux_stob_io_fini,
	.sio_lanuch = linux_stob_io_launch,
	.sio_cancel = linux_stob_io_cancel
};

void linux_stob_fini(struct c2_stob *stob)
{
}

int linux_stob_io_init(struct c2_stob *stob, struct c2_stob_io *io)
{
	io->si_op = &linux_stob_io_op;
}

static const struct c2_stob_op linux_stob_op = {
	.sop_fini    = linux_stob_fini,
	.sop_io_init = linux_stob_io_init
};

static int linux_stob_init(struct c2_stob *stob)
{
	stob->so_op = &linux_stob_op;
}

static const struct c2_stob_type_op linux_stob_type_op = {
	.sto_init = linux_stob_init
};

static const struct c2_stob_type linux_stob = {
	.st_op    = &linux_stob_type_op,
	.st_name  = "linuxstob",
	.st_magic = 0xACC01ADE
};

/** @} end group stoblinux */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
