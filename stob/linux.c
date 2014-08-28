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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_STOB
#include "lib/trace.h"

#include "stob/linux.h"

#include <stdio.h>			/* fopen */
#include <stdarg.h>			/* va_list */
#include <string.h>			/* strncpy */

#include <sys/types.h>			/* lstat */
#include <sys/stat.h>			/* lstat */
#include <unistd.h>			/* lstat */
#include <fcntl.h>			/* open */
#include <limits.h>			/* PATH_MAX */

#include "lib/errno.h"			/* ENOENT */
#include "lib/memory.h"			/* M0_ALLOC_PTR */
#include "lib/string.h"			/* m0_strdup */

#include "stob/type.h"			/* m0_stob_type_id_get */
#include "stob/stob_addb.h"		/* M0_STOB_OOM */
#include "stob/ioq.h"			/* m0_stob_ioq_init */

/**
   @addtogroup stoblinux

   <b>Implementation of m0_stob on top of Linux files.</b>

   A linux storage object is simply a file on a local file system. A linux
   storage object domain is a directory containing

   @li data-base (db5) tables mapping storage object identifiers to local
   identifiers (not currently used) and

   @li a directory where files, corresponding to storage objects are stored
   in. A name of a file is built from the corresponding storage object local
   identifier (stob_ket).

   A linux storage object domain is identified by the path to its directory.

   When an in-memory representation for an object is created, no file system
   operations are performed. It is only when the object is "located"
   (m0_stob_locate()) or "created" (m0_stob_create()) when actual open(2)
   system call is made. If the call was successful, the file descriptor
   (m0_stob_linux::sl_fd) remains open until the object is destroyed.

   <b>Direct I/O</b>

   To enable directio for a stob domain you should specify "directio=true"
   somewhere in str_cfg_init for m0_stob_domain_init() or
   m0_stob_domain_create().

   <b>Symlinks</b>

   To make stob pointing to other file on the filesystem just pass filename
   as str_cfg to m0_stob_create() to create a symlink instead of a file.

   @{
 */

enum {
	STOB_TYPE_LINUX = 0x01,
};

const struct m0_stob_type m0_stob_linux_type;

static struct m0_stob_type_ops stob_linux_type_ops;
static struct m0_stob_domain_ops stob_linux_domain_ops;
static struct m0_stob_ops stob_linux_ops;

static void stob_linux_type_register(struct m0_stob_type *type)
{
}

static void stob_linux_type_deregister(struct m0_stob_type *type)
{
}

M0_INTERNAL struct m0_stob_linux *m0_stob_linux_container(struct m0_stob *stob)
{
	return container_of(stob, struct m0_stob_linux, sl_stob);
}

M0_INTERNAL struct m0_stob_linux_domain *
m0_stob_linux_domain_container(struct m0_stob_domain *dom)
{
	return container_of(dom, struct m0_stob_linux_domain, sld_dom);
}

/**
 * Call vsnprintf() for the given parameters.
 *
 * - memory is allocated using m0_alloc() for the string returned;
 * - at most (MAXPATHLEN - 1) characters is in the string returned;
 * - NULL is returned in case of error (including too large string).
 */
static char *stob_linux_vsnprintf(const char *format, ...)
{
	va_list ap;
	char	str[MAXPATHLEN];
	size_t	len;

	va_start(ap, format);
	len = vsnprintf(str, ARRAY_SIZE(str), format, ap);
	va_end(ap);

	return len < 0 || len >= ARRAY_SIZE(str) ? NULL : m0_strdup(str);
}

static char *stob_linux_dir_domain(const char *path)
{
	return stob_linux_vsnprintf("%s", path);
}

static char *stob_linux_dir_stob(const char *path)
{
	return stob_linux_vsnprintf("%s/o", path);
}

static char *stob_linux_file_domain_id(const char *path)
{
	return stob_linux_vsnprintf("%s/id", path);
}

static char *stob_linux_file_stob(const char *path, uint64_t stob_key)
{
	return stob_linux_vsnprintf("%s/o/%016lx", path, stob_key);
}

