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

#include "stob/linux.h"
#include "stob/linux_internal.h"
#include "stob/stob_addb.h"

/**
   @addtogroup stoblinux

   <b>Implementation of m0_stob on top of Linux files.</b>

   A linux storage object is simply a file on a local file system. A linux
   storage object domain is a directory containing

   @li data-base (db5) tables mapping storage object identifiers to local
   identifiers (not currently used) and

   @li a directory where files, corresponding to storage objects are stored
   in. A name of a file is built from the corresponding storage object local
   identifier (currently m0_stob_id is used).

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

struct m0_stob_type m0_linux_stob_type;
static const struct m0_stob_type_op linux_stob_type_op;
static const struct m0_stob_op linux_stob_op;
static const struct m0_stob_domain_op linux_stob_domain_op;

static void linux_stob_fini(struct m0_stob *stob);


/**
   Implementation of m0_stob_type_op::sto_init().
 */
static int linux_stob_type_init(struct m0_stob_type *stype)
{
	m0_stob_type_init(stype);
	return 0;
}

/**
   Implementation of m0_stob_type_op::sto_fini().
 */
static void linux_stob_type_fini(struct m0_stob_type *stype)
{
	m0_stob_type_fini(stype);
}

/**
   Implementation of m0_stob_domain_op::sdo_fini().

   Finalizes all still existing in-memory objects.
 */
static void linux_domain_fini(struct m0_stob_domain *self)
{
	struct linux_domain *ldom;

	ldom = domain2linux(self);
	linux_domain_io_fini(self);
	m0_stob_cache_fini(&ldom->sdl_cache);
	m0_stob_domain_fini(self);
	m0_free(ldom);
}

/**
   Implementation of m0_stob_type_op::sto_domain_locate().

   Initialises adieu sub-system for the domain.

   @note the domain returned is ready for use, but m0_linux_stob_setup() can be
   called against it in order to customize some configuration options (currently
   there is only one such option: "sdl_use_directio" flag).
 */
static int linux_stob_type_domain_locate(struct m0_stob_type *type,
					 const char *domain_name,
					 struct m0_stob_domain **out,
					 uint64_t dom_id)
{
	struct linux_domain   *ldom;
	struct m0_stob_domain *dom;
	int                    result;

	M0_ASSERT(domain_name != NULL);
	M0_ASSERT(strlen(domain_name) < ARRAY_SIZE(ldom->sdl_path));

	M0_ALLOC_PTR(ldom);
	if (ldom != NULL) {
		strcpy(ldom->sdl_path, domain_name);
		dom = &ldom->sdl_base;
		dom->sd_ops = &linux_stob_domain_op;
		m0_stob_domain_init(dom, type, dom_id);
		m0_stob_cache_init(&ldom->sdl_cache);
		result = linux_domain_io_init(dom);
		if (result == 0)
			*out = dom;
		else
			linux_domain_fini(dom);
		ldom->sdl_use_directio = false;
		dom->sd_name = ldom->sdl_path;
	} else {
		M0_STOB_OOM(LS_DOM_LOCATE);
		result = -ENOMEM;
	}
	return result;
}

M0_INTERNAL int m0_linux_stob_setup(struct m0_stob_domain *dom,
				    bool use_directio)
{
	struct linux_domain *ldom;

	ldom = domain2linux(dom);

	ldom->sdl_linux_setup  = true;
	ldom->sdl_use_directio = use_directio;

	return 0;
}

static bool linux_stob_invariant(const struct linux_stob *lstob)
{
	const struct m0_stob *stob;

	stob = &lstob->sl_stob.ca_stob;
	return
		(lstob->sl_fd >= 0) == (stob->so_state == CSS_EXISTS) &&
		stob->so_domain->sd_type == &m0_linux_stob_type;
}

static int linux_incache_init(struct m0_stob_domain *dom,
			      const struct m0_stob_id *id,
			      struct m0_stob_cacheable **out)
{
	struct linux_stob        *lstob;
	struct m0_stob_cacheable *incache;

	M0_ALLOC_PTR(lstob);
	if (lstob != NULL) {
		*out = incache = &lstob->sl_stob;
		incache->ca_stob.so_op = &linux_stob_op;
		m0_stob_cacheable_init(incache, id, dom);
		lstob->sl_fd = -1;
		return 0;
	} else {
		M0_STOB_OOM(LS_STOB_FIND);
		return -ENOMEM;
	}
}

/**
   Implementation of m0_stob_domain_op::sdo_stob_find().

   Returns an in-memory representation of the object with a given identifier.
 */
static int linux_domain_stob_find(struct m0_stob_domain *dom,
				  const struct m0_stob_id *id,
				  struct m0_stob **out)
{
	struct m0_stob_cacheable *incache;
	struct linux_domain      *ldom;
	int                       result;

	ldom = domain2linux(dom);
	result = m0_stob_cache_find(&ldom->sdl_cache, dom, id,
				    linux_incache_init, &incache);
	*out = &incache->ca_stob;
	return result;
}

/**
 * Implementation of m0_stob_op::sop_fini().
 *
 * Closes the object's file descriptor.
 *
 * @see m0_linux_stob_link()
 */
