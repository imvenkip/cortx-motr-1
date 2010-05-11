#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <db.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>


#include "linux.h"

/**
   @addtogroup stoblinux
   @{
 */

static void linux_stob_io_release(struct c2_stob_io *io)
{
}

static int linux_stob_io_launch(struct c2_stob_io *io, struct c2_dtx *tx,
			        struct c2_io_scope *scope)
{
	return 0;
}

static void linux_stob_io_cancel(struct c2_stob_io *io)
{
}

static const struct c2_stob_io_op linux_stob_io_op = {
	.sio_release = linux_stob_io_release,
	.sio_launch  = linux_stob_io_launch,
	.sio_cancel  = linux_stob_io_cancel
};

static void
db_err(DB_ENV *dbenv, int rc, const char *msg)
{
	dbenv->err(dbenv, rc, msg);
	printf("%s: %s", msg, db_strerror(rc));
	return;
}

void mapping_db_fini(struct c2_stob_domain_linux *sdl)
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


static int mapping_db_init(struct c2_stob_domain_linux *sdl)
{
	int            	 rc;
	DB_ENV 		*dbenv;
	const char      *backend_path = sdl->sdl_path;
	char 	         path[MAXPATHLEN];
	char		 *msg;

	rc = db_env_create(&dbenv, 0);
	if (rc != 0) {
		printf("db_env_create: %s", db_strerror(rc));
		return rc;
	}

	sdl->sdl_dbenv = dbenv;
	dbenv->set_errfile(dbenv, stderr);
	dbenv->set_errpfx(dbenv, "stob_linux");

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


/**
  Linux stob init
*/
static int linux_stob_init(struct c2_stob *stob)
{
	return 0;
}

/**
  Linux stob fini
*/
static void linux_stob_fini(struct c2_stob *stob)
{
}

static int linux_stob_io_init(struct c2_stob *stob, struct c2_stob_io *io)
{
	return 0;
}

static const struct c2_stob_op linux_stob_op = {
	.sop_init    = linux_stob_init,
	.sop_fini    = linux_stob_fini,
	.sop_io_init = linux_stob_io_init
};

static int stob_domain_linux_init(struct c2_stob_domain *self)
{
	struct c2_stob_domain_linux *sdl = container_of(self,
					  struct c2_stob_domain_linux, sdl_base);
	int rc;

	rc = mapping_db_init(sdl);
	return rc;
}

static void stob_domain_linux_fini(struct c2_stob_domain *self)
{
	struct c2_stob_domain_linux *sdl = container_of(self,
					  struct c2_stob_domain_linux, sdl_base);

	mapping_db_fini(sdl);
}

static int mapping_db_insert(struct c2_stob_domain_linux *sdl,
  		      	     const struct c2_stob_id *id,
		             const char *fname)
{
	DB_TXN               *tx = NULL;
	DB_ENV               *dbenv = sdl->sdl_dbenv;
	size_t                flen  = strlen(fname) + 1;
	int 		      rc;
	DBT keyt;
	DBT rect;

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

static int mapping_db_lookup(struct c2_stob_domain_linux *sdl,
  		      	     const struct c2_stob_id *id,
		             char *fname, int maxflen)
{
	DB_ENV               *dbenv = sdl->sdl_dbenv;
	size_t                flen  = maxflen;
	int 		      rc;
	DBT keyt;
	DBT rect;

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



static struct c2_stob *stob_domain_linux_alloc(struct c2_stob_domain *d,
                                               struct c2_stob_id *id)
{
	struct c2_stob_linux *sl;

	sl = (struct c2_stob_linux*)malloc(sizeof(*sl));
	if (sl != NULL) {
		sl->sl_stob.so_op = &linux_stob_op;
		sl->sl_stob.so_id = *id;
		c2_list_link_init(&sl->sl_stob.so_linkage);
		c2_list_add(&d->sd_objects, &sl->sl_stob.so_linkage);
		return &sl->sl_stob;
	} else
		return NULL;
}


/**
  Create an object

  Create an object on storage, establish the mapping from id to it in the db.
*/
static int stob_domain_linux_create(struct c2_stob_domain *d,
                                    struct c2_stob *o)
{
	struct c2_stob_domain_linux *sdl = container_of(d,
		 			   struct c2_stob_domain_linux, sdl_base);
	struct c2_stob_linux        *sl  = container_of(o, struct c2_stob_linux,
							sl_stob);	
	int fd;
	char *filename;

	sprintf(sl->sl_filename, "%s/Objects/LOXXXXXXXXXX", sdl->sdl_path);
	filename = mktemp(sl->sl_filename);

	if ((fd = open(filename, O_CREAT | O_RDWR)) > 0) {
		mapping_db_insert(sdl, &o->so_id, filename);
		sl->sl_fd = fd;
	}
	return 0;
}

/**
  Lookup an object with specified id

  Lookup an object with specified id in the mapping db.
*/
static int stob_domain_linux_locate(struct c2_stob_domain *d,
				    struct c2_stob *o)
{
	struct c2_stob_domain_linux *sdl = container_of(d,
		 			   struct c2_stob_domain_linux, sdl_base);
	struct c2_stob_linux        *sl  = container_of(o, struct c2_stob_linux,
							sl_stob);
	int rc;	

	memset(sl->sl_filename, 0, MAXPATHLEN);
	rc = mapping_db_lookup(sdl, &o->so_id, sl->sl_filename, MAXPATHLEN - 1);
	if (!rc) {
		sl->sl_fd = open(sl->sl_filename, O_RDWR);
		if (sl->sl_fd > 0)
			return 0;
		else
			return -EIO;
	} else
		return -ENOENT;
}

static const struct c2_stob_domain_op stob_domain_linux_op = {
	.sdops_init   = stob_domain_linux_init,
	.sdops_fini   = stob_domain_linux_fini,
	.sdops_alloc  = stob_domain_linux_alloc,
	.sdops_create = stob_domain_linux_create,
	.sdops_locate = stob_domain_linux_locate

};

struct c2_stob_domain_linux sdl = {
	.sdl_base = {
		.sd_name = "stob_domain_linux_t1",
		.sd_ops  = &stob_domain_linux_op,
	},
	.sdl_path = "./",
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