static int stob_linux_domain_key_get_set(const char *path,
					 uint64_t *dom_key,
					 bool get)
{
	char *id_file_path;
	FILE *id_file;
	int   rc;
	int   rc1;

	id_file_path = stob_linux_file_domain_id(path);
	rc = id_file_path == NULL ? -EOVERFLOW : 0;
	if (rc == 0) {
		id_file = fopen(id_file_path, get ? "r" : "w");
		m0_free(id_file_path);
		if (id_file == NULL)
			rc = -errno;
	}
	if (rc == 0) {
		if (get) {
			rc = fscanf(id_file, "%lx\n", dom_key);
			rc = rc == EOF ? -errno : 0;
		} else {
			rc = fprintf(id_file, "%lx\n", *dom_key);
			rc = rc < 0 ? -errno : 0;
		}
		rc1 = fclose(id_file);
		rc = rc == 0 && rc1 != 0 ? rc1 : rc;
	}
	return rc;
}

static int stob_linux_domain_cfg_init_parse(const char *str_cfg_init,
					    void **cfg_init)
{
	struct m0_stob_linux_domain_cfg *cfg;
	int				 rc;

	M0_ALLOC_PTR(cfg);
	rc = cfg == NULL ? -ENOMEM : 0;
	if (rc == 0) {
		*cfg = (struct m0_stob_linux_domain_cfg) {
			.sldc_file_mode	   = 0700,
			.sldc_file_flags   = 0,
			.sldc_use_directio = false,
		};
		if (str_cfg_init != NULL) {
			cfg->sldc_use_directio = strstr(str_cfg_init,
						"directio=true") != NULL;
		}
	}
	if (rc == 0)
		*cfg_init = cfg;
	else
		m0_free(cfg);
	return rc;
}

static void stob_linux_domain_cfg_init_free(void *cfg_init)
{
	m0_free(cfg_init);
}

static int stob_linux_domain_cfg_create_parse(const char *str_cfg_create,
					      void **cfg_create)
{
	return 0;
}

static void stob_linux_domain_cfg_create_free(void *cfg_create)
{
}

static int stob_linux_domain_init(struct m0_stob_type *type,
				  const char *location_data,
				  void *cfg_init,
				  struct m0_stob_domain **out)
{
	struct m0_stob_linux_domain *ldom;
	uint64_t		     dom_key;
	uint64_t		     dom_id;
	uint8_t			     type_id;
	int			     rc;
	char			    *path;

	M0_PRE(location_data != NULL);

	M0_ALLOC_PTR(ldom);
	if (ldom != NULL)
		ldom->sld_cfg = *(struct m0_stob_linux_domain_cfg *)cfg_init;
	path = m0_strdup(location_data);
	rc = ldom == NULL || path == NULL ? -ENOMEM : 0;

	rc = rc ?: stob_linux_domain_key_get_set(path, &dom_key, true);
	rc = rc ?: m0_stob_domain__dom_key_is_valid(dom_key) ? 0 : -EINVAL;
	rc = rc ?: m0_stob_ioq_init(&ldom->sld_ioq);
	if (rc == 0) {
		m0_stob_ioq_directio_setup(&ldom->sld_ioq,
					   ldom->sld_cfg.sldc_use_directio);
		ldom->sld_dom.sd_ops = &stob_linux_domain_ops;
		ldom->sld_path	     = path;
		type_id = m0_stob_type_id_get(type);
		dom_id  = m0_stob_domain__dom_id(type_id, dom_key);
		m0_stob_domain__id_set(&ldom->sld_dom, dom_id);
	}
	if (rc == -ENOMEM)
		M0_STOB_OOM(LS_DOM_LOCATE);
	if (rc != 0) {
		m0_free(path);
		m0_free(ldom);
	}
	*out = rc == 0 ? &ldom->sld_dom : NULL;
	return rc;
}

static void stob_linux_domain_fini(struct m0_stob_domain *dom)
{
	struct m0_stob_linux_domain *ldom = m0_stob_linux_domain_container(dom);

	m0_stob_ioq_fini(&ldom->sld_ioq);
	m0_free(ldom->sld_path);
	m0_free(ldom);
}