static void linux_stob_fini(struct m0_stob *stob)
{
	struct linux_stob *lstob;

	lstob = stob2linux(stob);
	M0_ASSERT(linux_stob_invariant(lstob));
	/*
	 * No caching for now, dispose of the body^Wobject immediately.
	 */
	if (lstob->sl_fd != -1) {
		close(lstob->sl_fd);
		lstob->sl_fd = -1;
	}
	m0_stob_cacheable_fini(&lstob->sl_stob);
	m0_free(lstob);
}

/**
   Implementation of m0_stob_domain_op::sdo_tx_make().
 */
static int linux_domain_tx_make(struct m0_stob_domain *dom, struct m0_dtx *tx)
{
	return 0;
}

/**
   Helper function constructing a file system path to the object.
 */
static int linux_stob_path(const struct linux_stob *lstob, int nr, char *path)
{
	int                   nob;
	struct linux_domain  *ldom;
	const struct m0_stob *stob;

	M0_ASSERT(linux_stob_invariant(lstob));

	stob = &lstob->sl_stob.ca_stob;
	ldom = domain2linux(stob->so_domain);
	nob = snprintf(path, nr, "%s/o/%016lx.%016lx", ldom->sdl_path,
		       stob->so_id.si_bits.u_hi, stob->so_id.si_bits.u_lo);
	return nob < nr ? 0 : -EOVERFLOW;
}

/**
   Helper function returns inode number of the linux stob object.
 */
M0_INTERNAL int m0_linux_stob_ino(struct m0_stob *obj)
{
	char		   pathname[MAXPATHLEN];
	int		   result;
	struct stat	   statbuf;
	struct linux_stob *lstob;

	M0_PRE(obj != NULL);

	lstob = stob2linux(obj);

	M0_ASSERT(linux_stob_invariant(lstob));

	result = linux_stob_path(lstob, ARRAY_SIZE(pathname), pathname) ?:
		 lstat(pathname, &statbuf);
	return result == 0 ? statbuf.st_ino : result;
}

/**
   Helper function opening the object in the file system.
 */
static int linux_stob_open(struct linux_stob *lstob, int oflag)
{
	char                 pathname[MAXPATHLEN];
	int                  result;
	struct stat          statbuf;

	M0_ASSERT(linux_stob_invariant(lstob));
	M0_ASSERT(lstob->sl_fd == -1);

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
   Implementation of m0_stob_op::sop_create().
 */
static int linux_stob_create(struct m0_stob *obj, struct m0_dtx *tx)
{
	int oflags = O_RDWR|O_CREAT;
	struct linux_domain *ldom;

	ldom  = domain2linux(obj->so_domain);
	if (ldom->sdl_use_directio)
		oflags |= O_DIRECT;

	return linux_stob_open(stob2linux(obj), oflags);
}

/**
   Implementation of m0_stob_op::sop_locate().
 */
static int linux_stob_locate(struct m0_stob *obj, struct m0_dtx *tx)
{
	int oflags = O_RDWR;
	struct linux_domain *ldom;

	ldom  = domain2linux(obj->so_domain);
	if (ldom->sdl_use_directio)
		oflags |= O_DIRECT;

	return linux_stob_open(stob2linux(obj), oflags);
}

static const struct m0_stob_type_op linux_stob_type_op = {
	.sto_init          = linux_stob_type_init,
	.sto_fini          = linux_stob_type_fini,
	.sto_domain_locate = linux_stob_type_domain_locate
};

static const struct m0_stob_domain_op linux_stob_domain_op = {
	.sdo_fini        = linux_domain_fini,
	.sdo_stob_find   = linux_domain_stob_find,
	.sdo_tx_make     = linux_domain_tx_make,
};

static const struct m0_stob_op linux_stob_op = {
	.sop_fini         = linux_stob_fini,
	.sop_create       = linux_stob_create,
	.sop_locate       = linux_stob_locate,
	.sop_io_init      = linux_stob_io_init,
	.sop_block_shift  = linux_stob_block_shift
};

struct m0_stob_type m0_linux_stob_type = {
	.st_op    = &linux_stob_type_op,
	.st_name  = "linuxstob",
	.st_magic = 0xACC01ADE
};

/**
   This function is called to link a path of an existing file to a stob id,
   for a given Linux stob. This path will be typically a block device path.

   @pre obj != NULL
   @pre path != NULL

   @param dom -> storage domain
   @param obj -> stob object embedded inside Linux stob object
   @param path -> Path to other file (typically block device)
   @param tx -> transaction context

   @see m0_linux_stob_open()
 */
M0_INTERNAL int m0_linux_stob_link(struct m0_stob_domain *dom,
				   struct m0_stob *obj, const char *path,
				   struct m0_dtx *tx)
{
	int                result;
	char               symlinkname[64];
	struct linux_stob *lstob;

	M0_PRE(obj != NULL);
	M0_PRE(path != NULL);

	lstob = stob2linux(obj);

	result = linux_stob_path(lstob, ARRAY_SIZE(symlinkname), symlinkname);
	if (result == 0) {
		result = symlink(path, symlinkname) < 0  ? -errno : 0;
	}

	return result;
}

M0_INTERNAL int m0_linux_stobs_init(void)
{
	return M0_STOB_TYPE_OP(&m0_linux_stob_type, sto_init);
}

M0_INTERNAL void m0_linux_stobs_fini(void)
{
	M0_STOB_TYPE_OP(&m0_linux_stob_type, sto_fini);
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
