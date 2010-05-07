#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <libaio.h>

#include "lib/memory.h"
#include "lib/assert.h"
#include "linux.h"

/**
   @addtogroup stoblinux

   XXX configure check for libaio.

   @{
 */

struct linux_stob {
	struct c2_stob sl_stob;
	int            sl_fd;
	const char    *sl_path;
	io_context_t   sl_ctx;
	struct c2_list sl_aio;
};

struct linux_stob_io {
};

struct linux_aio {
	struct iocb         la_iocb;
	struct c2_list_link la_linkage;
};

#if 0
static struct linux_aio *linux_aio_get(bool must_succeed)
{
	struct linux_aio *aio;

	do {
		C2_ALLOC_PTR(aio);
	} while (must_succeed && aio == NULL);
	return aio;
}

static void linux_aio_put(struct linux_aio *aio)
{
	c2_free(aio);
}
#endif

static int linux_stob_io_launch(struct c2_stob_io *io)
{
	return 0;
}

static void linux_stob_io_cancel(struct c2_stob_io *io)
{
}

static const struct c2_stob_io_op linux_stob_io_op = {
	.sio_launch  = linux_stob_io_launch,
	.sio_cancel  = linux_stob_io_cancel
};

/**
  Linux stob init

  Init the Linux storage objects environment.
  Plain Linux file is used to represent an object for Linux type.
  Here we will use db4 to store the mapping from id to internal object
  representative. So, db4 connection and tales are initialized here.
*/
static int linux_stob_init(struct c2_stob *stob)
{
	/* connect to db4 here*/
	return 0;
}

/**
  Linux stob fini

  Cleanup the environment. Here we cleanup the db4 connections.
*/
static void linux_stob_fini(struct c2_stob *stob)
{
	/* disconnect to db4 here*/
}

/**
  Create an object

  Create an object, establish the mapping from id to it in the db.
*/
static int linux_stob_create(struct c2_stob_id *id,
                             struct c2_stob_object **out)
{
	return 0;
}

/**
  Lookup an object with specified id

  Lookup an object with specified id in the mapping db.
*/
static int linux_stob_locate(struct c2_stob_id *id,
                             struct c2_stob_object **out)
{
	return 0;
}

static int linux_stob_io_init(struct c2_stob *stob, struct c2_stob_io *io)
{
	C2_PRE(io->si_state == SIS_IDLE);

	io->si_op = &linux_stob_io_op;
	return 0;
}

static const struct c2_stob_op linux_stob_op = {
	.sop_init    = linux_stob_init,
	.sop_fini    = linux_stob_fini,
	.sop_create  = linux_stob_create,
	.sop_locate  = linux_stob_locate,
	.sop_io_init = linux_stob_io_init
};

static int linux_stob_type_init(struct c2_stob *stob)
{
	stob->so_op = &linux_stob_op;
	return 0;
}

static const struct c2_stob_type_op linux_stob_type_op = {
	.sto_init = linux_stob_type_init
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