static int stob_linux_domain_create_destroy(struct m0_stob_type *type,
					    const char *path,
					    uint64_t dom_key,
					    void *cfg,
					    bool create)
{
	mode_t	mode	    = 0700;	/** @todo get mode from create cfg */
	char   *dir_domain  = stob_linux_dir_domain(path);
	char   *dir_stob    = stob_linux_dir_stob(path);
	char   *file_dom_id = stob_linux_file_domain_id(path);
	char    cmd[PATH_MAX];
	int	rc;
	int	rc1;

	rc = dir_domain == NULL || dir_stob == NULL || file_dom_id == NULL ?
	     -ENOMEM : 0;
	if (rc != 0)
		goto out;
	if (!create)
		goto destroy;
	rc1 = mkdir(dir_domain, mode);
	rc = rc1 == -1 ? -errno : 0;
	if (rc != 0)
		goto out;
	rc1 = mkdir(dir_stob, mode);
	rc = rc1 == -1 ? -errno : 0;
	if (rc != 0)
		goto rmdir_domain;
	rc = stob_linux_domain_key_get_set(path, &dom_key, false);
	if (rc == 0)
		goto out;
destroy:
	/* XXX: One day this mess should be cleaned up.*/
	//rc1 = unlink(file_dom_id);
	snprintf(cmd, sizeof(cmd), "rm -fr %s", file_dom_id);
	rc1 = WEXITSTATUS(system(cmd));
	rc = rc1 == -1 ? -errno : rc;
	//rc1 = rmdir(dir_stob);
	snprintf(cmd, sizeof(cmd), "rm -fr %s", dir_stob);
	rc1 = WEXITSTATUS(system(cmd));
	rc = rc1 == -1 ? -errno : rc;
rmdir_domain:
	rc1 = rmdir(dir_domain);
	rc = rc1 == -1 ? -errno : rc;
out:
	m0_free(file_dom_id);
	m0_free(dir_stob);
	m0_free(dir_domain);
	return rc;
}

static int stob_linux_domain_create(struct m0_stob_type *type,
				    const char *location_data,
				    uint64_t dom_key,
				    void *cfg_create)
{
	return stob_linux_domain_create_destroy(type, location_data, dom_key,
						cfg_create, true);
}

static int stob_linux_domain_destroy(struct m0_stob_type *type,
				     const char *location_data)
{
	return stob_linux_domain_create_destroy(type, location_data, 0,
						NULL, false);
}

static struct m0_stob *stob_linux_alloc(struct m0_stob_domain *dom,
					uint64_t stob_key)
{
	struct m0_stob_linux *lstob;

	M0_ALLOC_PTR(lstob);
	return lstob == NULL ? NULL : &lstob->sl_stob;
}

static void stob_linux_free(struct m0_stob_domain *dom,
			    struct m0_stob *stob)
{
	if (stob != NULL)
		m0_free(m0_stob_linux_container(stob));
}

static int stob_linux_cfg_parse(const char *str_cfg_create,
				void **cfg_create)
{
	*cfg_create = str_cfg_create == NULL ? NULL : m0_strdup(str_cfg_create);
	return 0;
}

static void stob_linux_cfg_free(void *cfg_create)
{
	m0_free(cfg_create);
}


static int stob_linux_open(struct m0_stob *stob,
			   struct m0_stob_domain *dom,
			   uint64_t stob_key,
			   void *cfg,
			   bool create)
{
	struct m0_stob_linux_domain *ldom = m0_stob_linux_domain_container(dom);
	struct m0_stob_linux	    *lstob = m0_stob_linux_container(stob);
	struct stat		     statbuf;
	mode_t			     mode = ldom->sld_cfg.sldc_file_mode;
	char			    *path = ldom->sld_path;
	char			    *file_stob;
	int			     flags = ldom->sld_cfg.sldc_file_flags;
	int			     rc;

	stob->so_ops = &stob_linux_ops;
	lstob->sl_dom = ldom;

	file_stob = stob_linux_file_stob(path, stob_key);
	rc	  = file_stob == NULL ? -ENOMEM : 0;
	if (rc == 0) {
		rc = create && cfg != NULL ? symlink((char*)cfg, file_stob) : 0;
		flags |= O_RDWR;
		flags |= create && cfg == NULL		      ? O_CREAT  : 0;
		flags |= m0_stob_ioq_directio(&ldom->sld_ioq) ? O_DIRECT : 0;
		lstob->sl_fd = rc ?: open(file_stob, flags, mode);
		rc = lstob->sl_fd == -1 ? -errno : 0;
		if (rc == 0) {
			rc = fstat(lstob->sl_fd, &statbuf);
			rc = rc == -1 ? -errno : 0;
			if (rc == 0)
				lstob->sl_mode = statbuf.st_mode;
		}
	}
	m0_free(file_stob);
	return rc;
}

