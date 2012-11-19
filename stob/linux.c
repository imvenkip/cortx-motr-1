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

#include <stdio.h>
#include <unistd.h>                    /* close */
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/queue.h"

#include "linux.h"
#include "linux_internal.h"

/**
   @addtogroup stoblinux

   <b>Implementation of c2_stob on top of Linux files.</b>

   A linux storage object is simply a file on a local file system. A linux
   storage object domain is a directory containing

   @li data-base (db5) tables mapping storage object identifiers to local
   identifiers (not currently used) and

   @li a directory where files, corresponding to storage objects are stored
   in. A name of a file is built from the corresponding storage object local
   identifier (currently c2_stob_id is used).

   A linux storage object domain is identified by the path to its directory.

   When an in-memory representation for an object is created, no file system
   operations are performed. It is only when the object is "located"
   (linux_stob_locate()) or "created" (linux_stob_create()) when actual open(2)
   system call is made. If the call was successful, the file descriptor
   (linux_stob::sl_fd) remains open until the object is destroyed.

   Storage objects are kept on a list linux_domain::sdl_object list that is
   consulted by linux_domain_lookup().

   @todo object caching

   @todo a per-domain limit on number of open file descriptors with LRU based
   cleanup.

   @todo more scalable object index instead of a list.

   @{
 */

struct c2_stob_type c2_linux_stob_type;
static const struct c2_stob_type_op linux_stob_type_op;
static const struct c2_stob_op linux_stob_op;
static const struct c2_stob_domain_op linux_stob_domain_op;

static void linux_stob_fini(struct c2_stob *stob);

static const struct c2_addb_loc c2_linux_stob_addb_loc = {
	.al_name = "linux-stob"
};

C2_TL_DESCR_DEFINE(ls, "linux stobs", static, struct linux_stob,
		   sl_linkage, sl_magix,
		   0xb1b11ca15cab105a /* biblical scabiosa */,
		   0x11fe1e55cab00d1e /* lifeless caboodle */);

C2_TL_DEFINE(ls, static, struct linux_stob);

/**
   Implementation of c2_stob_type_op::sto_init().
 */
static int linux_stob_type_init(struct c2_stob_type *stype)
{
	c2_stob_type_init(stype);
	return 0;
}

/**
   Implementation of c2_stob_type_op::sto_fini().
 */
static void linux_stob_type_fini(struct c2_stob_type *stype)
{
	c2_stob_type_fini(stype);
}

/**
   Implementation of c2_stob_domain_op::sdo_fini().

   Finalizes all still existing in-memory objects.
 */
static void linux_domain_fini(struct c2_stob_domain *self)
{
	struct linux_domain *ldom;
	struct linux_stob   *lstob;

	ldom = domain2linux(self);
	linux_domain_io_fini(self);
	c2_rwlock_write_lock(&self->sd_guard);
	c2_tl_for(ls, &ldom->sdl_object, lstob) {
		linux_stob_fini(&lstob->sl_stob);
	} c2_tl_endfor;
	c2_rwlock_write_unlock(&self->sd_guard);
	ls_tlist_fini(&ldom->sdl_object);
	c2_stob_domain_fini(self);
	c2_free(ldom);
}

/**
   Implementation of c2_stob_type_op::sto_domain_locate().

   Initialises adieu sub-system for the domain.

   @note the domain returned is ready for use, but c2_linux_stob_setup() can be
   called against it in order to customize some configuration options (currently
   there is only one such option: "use_directio" flag).
 */
static int linux_stob_type_domain_locate(struct c2_stob_type *type,
					 const char *domain_name,
					 struct c2_stob_domain **out)
{
	struct linux_domain   *ldom;
	struct c2_stob_domain *dom;
	int                    result;

	C2_ASSERT(domain_name != NULL);
	C2_ASSERT(strlen(domain_name) < ARRAY_SIZE(ldom->sdl_path));

	C2_ALLOC_PTR(ldom);
	if (ldom != NULL) {
		ls_tlist_init(&ldom->sdl_object);
		strcpy(ldom->sdl_path, domain_name);
		dom = &ldom->sdl_base;
		dom->sd_ops = &linux_stob_domain_op;
		c2_stob_domain_init(dom, type);
		result = linux_domain_io_init(dom);
		if (result == 0)
			*out = dom;
		else
			linux_domain_fini(dom);
		ldom->use_directio = false;
		dom->sd_name = ldom->sdl_path;
	} else {
		C2_ADDB_ADD(&type->st_addb,
			    &c2_linux_stob_addb_loc, c2_addb_oom);
		result = -ENOMEM;
	}
	return result;
}

