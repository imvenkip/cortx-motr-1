#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>


#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/queue.h"

#include "linux.h"
#include "linux_internal.h"

/**
   @addtogroup stoblinux

   @{
 */

struct c2_stob_type linux_stob_type;
static const struct c2_stob_type_op linux_stob_type_op;
static const struct c2_stob_op linux_stob_op;
static const struct c2_stob_domain_op linux_stob_domain_op;

#if 0
static void db_err(DB_ENV *dbenv, int rc, const char *msg)
{
	dbenv->err(dbenv, rc, msg);
	printf("%s: %s", msg, db_strerror(rc));
}

void mapping_db_fini(struct linux_domain *sdl)
{
	DB_ENV  *dbenv = sdl->sdl_dbenv;
	int 	 rc;
	
	if (dbenv) {
		rc = dbenv->log_flush(dbenv, NULL);
		if (sdl->sdl_mapping) {
			sdl->sdl_mapping->sync(sdl->sdl_mapping, 0);
			sdl->sdl_mapping->close(sdl->sdl_mapping, 0);
		}
		sdl->sdl_mapping = NULL;
		dbenv->close(dbenv, 0);
		sdl->sdl_dbenv = NULL;
	}
}


static int mapping_db_init(struct linux_domain *sdl)
{
	int         rc;
	DB_ENV 	   *dbenv;
	const char *backend_path = sdl->sdl_path;
	char 	    path[MAXPATHLEN];
	char	    *msg;

	rc = db_env_create(&dbenv, 0);
	if (rc != 0) {
		printf("db_env_create: %s", db_strerror(rc));
		return rc;
	}

	sdl->sdl_dbenv = dbenv;
	dbenv->set_errfile(dbenv, stderr);
	dbenv->set_errpfx(dbenv, "linux_stob");

	rc = dbenv->set_flags(dbenv, DB_TXN_NOSYNC, 1);
	if (rc != 0) {
		db_err(dbenv, rc, "->set_flags(DB_TXN_NOSYNC)");
		mapping_db_fini(sdl);
		return rc;
	}

	if (sdl->sdl_direct_db) {
		rc = dbenv->set_flags(dbenv, DB_DIRECT_DB, 1);
		if (rc != 0) {
			db_err(dbenv, rc, "->set_flags(DB_DIRECT_DB)");
			mapping_db_fini(sdl);
			return rc;
		}
		rc = dbenv->log_set_config(dbenv, DB_LOG_DIRECT, 1);
		if (rc != 0) {
			db_err(dbenv, rc, "->log_set_config()");
			mapping_db_fini(sdl);
			return rc;
		}
	}

	rc = dbenv->set_lg_bsize(dbenv, 1024*1024*1024);
	if (rc != 0) {
		db_err(dbenv, rc, "->set_lg_bsize()");
		mapping_db_fini(sdl);
		return rc;
	}

	rc = dbenv->log_set_config(dbenv, DB_LOG_AUTO_REMOVE, 1);
	if (rc != 0) {
		db_err(dbenv, rc, "->log_set_config(DB_LOG_AUTO_REMOVE)");
		mapping_db_fini(sdl);
		return rc;
	}

	rc = dbenv->set_cachesize(dbenv, 0, 1024 * 1024, 1);
	if (rc != 0) {
		db_err(dbenv, rc, "->set_cachesize()");
		mapping_db_fini(sdl);
		return rc;
	}

	rc = dbenv->set_thread_count(dbenv, sdl->sdl_nr_thread);
	if (rc != 0) {
		db_err(dbenv, rc, "->set_thread_count()");
		mapping_db_fini(sdl);
		return rc;
	}

#if 0
	dbenv->set_verbose(dbenv, DB_VERB_DEADLOCK, 1);
	dbenv->set_verbose(dbenv, DB_VERB_WAITSFOR, 1);
	dbenv->set_verbose(dbenv, DB_VERB_RECOVERY, 1);
	dbenv->set_verbose(dbenv, DB_VERB_FILEOPS, 1);
	dbenv->set_verbose(dbenv, DB_VERB_FILEOPS_ALL, 1);
#endif

	/* creating working directory */
	rc = mkdir(backend_path, 0700);
	if (rc != 0 && errno != EEXIST) {
		printf("mkdir(%s): %s", backend_path, strerror(errno));
		mapping_db_fini(sdl);
		return rc;
	}

	/* directory to hold objects */
	snprintf(path, MAXPATHLEN - 1, "%s/Objects", backend_path);
	rc = mkdir(path, 0700);
	if (rc != 0 && errno != EEXIST)
		printf("mkdir(%s): %s", path, strerror(errno));

	/* directory to hold mapping db */
	snprintf(path, MAXPATHLEN - 1, "%s/oi.db", backend_path);
	rc = mkdir(path, 0700);
	if (rc != 0 && errno != EEXIST)
		printf("mkdir(%s): %s", path, strerror(errno));

	snprintf(path, MAXPATHLEN - 1, "%s/oi.db/d", backend_path);
	mkdir(path, 0700);

	snprintf(path, MAXPATHLEN - 1, "%s/oi.db/d/o", backend_path);
	mkdir(path, 0700);

	snprintf(path, MAXPATHLEN - 1, "%s/oi.db/l", backend_path);
	mkdir(path, 0700);

	snprintf(path, MAXPATHLEN - 1, "%s/oi.db/t", backend_path);
	mkdir(path, 0700);

	rc = dbenv->set_tmp_dir(dbenv, "t");
	if (rc != 0)
		db_err(dbenv, rc, "->set_tmp_dir()");

	rc = dbenv->set_lg_dir(dbenv, "l");
	if (rc != 0)
		db_err(dbenv, rc, "->set_lg_dir()");

	rc = dbenv->set_data_dir(dbenv, "d");
	if (rc != 0)
		db_err(dbenv, rc, "->set_data_dir()");

	/* Open the environment with full transactional support. */
	sdl->sdl_dbenv_flags = DB_CREATE|DB_THREAD|DB_INIT_LOG|
	               	       DB_INIT_MPOOL|DB_INIT_TXN|DB_INIT_LOCK|
	                       DB_RECOVER;
	snprintf(path, MAXPATHLEN - 1, "%s/db4s.db", backend_path);
	rc = dbenv->open(dbenv, path, sdl->sdl_dbenv_flags, 0);
	if (rc != 0)
		db_err(dbenv, rc, "environment open");

	sdl->sdl_db_flags = DB_AUTO_COMMIT|DB_CREATE|
                            DB_THREAD|DB_TXN_NOSYNC|
	                    /*
	    		     * Both a data-base and a transaction
			     * must use "read uncommitted" to avoid
	    	             * dead-locks.
			     */
                             DB_READ_UNCOMMITTED;

	sdl->sdl_txn_flags = DB_READ_UNCOMMITTED|DB_TXN_NOSYNC;
	rc = db_create(&sdl->sdl_mapping, dbenv, 0);
	msg = "create";
	if (rc == 0) {
		rc = sdl->sdl_mapping->open(sdl->sdl_mapping, NULL, "o/oi", 
			               NULL, DB_BTREE, sdl->sdl_db_flags, 0664);
		msg = "open";
	}
	if (rc != 0) {
		dbenv->err(dbenv, rc, "database \"oi\": %s failure", msg);
		printf("database \"oi\": %s failure", msg);
	}
	return 0;
}