static int stob_linux_init(struct m0_stob *stob,
			   struct m0_stob_domain *dom,
			   uint64_t stob_key)
{
	return stob_linux_open(stob, dom, stob_key, NULL, false);
}

static void stob_linux_fini(struct m0_stob *stob)
{
	struct m0_stob_linux *lstob = m0_stob_linux_container(stob);
	int		      rc;

	if (lstob->sl_fd != -1) {
		rc = close(lstob->sl_fd);
		M0_ASSERT(rc == 0);
		lstob->sl_fd = -1;
	}
}

static void stob_linux_create_credit(struct m0_stob_domain *dom,
				     struct m0_be_tx_credit *accum)
{
}

static int stob_linux_create(struct m0_stob *stob,
			     struct m0_stob_domain *dom,
			     struct m0_dtx *dtx,
			     uint64_t stob_key,
			     void *cfg)
{
	return stob_linux_open(stob, dom, stob_key, cfg, true);
}

static int stob_linux_destroy_credit(struct m0_stob *stob,
				     struct m0_be_tx_credit *accum)
{
	return 0;
}

static int stob_linux_destroy(struct m0_stob *stob, struct m0_dtx *dtx)
{
	struct m0_stob_linux *lstob = m0_stob_linux_container(stob);
	char		     *file_stob;
	int		      rc;

	stob_linux_fini(stob);
	file_stob = stob_linux_file_stob(lstob->sl_dom->sld_path,
					 m0_stob_key_get(stob));
	rc	  = file_stob == NULL ? -ENOMEM : unlink(file_stob);
	m0_free(file_stob);
	return rc;
}

static void stob_linux_write_credit(struct m0_stob_domain *dom,
				    struct m0_indexvec *iv,
				    struct m0_be_tx_credit *accum)
{
}

static uint32_t stob_linux_block_shift(struct m0_stob *stob)
{
	struct m0_stob_linux *lstob = m0_stob_linux_container(stob);

	return m0_stob_ioq_bshift(&lstob->sl_dom->sld_ioq);
}

static struct m0_stob_type_ops stob_linux_type_ops = {
	.sto_register		     = &stob_linux_type_register,
	.sto_deregister		     = &stob_linux_type_deregister,
	.sto_domain_cfg_init_parse   = &stob_linux_domain_cfg_init_parse,
	.sto_domain_cfg_init_free    = &stob_linux_domain_cfg_init_free,
	.sto_domain_cfg_create_parse = &stob_linux_domain_cfg_create_parse,
	.sto_domain_cfg_create_free  = &stob_linux_domain_cfg_create_free,
	.sto_domain_init	     = &stob_linux_domain_init,
	.sto_domain_create	     = &stob_linux_domain_create,
	.sto_domain_destroy	     = &stob_linux_domain_destroy,
};

static struct m0_stob_domain_ops stob_linux_domain_ops = {
	.sdo_fini		= &stob_linux_domain_fini,
	.sdo_stob_alloc		= &stob_linux_alloc,
	.sdo_stob_free		= &stob_linux_free,
	.sdo_stob_cfg_parse	= &stob_linux_cfg_parse,
	.sdo_stob_cfg_free	= &stob_linux_cfg_free,
	.sdo_stob_init		= &stob_linux_init,
	.sdo_stob_create_credit = &stob_linux_create_credit,
	.sdo_stob_create	= &stob_linux_create,
	.sdo_stob_write_credit	= &stob_linux_write_credit,
};

static struct m0_stob_ops stob_linux_ops = {
	.sop_fini	    = &stob_linux_fini,
	.sop_destroy_credit = &stob_linux_destroy_credit,
	.sop_destroy	    = &stob_linux_destroy,
	.sop_io_init	    = &m0_stob_linux_io_init,
	.sop_block_shift    = &stob_linux_block_shift,
};

const struct m0_stob_type m0_stob_linux_type = {
	.st_ops  = &stob_linux_type_ops,
	.st_fidt = {
		.ft_id   = STOB_TYPE_LINUX,
		.ft_name = "linuxstob",
	},
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