C2_INTERNAL int c2_linux_stob_setup(struct c2_stob_domain *dom,
				    bool use_directio)
{
	struct linux_domain *ldom;

	ldom = domain2linux(dom);

	ldom->linux_setup = true;
	ldom->use_directio = use_directio;

	return 0;
}

static bool linux_stob_invariant(const struct linux_stob *lstob)
{
	const struct c2_stob *stob;

	stob = &lstob->sl_stob;
	return
		(lstob->sl_fd >= 0) == (stob->so_state == CSS_EXISTS) &&
		lstob->sl_stob.so_domain->sd_type == &c2_linux_stob_type;
}

/**
   Searches for the object with a given identifier in the domain object list.

   This function is used by linux_domain_stob_find() to check whether in-memory
   representation of an object already exists.
 */
static struct linux_stob *linux_domain_lookup(struct linux_domain *ldom,
					      const struct c2_stob_id *id)
{
	struct linux_stob *obj;

	c2_tl_for(ls, &ldom->sdl_object, obj) {
		C2_ASSERT(linux_stob_invariant(obj));
		if (c2_stob_id_eq(id, &obj->sl_stob.so_id)) {
			c2_stob_get(&obj->sl_stob);
			break;
		}
	} c2_tl_endfor;
	return obj;
}

/**
   Implementation of c2_stob_domain_op::sdo_stob_find().

   Returns an in-memory representation of the object with a given identifier.
 */
static int linux_domain_stob_find(struct c2_stob_domain *dom,
				  const struct c2_stob_id *id,
				  struct c2_stob **out)
{
	struct linux_domain *ldom;
	struct linux_stob   *lstob;
	struct linux_stob   *ghost;
	struct c2_stob      *stob;
	int                  result;

	ldom = domain2linux(dom);

	result = 0;
	c2_rwlock_read_lock(&dom->sd_guard);
	lstob = linux_domain_lookup(ldom, id);
	c2_rwlock_read_unlock(&dom->sd_guard);

	if (lstob == NULL) {
		C2_ALLOC_PTR(lstob);
		if (lstob != NULL) {
			c2_rwlock_write_lock(&dom->sd_guard);
			ghost = linux_domain_lookup(ldom, id);
			if (ghost == NULL) {
				stob = &lstob->sl_stob;
				stob->so_op = &linux_stob_op;
				lstob->sl_fd = -1;
				c2_stob_init(stob, id, dom);
				ls_tlink_init_at(lstob, &ldom->sdl_object);
			} else {
				c2_free(lstob);
				lstob = ghost;
				c2_stob_get(&lstob->sl_stob);
			}
			c2_rwlock_write_unlock(&dom->sd_guard);
		} else {
			C2_ADDB_ADD(&dom->sd_addb,
				    &c2_linux_stob_addb_loc, c2_addb_oom);
			result = -ENOMEM;
		}
	}
	if (result == 0) {
		*out = &lstob->sl_stob;
		C2_ASSERT(linux_stob_invariant(lstob));
	}
	return result;
}

/**
   Implementation of c2_stob_domain_op::sdo_tx_make().
 */
static int linux_domain_tx_make(struct c2_stob_domain *dom, struct c2_dtx *tx)
{
	return 0;
}

/**
   Helper function constructing a file system path to the object.
 */
static int linux_stob_path(const struct linux_stob *lstob, int nr, char *path)
{
	int                  nob;
	struct linux_domain *ldom;

	C2_ASSERT(linux_stob_invariant(lstob));

	ldom  = domain2linux(lstob->sl_stob.so_domain);
	nob = snprintf(path, nr, "%s/o/%016lx.%016lx", ldom->sdl_path,
		       lstob->sl_stob.so_id.si_bits.u_hi,
		       lstob->sl_stob.so_id.si_bits.u_lo);
	return nob < nr ? 0 : -EOVERFLOW;
}

/**
   Implementation of c2_stob_op::sop_fini().

   Closes the object's file descriptor.

   @see c2_linux_stob_link()
 */
static void linux_stob_fini(struct c2_stob *stob)
{
	struct linux_stob *lstob;

	lstob = stob2linux(stob);
	C2_ASSERT(linux_stob_invariant(lstob));
	/*
	 * No caching for now, dispose of the body^Wobject immediately.
	 */
	if (lstob->sl_fd != -1) {
		close(lstob->sl_fd);
		lstob->sl_fd = -1;
	}
	ls_tlink_del_fini(lstob);
	c2_stob_fini(&lstob->sl_stob);
	c2_free(lstob);
}