static int mapping_db_insert(struct linux_domain *sdl,
  		      	     const struct c2_stob_id *id,
		             const char *fname)
{
	DB_TXN *tx = NULL;
	DB_ENV *dbenv = sdl->sdl_dbenv;
	size_t  flen  = strlen(fname) + 1;
	int     rc;
	DBT     keyt;
	DBT     rect;

	memset(&keyt, 0, sizeof keyt);
	memset(&rect, 0, sizeof rect);

	keyt.data = (void*)id;
	keyt.size = sizeof *id;

	rect.data = (void*)fname;
	rect.size = flen;

	rc = dbenv->txn_begin(dbenv, NULL, &tx, sdl->sdl_txn_flags);
	if (tx == NULL) {
		db_err(dbenv, rc, "cannot start transaction");
		return rc;
	}

	rc = sdl->sdl_mapping->put(sdl->sdl_mapping, tx, &keyt, &rect, 0);
	if (rc == DB_LOCK_DEADLOCK)
		fprintf(stderr, "deadlock.\n");
	else if (rc != 0)
		db_err(dbenv, rc, "DB->put() cannot insert into database");

	if (rc == 0)
		rc = tx->commit(tx, 0);
	else
		rc = tx->abort(tx);
	if (rc != 0)
		db_err(dbenv, rc, "cannot commit/abort transaction");
	return rc;
}

static int mapping_db_lookup(struct linux_domain *sdl,
  		      	     const struct c2_stob_id *id,
		             char *fname, int maxflen)
{
	DB_ENV *dbenv = sdl->sdl_dbenv;
	size_t  flen  = maxflen;
	int     rc;
	DBT     keyt;
	DBT     rect;

	memset(&keyt, 0, sizeof keyt);
	memset(&rect, 0, sizeof rect);

	keyt.data = (void*)id;
	keyt.size = sizeof *id;

	rect.data = (void*)fname;
	rect.size = flen;

	rc = sdl->sdl_mapping->get(sdl->sdl_mapping, NULL, &keyt, &rect, 0);
	if (rc != 0)
		db_err(dbenv, rc, "DB->get() cannot get data from database");
	return rc;
}
#endif

static int linux_stob_type_init(struct c2_stob_type *stype)
{
	c2_stob_type_init(stype);
	return 0;
}

static void linux_stob_type_fini(struct c2_stob_type *stype)
{
	c2_stob_type_fini(stype);
}

static void linux_domain_fini(struct c2_stob_domain *self)
{
	struct linux_domain *ldom;

	ldom = domain2linux(self);
	linux_domain_io_fini(self);
	/* mapping_db_fini(ldom); */
	c2_list_fini(&ldom->sdl_object);
	c2_stob_domain_fini(self);
	c2_free(ldom);
}

static int linux_stob_type_domain_locate(struct c2_stob_type *type, 
					 const char *domain_name,
					 struct c2_stob_domain **out)
{
	struct linux_domain *ldom;
	struct c2_stob_domain       *dom;
	int                          result;

	C2_ASSERT(strlen(domain_name) < ARRAY_SIZE(ldom->sdl_path));

	C2_ALLOC_PTR(ldom);
	if (ldom != NULL) {
		c2_list_init(&ldom->sdl_object);
		strcpy(ldom->sdl_path, domain_name);
		dom = &ldom->sdl_base;
		dom->sd_ops = &linux_stob_domain_op;
		c2_stob_domain_init(dom, type);
		result = 0; /* mapping_db_init(ldom); */
		if (result == 0) {
			result = linux_domain_io_init(dom);
			if (result == 0)
				*out = dom;
		}
		if (result != 0)
			linux_domain_fini(dom);
	} else
		result = -ENOMEM;
	return result;
}

static bool linux_stob_invariant(struct linux_stob *lstob)
{
	struct c2_stob *stob;

	stob = &lstob->sl_stob;
	return
		((lstob->sl_fd >= 0) == (stob->so_state == CSS_EXISTS));
}

static struct linux_stob *linux_domain_lookup(struct linux_domain *ldom,
					      const struct c2_stob_id *id)
{
	struct linux_stob *obj;
	bool               found;

	found = false;
	c2_list_for_each_entry(&ldom->sdl_object, obj, 
			       struct linux_stob, sl_linkage) {
		if (c2_stob_id_eq(id, &obj->sl_stob.so_id)) {
			c2_atomic64_inc(&obj->sl_stob.so_ref);
			found = true;
			break;
		}
	}
	C2_ASSERT(linux_stob_invariant(obj));
	return found ? obj : NULL;
}

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
				c2_stob_init(stob, id);
				c2_list_add(&ldom->sdl_object, 
					    &lstob->sl_linkage);
			} else {
				c2_free(lstob);
				lstob = ghost;
				c2_stob_get(&lstob->sl_stob);
			}
			c2_rwlock_write_unlock(&dom->sd_guard);
		} else
			result = -ENOMEM;
	}
	if (result == 0) {
		*out = &lstob->sl_stob;
		C2_ASSERT(linux_stob_invariant(lstob));
	}
	return result;
}

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
	c2_list_del_init(&lstob->sl_linkage);
	c2_stob_fini(&lstob->sl_stob);
	c2_free(lstob);
}

static int linux_stob_create(struct c2_stob *stob)
{
	C2_ASSERT(linux_stob_invariant(stob2linux(stob)));
	return -ENOSYS;
}

static int linux_stob_locate(struct c2_stob *obj)
{
	struct linux_domain *ldom;
	struct linux_stob   *lstob;
	char                 pathname[40];
	int                  nob;
	int                  result;

	lstob = stob2linux(obj);
	ldom  = domain2linux(obj->so_domain);

	C2_ASSERT(linux_stob_invariant(lstob));
	C2_ASSERT(obj->so_state == CSS_UNKNOWN);
	C2_ASSERT(lstob->sl_fd == -1);

	nob = snprintf(pathname, ARRAY_SIZE(pathname), "%s/o/%016lx.%016lx", 
		       ldom->sdl_path, obj->so_id.si_seq, obj->so_id.si_id);
	if (nob < ARRAY_SIZE(pathname)) {
		lstob->sl_fd = open(pathname, O_RDWR);
		result = -errno;
	} else
		result = -EOVERFLOW;
	C2_ASSERT(linux_stob_invariant(lstob));
	return result;
}

static const struct c2_stob_type_op linux_stob_type_op = {
	.sto_init          = linux_stob_type_init,
	.sto_fini          = linux_stob_type_fini,
	.sto_domain_locate = linux_stob_type_domain_locate
};

static const struct c2_stob_domain_op linux_stob_domain_op = {
	.sdo_fini      = linux_domain_fini,
	.sdo_stob_find = linux_domain_stob_find
};

static const struct c2_stob_op linux_stob_op = {
	.sop_fini         = linux_stob_fini,
	.sop_create       = linux_stob_create,
	.sop_locate       = linux_stob_locate,
	.sop_io_init      = linux_stob_io_init,
	.sop_io_lock      = linux_stob_io_lock,
	.sop_io_unlock    = linux_stob_io_unlock,
	.sop_io_is_locked = linux_stob_io_is_locked,
};

struct c2_stob_type linux_stob_type = {
	.st_op    = &linux_stob_type_op,
	.st_name  = "linuxstob",
	.st_magic = 0xACC01ADE
};

int linux_stob_module_init(void)
{
	return linux_stob_type.st_op->sto_init(&linux_stob_type);
}

void linux_stob_module_fini(void)
{
	linux_stob_type.st_op->sto_fini(&linux_stob_type);
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