/**
   Helper function opening the object in the file system.
 */
static int linux_stob_open(struct linux_stob *lstob, int oflag)
{
	char                 pathname[MAXPATHLEN];
	int                  result;
	struct stat          statbuf;

	C2_ASSERT(linux_stob_invariant(lstob));
	C2_ASSERT(lstob->sl_fd == -1);

	result = linux_stob_path(lstob, ARRAY_SIZE(pathname), pathname);
	if (result == 0) {
		lstob->sl_fd = open(pathname, oflag, 0700);
		if (lstob->sl_fd == -1)
			result = -errno;
		else {
			result = fstat(lstob->sl_fd, &statbuf);
			if (result == 0)
				lstob->sl_mode = statbuf.st_mode;
			else
				result = -errno;
		}
	}
	return result;
}

/**
   Implementation of c2_stob_op::sop_create().
 */
static int linux_stob_create(struct c2_stob *obj, struct c2_dtx *tx)
{
	int oflags = O_RDWR|O_CREAT;
	struct linux_domain *ldom;

	ldom  = domain2linux(obj->so_domain);
	if (ldom->use_directio)
		oflags |= O_DIRECT;

	return linux_stob_open(stob2linux(obj), oflags);
}

/**
   Implementation of c2_stob_op::sop_locate().
 */
static int linux_stob_locate(struct c2_stob *obj, struct c2_dtx *tx)
{
	int oflags = O_RDWR;
	struct linux_domain *ldom;

	ldom  = domain2linux(obj->so_domain);
	if (ldom->use_directio)
		oflags |= O_DIRECT;

	return linux_stob_open(stob2linux(obj), oflags);
}

static const struct c2_stob_type_op linux_stob_type_op = {
	.sto_init          = linux_stob_type_init,
	.sto_fini          = linux_stob_type_fini,
	.sto_domain_locate = linux_stob_type_domain_locate
};

static const struct c2_stob_domain_op linux_stob_domain_op = {
	.sdo_fini        = linux_domain_fini,
	.sdo_stob_find   = linux_domain_stob_find,
	.sdo_tx_make     = linux_domain_tx_make,
	.sdo_block_shift = linux_stob_domain_block_shift
};

static const struct c2_stob_op linux_stob_op = {
	.sop_fini         = linux_stob_fini,
	.sop_create       = linux_stob_create,
	.sop_locate       = linux_stob_locate,
	.sop_io_init      = linux_stob_io_init,
	.sop_io_lock      = linux_stob_io_lock,
	.sop_io_unlock    = linux_stob_io_unlock,
	.sop_io_is_locked = linux_stob_io_is_locked,
	.sop_block_shift  = linux_stob_block_shift
};

struct c2_stob_type c2_linux_stob_type = {
	.st_op    = &linux_stob_type_op,
	.st_name  = "linuxstob",
	.st_magic = 0xACC01ADE
};

const struct c2_addb_ctx_type adieu_addb_ctx_type = {
	.act_name = "adieu"
};

struct c2_addb_ctx adieu_addb_ctx;

/**
   This function is called to link a path of an existing file to a stob id,
   for a given Linux stob. This path will be typically a block device path.

   @pre obj != NULL
   @pre path != NULL

   @param dom -> storage domain
   @param obj -> stob object embedded inside Linux stob object
   @param path -> Path to other file (typically block device)
   @param tx -> transaction context

   @see c2_linux_stob_open()
 */
C2_INTERNAL int c2_linux_stob_link(struct c2_stob_domain *dom,
				   struct c2_stob *obj, const char *path,
				   struct c2_dtx *tx)
{
	int                result;
	char               symlinkname[64];
	struct linux_stob *lstob;

	C2_PRE(obj != NULL);
	C2_PRE(path != NULL);

	lstob = stob2linux(obj);

	result = linux_stob_path(lstob, ARRAY_SIZE(symlinkname), symlinkname);
	if (result == 0) {
		result = symlink(path, symlinkname) < 0  ? -errno : 0;
	}

	return result;
}

C2_INTERNAL int c2_linux_stobs_init(void)
{
	c2_addb_ctx_init(&adieu_addb_ctx, &adieu_addb_ctx_type,
			 &c2_addb_global_ctx);
	return C2_STOB_TYPE_OP(&c2_linux_stob_type, sto_init);
}

C2_INTERNAL void c2_linux_stobs_fini(void)
{
	C2_STOB_TYPE_OP(&c2_linux_stob_type, sto_fini);
	c2_addb_ctx_fini(&adieu_addb_ctx);
}

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
